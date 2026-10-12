#!/usr/bin/env python3
"""Audit Geode mod-setting reads against the types declared in ``mod.json``.

Why this exists
---------------
``Mod::getSettingValue<T>(key)`` does not report a type mismatch. Geode resolves
``SettingTypeForValueType<T>::SettingType`` for the requested ``T`` and then
``cast::typeinfo_pointer_cast`` to the setting object; when the cast fails the
function returns a default-constructed ``T`` with no error and no log line
(``geode-sdk`` ``loader/include/Geode/loader/Mod.hpp``). ``listenForSettingChanges<T>``
uses the same mapping and simply never fires.

The consequence is a setting that looks configurable in Geode's UI and is
silently inert in the mod. ``v0.1.0-beta.3`` shipped exactly that for
``benchmark-duration-seconds``: declared ``"type": "int"``, read as ``double``,
so every capture ran with the built-in default duration and the user's number
was discarded without a trace.

The mapping this tool enforces (from ``SettingV3.hpp``):

===============================  =========================
C++ ``T``                        required ``mod.json`` type
===============================  =========================
``bool``                         ``bool``
any integral type                ``int``
any floating-point type          ``float``
``std::string``/``string_view``  ``string``
``std::filesystem::path``        ``file``
``ccColor3B``                    ``color``
``ccColor4B``                    ``color-alpha``
``std::vector<geode::Keybind>``  ``keybind``
===============================  =========================

A type outside this table is reported rather than guessed at.

Usage
-----
::

    python3 tools/audit_setting_types.py                 # audit this repository
    python3 tools/audit_setting_types.py --self-test     # mutation-test the auditor
    python3 tools/audit_setting_types.py --mod-json mod.json --src src

Exit status is ``0`` when every verifiable call site matches the declared type,
``1`` otherwise (mismatch, unknown key, unverifiable call site, unreadable
input).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

# ---------------------------------------------------------------------------
# The C++ -> mod.json setting-type mapping, transcribed from the SDK rather
# than invented. Anything not listed here is reported as unmappable instead of
# being assumed to be "probably fine".
# ---------------------------------------------------------------------------

INTEGRAL_TYPES = frozenset({
    "char", "signed char", "unsigned char",
    "short", "short int", "signed short", "signed short int",
    "unsigned short", "unsigned short int",
    "int", "signed", "signed int", "unsigned", "unsigned int",
    "long", "long int", "signed long", "signed long int",
    "unsigned long", "unsigned long int",
    "long long", "long long int", "signed long long", "signed long long int",
    "unsigned long long", "unsigned long long int",
    "int8_t", "int16_t", "int32_t", "int64_t",
    "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "int_fast8_t", "int_fast16_t", "int_fast32_t", "int_fast64_t",
    "uint_fast8_t", "uint_fast16_t", "uint_fast32_t", "uint_fast64_t",
    "int_least8_t", "int_least16_t", "int_least32_t", "int_least64_t",
    "uint_least8_t", "uint_least16_t", "uint_least32_t", "uint_least64_t",
    "intmax_t", "uintmax_t", "intptr_t", "uintptr_t",
    "size_t", "ptrdiff_t", "ssize_t",
    "wchar_t", "char16_t", "char32_t",
})

FLOATING_TYPES = frozenset({"float", "double", "long double"})

BOOL_TYPES = frozenset({"bool"})

STRING_TYPES = frozenset({
    "std::string", "string",
    "std::string_view", "string_view",
    "geode::ZStringView", "ZStringView",
})

FILE_TYPES = frozenset({"std::filesystem::path", "filesystem::path", "std::path"})

COLOR_TYPES = frozenset({"cocos2d::ccColor3B", "ccColor3B"})

COLOR_ALPHA_TYPES = frozenset({"cocos2d::ccColor4B", "ccColor4B"})

KEYBIND_TYPES = frozenset({
    "std::vector<geode::Keybind>", "std::vector<Keybind>",
    "std::span<geode::Keybind const>", "std::span<Keybind const>",
    "std::span<geode::Keybind>", "std::span<Keybind>",
})

EXPECTED_TYPE_FOR_CPP: Tuple[Tuple[frozenset, str], ...] = (
    (BOOL_TYPES, "bool"),
    (INTEGRAL_TYPES, "int"),
    (FLOATING_TYPES, "float"),
    (STRING_TYPES, "string"),
    (FILE_TYPES, "file"),
    (COLOR_TYPES, "color"),
    (COLOR_ALPHA_TYPES, "color-alpha"),
    (KEYBIND_TYPES, "keybind"),
)

# ``mod.json`` setting types this project may declare. A declared type outside
# this set is a metadata problem, not a C++ problem, and is reported as such.
KNOWN_SETTING_TYPES = frozenset({
    "bool", "int", "float", "string", "file", "folder",
    "color", "color-alpha", "keybind", "custom",
})

# The Geode entry points whose template argument decides which setting object is
# looked up. ``getSettingValue`` returns ``T()`` on a mismatch;
# ``listenForSettingChanges`` never fires; both are silent.
SETTING_ACCESSORS = ("getSettingValue", "listenForSettingChanges")

SOURCE_SUFFIXES = (".cpp", ".hpp", ".cc", ".cxx", ".h", ".hh", ".inl")


@dataclass(frozen=True)
class CallSite:
    """One ``getSettingValue<T>("key")`` / ``listenForSettingChanges<T>("key", ...)``."""

    path: Path
    line: int
    accessor: str
    cpp_type: Optional[str]  # None when the call has no template argument
    key: str

    @property
    def location(self) -> str:
        return f"{self.path}:{self.line}"


@dataclass(frozen=True)
class Finding:
    severity: str  # "error" | "note"
    message: str
    location: str = ""

    def render(self) -> str:
        where = f"{self.location}: " if self.location else ""
        return f"{self.severity.upper()}: {where}{self.message}"


def normalize_cpp_type(raw: str) -> str:
    """Collapse whitespace and strip a leading ``const`` / trailing reference."""

    text = re.sub(r"\s+", " ", raw).strip()
    text = re.sub(r"^const\s+", "", text)
    text = re.sub(r"\s*&+$", "", text)
    text = re.sub(r"\s*\*\s*$", "", text)
    return text


def expected_setting_type(cpp_type: str) -> Optional[str]:
    """Return the ``mod.json`` ``type`` a C++ ``T`` maps to, or ``None``."""

    normalized = normalize_cpp_type(cpp_type)
    for candidates, setting_type in EXPECTED_TYPE_FOR_CPP:
        if normalized in candidates:
            return setting_type
    # ``std::integral``/``std::floating_point`` qualified spellings.
    for prefix in ("std::", "geode::"):
        if normalized.startswith(prefix):
            stripped = normalized[len(prefix):]
            for candidates, setting_type in EXPECTED_TYPE_FOR_CPP:
                if stripped in candidates:
                    return setting_type
    return None


def strip_comments(text: str) -> str:
    """Blank out C/C++ comments while keeping every other byte in place.

    Line and column positions are preserved (comments become spaces, newlines
    stay newlines) so a reported line number still points at the source. String
    and character literals are respected: ``"http://example"`` is not a comment.
    """

    out: List[str] = []
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if ch == '"' or ch == "'":
            quote = ch
            out.append(ch)
            i += 1
            while i < n:
                if text[i] == "\\" and i + 1 < n:
                    out.append(text[i])
                    out.append(text[i + 1])
                    i += 2
                    continue
                out.append(text[i])
                if text[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        if ch == "/" and nxt == "/":
            while i < n and text[i] != "\n":
                out.append(" ")
                i += 1
            continue
        if ch == "/" and nxt == "*":
            while i < n and not (text[i] == "*" and i + 1 < n and text[i + 1] == "/"):
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            if i < n:
                out.append("  ")
                i += 2
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def _read_balanced_template(text: str, start: int) -> Tuple[str, int]:
    """Read a ``<...>`` argument list beginning at ``text[start] == '<'``."""

    depth = 0
    i = start
    n = len(text)
    while i < n:
        ch = text[i]
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
            if depth == 0:
                return text[start + 1:i], i + 1
        i += 1
    return text[start + 1:], n


def _read_string_literal(text: str, start: int) -> Tuple[Optional[str], int]:
    """Read a ``"..."`` literal beginning at ``text[start] == '\"'``."""

    i = start + 1
    n = len(text)
    chars: List[str] = []
    while i < n:
        ch = text[i]
        if ch == "\\" and i + 1 < n:
            chars.append(text[i + 1])
            i += 2
            continue
        if ch == '"':
            return "".join(chars), i + 1
        chars.append(ch)
        i += 1
    return None, n


def find_call_sites(text: str, path: Path) -> List[CallSite]:
    """Locate every setting accessor in a source file (comments excluded)."""

    clean = strip_comments(text)
    sites: List[CallSite] = []
    for accessor in SETTING_ACCESSORS:
        for match in re.finditer(re.escape(accessor), clean):
            i = match.end()
            cpp_type: Optional[str] = None
            if i < len(clean) and clean[i] == "<":
                cpp_type, i = _read_balanced_template(clean, i)
            while i < len(clean) and clean[i] in " \t\r\n":
                i += 1
            if i >= len(clean) or clean[i] != "(":
                continue  # not a call (e.g. a declaration or a doc reference)
            i += 1
            while i < len(clean) and clean[i] in " \t\r\n":
                i += 1
            if i >= len(clean) or clean[i] != '"':
                continue  # key is not a literal: nothing to audit
            key, _ = _read_string_literal(clean, i)
            if key is None:
                continue
            line = clean.count("\n", 0, match.start()) + 1
            sites.append(CallSite(
                path=path,
                line=line,
                accessor=accessor,
                cpp_type=normalize_cpp_type(cpp_type) if cpp_type is not None else None,
                key=key,
            ))
    sites.sort(key=lambda s: (str(s.path), s.line, s.accessor))
    return sites


def collect_call_sites(src_root: Path) -> List[CallSite]:
    sites: List[CallSite] = []
    for path in sorted(src_root.rglob("*")):
        if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError as exc:  # pragma: no cover - defensive
            raise SystemExit(f"error: cannot read {path}: {exc}") from exc
        sites.extend(find_call_sites(text, path))
    return sites


def declared_settings(mod_json: Path) -> Dict[str, str]:
    try:
        data = json.loads(mod_json.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise SystemExit(f"error: cannot read {mod_json}: {exc}") from exc
    settings = data.get("settings")
    if not isinstance(settings, dict):
        raise SystemExit(f"error: {mod_json} has no 'settings' object")
    declared: Dict[str, str] = {}
    for key, body in settings.items():
        if not isinstance(body, dict):
            raise SystemExit(f"error: {mod_json}: setting '{key}' is not an object")
        setting_type = body.get("type")
        if not isinstance(setting_type, str):
            raise SystemExit(f"error: {mod_json}: setting '{key}' has no string 'type'")
        declared[key] = setting_type
    return declared


def audit(mod_json: Path, src_root: Path) -> Tuple[List[CallSite], List[Finding]]:
    """Cross-check every call site against ``mod.json``. Never raises on findings."""

    declared = declared_settings(mod_json)
    sites = collect_call_sites(src_root)
    findings: List[Finding] = []

    for key, setting_type in sorted(declared.items()):
        if setting_type not in KNOWN_SETTING_TYPES:
            findings.append(Finding(
                "error",
                f"setting '{key}' declares unknown type '{setting_type}'",
                str(mod_json),
            ))

    read_keys = set()
    for site in sites:
        read_keys.add(site.key)
        if site.key not in declared:
            findings.append(Finding(
                "error",
                f"{site.accessor} reads '{site.key}', which mod.json does not declare; "
                "Geode returns a default-constructed value for undeclared keys",
                site.location,
            ))
            continue
        setting_type = declared[site.key]
        if site.cpp_type is None:
            findings.append(Finding(
                "error",
                f"{site.accessor}(\"{site.key}\") has no explicit template argument, so the "
                "requested type - and therefore whether it matches the declared "
                f"'{setting_type}' - cannot be verified",
                site.location,
            ))
            continue
        expected = expected_setting_type(site.cpp_type)
        if expected is None:
            findings.append(Finding(
                "error",
                f"{site.accessor}<{site.cpp_type}>(\"{site.key}\") uses a C++ type this "
                "auditor cannot map to a Geode setting type; extend the mapping in "
                "tools/audit_setting_types.py rather than assuming it is correct",
                site.location,
            ))
            continue
        if expected != setting_type:
            findings.append(Finding(
                "error",
                f"{site.accessor}<{site.cpp_type}>(\"{site.key}\") requests a '{expected}' "
                f"setting but mod.json declares '{setting_type}': Geode returns "
                f"{site.cpp_type}() silently and the setting never takes effect "
                "(and a change listener with this type never fires)",
                site.location,
            ))

    for key, setting_type in sorted(declared.items()):
        if key not in read_keys:
            findings.append(Finding(
                "note",
                f"setting '{key}' (type '{setting_type}') is declared but never read by "
                "code under the audited tree",
                str(mod_json),
            ))

    return sites, findings


def _display(path: Path) -> str:
    """Render a path relative to the working directory when it is inside it."""

    try:
        return str(path.resolve().relative_to(Path.cwd().resolve()))
    except ValueError:
        return str(path)


def report(mod_json: Path, src_root: Path, quiet: bool) -> int:
    sites, findings = audit(mod_json, src_root)
    errors = [f for f in findings if f.severity == "error"]
    notes = [f for f in findings if f.severity == "note"]

    if not quiet:
        print(f"setting-type audit: {_display(mod_json)} vs {_display(src_root)}")
        print(f"  declared settings : {len(json.loads(mod_json.read_text(encoding='utf-8')).get('settings', {}))}")
        print(f"  call sites found  : {len(sites)}")
        for site in sites:
            cpp = site.cpp_type if site.cpp_type is not None else "<no template argument>"
            print(f"    {_display(site.path)}:{site.line}: {site.accessor}<{cpp}>(\"{site.key}\")")
        if notes:
            print("  notes:")
            for note in notes:
                print(f"    {note.severity.upper()}: {_display(Path(note.location)) if note.location else ''}: {note.message}")
    for error in errors:
        where = f"{_display(Path(error.location))}: " if error.location else ""
        print(f"  {error.severity.upper()}: {where}{error.message}")

    if errors:
        print(f"setting-type audit: FAIL ({len(errors)} error(s))")
        return 1
    if not quiet:
        print("setting-type audit: PASS")
    return 0


# ---------------------------------------------------------------------------
# Self-test: the auditor is only worth registering in ctest if it actually
# fails on the defect it was written for. Each case below is a synthetic
# fixture; none of them is a measurement of the game.
# ---------------------------------------------------------------------------

MOD_JSON_TEMPLATE = """{
  "id": "example.mod",
  "name": "Example",
  "version": "v1.0.0",
  "settings": {
    %s
  }
}
"""


def _mod_json(entries: Sequence[Tuple[str, str]]) -> str:
    body = ",\n    ".join(
        f'"{key}": {{ "type": "{kind}", "name": "{key}", "description": "fixture" }}'
        for key, kind in entries
    )
    return MOD_JSON_TEMPLATE % body


@dataclass(frozen=True)
class Case:
    name: str
    settings: Sequence[Tuple[str, str]]
    source: str
    expect_pass: bool
    expect_substring: str = ""


SELF_TEST_CASES: Tuple[Case, ...] = (
    Case(
        "matching types across the whole mapping",
        (("flag", "bool"), ("count", "int"), ("scale", "float"), ("name", "string"),
         ("binds", "keybind"), ("tint", "color"), ("tint-a", "color-alpha")),
        """
        void reads(Mod* mod) {
            bool a = mod->getSettingValue<bool>("flag");
            int64_t b = mod->getSettingValue<int64_t>("count");
            double c = mod->getSettingValue<double>("scale");
            std::string d = mod->getSettingValue<std::string>("name");
            auto e = mod->getSettingValue<std::vector<geode::Keybind>>("binds");
            auto f = mod->getSettingValue<cocos2d::ccColor3B>("tint");
            auto g = mod->getSettingValue<cocos2d::ccColor4B>("tint-a");
            (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g;
        }
        """,
        True,
    ),
    Case(
        "the beta.3 defect: an int setting read as double",
        (("benchmark-duration-seconds", "int"),),
        """
        int64_t apply(Mod* mod) {
            const double seconds = mod->getSettingValue<double>("benchmark-duration-seconds");
            return static_cast<int64_t>(seconds);
        }
        """,
        False,
        "requests a 'float' setting but mod.json declares 'int'",
    ),
    Case(
        "a bool setting read as a string",
        (("enable-thing", "bool"),),
        """
        void reads(Mod* mod) {
            auto value = mod->getSettingValue<std::string>("enable-thing");
            (void)value;
        }
        """,
        False,
        "requests a 'string' setting but mod.json declares 'bool'",
    ),
    Case(
        "a float setting read as an integer",
        (("target-fps", "float"),),
        """
        void reads(Mod* mod) {
            auto value = mod->getSettingValue<int>("target-fps");
            (void)value;
        }
        """,
        False,
        "requests a 'int' setting but mod.json declares 'float'",
    ),
    Case(
        "a change listener with the wrong type never fires",
        (("count", "int"),),
        """
        void hook() {
            geode::listenForSettingChanges<double>("count", [](double) {});
        }
        """,
        False,
        "listenForSettingChanges<double>(\"count\")",
    ),
    Case(
        "a key that mod.json does not declare",
        (("count", "int"),),
        """
        void reads(Mod* mod) {
            auto value = mod->getSettingValue<int64_t>("typo-count");
            (void)value;
        }
        """,
        False,
        "which mod.json does not declare",
    ),
    Case(
        "a call site with no template argument cannot be verified",
        (("count", "int"),),
        """
        void reads(Mod* mod) {
            auto value = mod->getSettingValue("count");
            (void)value;
        }
        """,
        False,
        "cannot be verified",
    ),
    Case(
        "a C++ type outside the mapping is reported, not guessed",
        (("count", "int"),),
        """
        struct Widget {};
        void reads(Mod* mod) {
            auto value = mod->getSettingValue<Widget>("count");
            (void)value;
        }
        """,
        False,
        "cannot map to a Geode setting type",
    ),
    Case(
        "mentions inside comments and strings are not call sites",
        (("count", "int"),),
        """
        // mod->getSettingValue<double>("count") would be wrong; do not write that.
        /* also not a call: listenForSettingChanges<std::string>("count", ...) */
        const char* doc = "call getSettingValue<int64_t>(\"count\")";
        void reads(Mod* mod) {
            auto value = mod->getSettingValue<int64_t>("count");
            (void)value; (void)doc;
        }
        """,
        True,
    ),
    Case(
        "a declared-but-unread setting is a note, not an error",
        (("count", "int"), ("unused", "bool")),
        """
        void reads(Mod* mod) {
            auto value = mod->getSettingValue<int64_t>("count");
            (void)value;
        }
        """,
        True,
    ),
    Case(
        "an unknown declared type is a metadata error",
        (("count", "integer"),),
        """
        void reads(Mod* mod) {
            auto value = mod->getSettingValue<int64_t>("count");
            (void)value;
        }
        """,
        False,
        "declares unknown type 'integer'",
    ),
    Case(
        "qualified and const spellings are normalized",
        (("count", "int"), ("scale", "float")),
        """
        void reads(Mod* mod) {
            auto a = mod->getSettingValue<std::int64_t>("count");
            auto b = mod->getSettingValue<const double&>("scale");
            (void)a; (void)b;
        }
        """,
        True,
    ),
)


def _run_case(tmp: Path, index: int, case: Case) -> Tuple[bool, str]:
    root = tmp / f"case{index}"
    (root / "src").mkdir(parents=True)
    mod_json = root / "mod.json"
    mod_json.write_text(_mod_json(case.settings), encoding="utf-8")
    source = root / "src" / "Hooks.cpp"
    source.write_text(case.source, encoding="utf-8")

    sites, findings = audit(mod_json, root / "src")
    errors = [f for f in findings if f.severity == "error"]
    passed = not errors
    detail = "; ".join(f.message for f in findings) or f"{len(sites)} call site(s), no findings"
    if passed != case.expect_pass:
        return False, f"expected {'PASS' if case.expect_pass else 'FAIL'}, got {'PASS' if passed else 'FAIL'} ({detail})"
    if not case.expect_pass and case.expect_substring and case.expect_substring not in detail:
        return False, f"failure text missing {case.expect_substring!r} (got: {detail})"
    if case.expect_pass and len(sites) == 0 and "comments" not in case.name and "unread" not in case.name:
        return False, "the auditor found no call sites at all, so the PASS proves nothing"
    return True, detail


def self_test() -> int:
    failures: List[str] = []
    with tempfile.TemporaryDirectory(prefix="optigd-setting-audit-") as tmpdir:
        tmp = Path(tmpdir)
        for index, case in enumerate(SELF_TEST_CASES):
            ok, detail = _run_case(tmp, index, case)
            status = "ok" if ok else "FAIL"
            print(f"  [{status}] {case.name}")
            if not ok:
                failures.append(f"{case.name}: {detail}")

    # The auditor must also be honest about its own reach: a source tree with no
    # settings at all is a PASS with zero call sites, not a crash.
    with tempfile.TemporaryDirectory(prefix="optigd-setting-audit-empty-") as tmpdir:
        tmp = Path(tmpdir)
        (tmp / "src").mkdir()
        (tmp / "mod.json").write_text(_mod_json(()), encoding="utf-8")
        sites, findings = audit(tmp / "mod.json", tmp / "src")
        if sites or [f for f in findings if f.severity == "error"]:
            failures.append("empty tree produced call sites or errors")
        else:
            print("  [ok] empty source tree")

    if failures:
        print("audit_setting_types.py self-test: FAIL")
        for failure in failures:
            print(f"  - {failure}")
        return 1
    print(f"audit_setting_types.py self-test: PASS ({len(SELF_TEST_CASES) + 1} cases)")
    return 0


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Audit Geode setting reads against the types declared in mod.json.",
    )
    repo_root = Path(__file__).resolve().parent.parent
    parser.add_argument("--mod-json", type=Path, default=repo_root / "mod.json",
                        help="path to mod.json (default: the repository's mod.json)")
    parser.add_argument("--src", type=Path, default=repo_root / "src",
                        help="source tree to scan (default: the repository's src/)")
    parser.add_argument("--quiet", action="store_true",
                        help="print findings only")
    parser.add_argument("--self-test", action="store_true",
                        help="mutation-test the auditor against synthetic fixtures and exit")
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test()
    if not args.mod_json.is_file():
        print(f"error: no such file: {args.mod_json}")
        return 1
    if not args.src.is_dir():
        print(f"error: no such directory: {args.src}")
        return 1
    return report(args.mod_json, args.src, args.quiet)


if __name__ == "__main__":
    sys.exit(main())
