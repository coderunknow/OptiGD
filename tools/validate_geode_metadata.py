#!/usr/bin/env python3
"""
OptiGD Geode metadata & settings-schema validator.

Validates `mod.json` against the rules actually enforced by the pinned Geode
SDK (v5.10.1) and cross-checks the declared version/target metadata against the
C++ and CMake sources.

Why this exists
---------------
OptiGD v0.1.0-beta1 shipped `"version": "v0.1.0-beta1"`, which Geode 5.10.1
rejects. The failure was reported to users only as:

    [coderunknow.optigd/v0.0.0/mod.json].version: unexpected type string

because `JsonExpectedValue::tryGet<T>()` (loader/include/Geode/utils/
JsonValidation.hpp) replaces the detailed matjson error with the *JSON* type of
the offending value. This script reproduces the semantics of the loader's own
parser so the mistake can never reach a released package again.

Rules implemented (all traced to geode-sdk/geode @ v5.10.1):
  * loader/src/loader/ModMetadataImpl.cpp   - required keys, `gd` object shape,
                                              mod id regex, settings collection
  * loader/src/utils/VersionInfo.cpp        - VersionInfo / VersionTag grammar
  * loader/src/loader/ModSettingsManager.cpp- known setting type generators
  * loader/src/loader/SettingV3.cpp         - per-type keys and constraints

Usage:
    python3 tools/validate_geode_metadata.py [--json] [path/to/mod.json]
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# [a-z0-9\-_]+\.[a-z0-9\-_]+  (ModMetadataImpl.cpp ID_REGEX)
MOD_ID_RE = re.compile(r"^[a-z0-9\-_]+\.[a-z0-9\-_]+$")

# Setting types with a built-in generator (ModSettingsManager.cpp:31-45)
BUILTIN_SETTING_TYPES = {
    "title", "info", "button", "bool", "int", "float",
    "string", "file", "folder", "rgb", "color", "rgba", "keybind",
}

# Keys every setting supports (SettingV3::parseBaseProperties + init)
COMMON_SETTING_KEYS = {
    "type", "name", "description", "requires-restart", "platforms",
    "enable-if", "enable-if-description", "default",
}

# Extra keys accepted per setting type (SettingV3.cpp per-type parse())
EXTRA_SETTING_KEYS = {
    "title": set(),
    "info": {"text", "no-arrow", "no-separator", "title"},
    "button": set(),
    "bool": set(),
    "int": {"min", "max", "control"},
    "float": {"min", "max", "control"},
    "string": {"match", "filter", "one-of"},
    "file": {"control"},
    "folder": {"control"},
    "rgb": set(),
    "color": set(),
    "rgba": set(),
    "keybind": {"default"},
}

# Expected JSON type of the `default` value per setting type
DEFAULT_JSON_TYPES = {
    "bool": (bool,),
    "int": (int,),
    "float": (int, float),
    "string": (str,),
    "file": (str,),
    "folder": (str,),
    "rgb": (list,),
    "color": (list,),
    "rgba": (list,),
    "keybind": (str, list),
}

# Custom setting types registered at build time by this mod (if any)
CUSTOM_TYPES_PREFIX = "custom:"


class ValidationError(Exception):
    pass


# ---------------------------------------------------------------------------
# Port of geode::VersionInfo::parse / geode::VersionTag::parse
# (loader/src/utils/VersionInfo.cpp @ v5.10.1)
# ---------------------------------------------------------------------------

VALID_TAGS = ("alpha", "beta", "prerelease", "pr")


def parse_geode_version(text: str):
    """Returns (ok, message). Mirrors the SDK's strict parser exactly."""
    if not isinstance(text, str):
        return False, "version must be a JSON string"

    i = 0
    n = len(text)
    if i < n and text[i] == 'v':
        i += 1

    def read_uint():
        nonlocal i
        start = i
        while i < n and text[i].isdigit():
            i += 1
        if start == i:
            return None
        return int(text[start:i])

    major = read_uint()
    if major is None:
        return False, "Unable to parse major"
    if i >= n or text[i] != '.':
        return False, "Minor version missing"
    i += 1
    minor = read_uint()
    if minor is None:
        return False, "Unable to parse minor"
    if i >= n or text[i] != '.':
        return False, "Patch version missing"
    i += 1
    patch = read_uint()
    if patch is None:
        return False, "Unable to parse patch"

    if i < n and text[i] == '-':
        i += 1
        start = i
        while i < n and 'a' <= text[i] <= 'z':
            i += 1
        ident = text[start:i]
        if ident not in VALID_TAGS:
            return False, 'Invalid tag "%s"' % ident
        if i < n and text[i] == '.':
            i += 1
            num = read_uint()
            if num is None:
                return False, "Unable to parse tag number"

    if i != n:
        return False, "Expected end of version, found '%s'" % text[i]
    return True, "v%d.%d.%d" % (major, minor, patch)


# ---------------------------------------------------------------------------
# mod.json validation
# ---------------------------------------------------------------------------

def validate_metadata(doc: dict, errors: list, warnings: list) -> None:
    for key in ("geode", "gd", "id", "version", "name"):
        if key not in doc:
            errors.append("mod.json: required key '%s' is missing" % key)

    geode = doc.get("geode")
    if geode is not None:
        if not isinstance(geode, str):
            errors.append("mod.json: 'geode' must be a string")
        else:
            ok, msg = parse_geode_version(geode)
            if not ok:
                errors.append("mod.json.geode: %s" % msg)

    gd = doc.get("gd")
    if gd is not None:
        if not isinstance(gd, dict):
            errors.append("mod.json: 'gd' must be an object "
                          "(ModMetadataImpl.cpp calls gd.assertIsObject())")
        else:
            for platform, value in gd.items():
                if not isinstance(value, str):
                    errors.append("mod.json.gd.%s: must be a string" % platform)
                elif value != "*":
                    try:
                        float(value)
                    except ValueError:
                        errors.append(
                            "mod.json.gd.%s: '%s' is not '*' or a numeric version"
                            % (platform, value))

    mod_id = doc.get("id")
    if mod_id is not None and not MOD_ID_RE.match(str(mod_id)):
        errors.append("mod.json.id: '%s' does not match %s" % (mod_id, MOD_ID_RE.pattern))

    version = doc.get("version")
    if version is not None:
        if not isinstance(version, str):
            errors.append("mod.json.version: must be a string")
        else:
            ok, msg = parse_geode_version(version)
            if not ok:
                errors.append(
                    "mod.json.version: %s\n"
                    "    -> Geode reports this failure only as "
                    "'unexpected type string' (JsonValidation.hpp tryGet<T>)\n"
                    "    -> Use the vMAJOR.MINOR.PATCH-tag.N form, e.g. "
                    "v0.1.0-beta.2" % msg)

    if "developer" in doc and "developers" in doc:
        errors.append("mod.json: cannot have both 'developer' and 'developers'")

    for tag in doc.get("tags", []):
        if not isinstance(tag, str):
            errors.append("mod.json.tags: entries must be strings")
            break


def validate_settings(doc: dict, errors: list, warnings: list) -> None:
    settings = doc.get("settings")
    if settings is None:
        return
    if not isinstance(settings, dict):
        errors.append("mod.json.settings: must be an object")
        return

    for key, spec in settings.items():
        if not isinstance(spec, dict):
            errors.append("mod.json.settings.%s: must be an object" % key)
            continue

        stype = spec.get("type")
        if stype is None:
            errors.append("mod.json.settings.%s: missing 'type'" % key)
            continue
        if not isinstance(stype, str):
            errors.append("mod.json.settings.%s: 'type' must be a string" % key)
            continue

        if stype.startswith(CUSTOM_TYPES_PREFIX):
            warnings.append(
                "mod.json.settings.%s: custom type '%s' requires a registered "
                "generator" % (key, stype))
            continue

        if stype not in BUILTIN_SETTING_TYPES:
            errors.append("mod.json.settings.%s: unknown setting type '%s' "
                          "(known: %s)" % (key, stype,
                                           ", ".join(sorted(BUILTIN_SETTING_TYPES))))
            continue

        allowed = COMMON_SETTING_KEYS | EXTRA_SETTING_KEYS.get(stype, set())
        for sub in spec:
            if sub not in allowed:
                errors.append("mod.json.settings.%s: unsupported key '%s' for "
                              "type '%s'" % (key, sub, stype))

        if "default" not in spec:
            errors.append("mod.json.settings.%s: missing 'default' "
                          "(SettingBaseValueV3::parseDefaultValue)" % key)
        else:
            expected = DEFAULT_JSON_TYPES.get(stype)
            if expected is not None and not isinstance(spec["default"], expected):
                errors.append("mod.json.settings.%s: default value type %s is "
                              "invalid for '%s' setting"
                              % (key, type(spec["default"]).__name__, stype))

        for bound in ("min", "max"):
            if bound in spec and not isinstance(spec[bound], (int, float)):
                errors.append("mod.json.settings.%s: '%s' must be a number"
                              % (key, bound))
        if "min" in spec and "max" in spec and spec["min"] > spec["max"]:
            errors.append("mod.json.settings.%s: 'min' > 'max'" % key)

        if "one-of" in spec:
            one_of = spec["one-of"]
            if not isinstance(one_of, list):
                errors.append("mod.json.settings.%s: 'one-of' must be an array"
                              % key)
            elif not one_of:
                errors.append('mod.json.settings.%s: "one-of" may not be empty!'
                              % key)
            elif not all(isinstance(v, str) for v in one_of):
                errors.append("mod.json.settings.%s: 'one-of' entries must be "
                              "strings" % key)
            elif "default" in spec and spec["default"] not in one_of:
                errors.append("mod.json.settings.%s: default '%s' is not listed "
                              "in 'one-of'" % (key, spec["default"]))

        for platform_key in ("platforms",):
            if platform_key in spec and not isinstance(spec[platform_key], list):
                errors.append("mod.json.settings.%s: '%s' must be an array"
                              % (key, platform_key))


def _read_first(path: Path, pattern: str) -> str | None:
    if not path.exists():
        return None
    match = re.search(pattern, path.read_text(encoding="utf-8"))
    return match.group(1) if match else None


def validate_cross_references(doc: dict, errors: list, warnings: list) -> None:
    types_hpp = REPO_ROOT / "src" / "core" / "Types.hpp"
    cmake = REPO_ROOT / "CMakeLists.txt"

    declared = _read_first(types_hpp, r'kOptiGDVersionTag\s*=\s*"([^"]+)"')
    if declared is None:
        warnings.append("src/core/Types.hpp: kOptiGDVersionTag not found")
    elif str(doc.get("version")) != declared:
        errors.append("version mismatch: mod.json=%r but Types.hpp "
                      "kOptiGDVersionTag=%r" % (doc.get("version"), declared))

    plain = _read_first(types_hpp, r'kOptiGDVersion\s*=\s*"([^"]+)"')
    if plain is not None and declared is not None:
        if declared != "v" + plain:
            errors.append("version mismatch: kOptiGDVersionTag=%r is not "
                          "'v' + kOptiGDVersion=%r" % (declared, plain))

    cmake_version = _read_first(
        cmake, r'set\(OPTIGD_VERSION_STRING\s+"([^"]+)"\)')
    if cmake_version is None:
        warnings.append("CMakeLists.txt: OPTIGD_VERSION_STRING not found")
    elif plain is not None and cmake_version != plain:
        errors.append("version mismatch: CMakeLists OPTIGD_VERSION_STRING=%r "
                      "!= Types.hpp kOptiGDVersion=%r" % (cmake_version, plain))

    target_gd = _read_first(types_hpp, r'kTargetGDVersion\s*=\s*"([^"]+)"')
    gd = doc.get("gd") or {}
    if target_gd is not None:
        for platform, value in gd.items():
            if value != "*" and value != target_gd:
                warnings.append("gd version mismatch: mod.json.gd.%s=%r but "
                                "Types.hpp kTargetGDVersion=%r"
                                % (platform, value, target_gd))

    target_geode = _read_first(types_hpp, r'kTargetGeodeVersion\s*=\s*"([^"]+)"')
    if target_geode is not None and doc.get("geode"):
        if target_geode.lstrip("v") != str(doc["geode"]).lstrip("v"):
            warnings.append("geode target mismatch: mod.json.geode=%r but "
                            "Types.hpp kTargetGeodeVersion=%r"
                            % (doc["geode"], target_geode))


def validate(path: Path):
    errors: list = []
    warnings: list = []

    if not path.exists():
        raise ValidationError("mod.json not found at %s" % path)
    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise ValidationError("mod.json is not valid JSON: %s" % exc)

    if not isinstance(doc, dict):
        raise ValidationError("mod.json root must be an object")

    validate_metadata(doc, errors, warnings)
    validate_settings(doc, errors, warnings)
    validate_cross_references(doc, errors, warnings)
    return doc, errors, warnings


def main(argv):
    args = [a for a in argv[1:] if a != "--json"]
    as_json = "--json" in argv[1:]
    target = Path(args[0]).resolve() if args else (REPO_ROOT / "mod.json")

    try:
        doc, errors, warnings = validate(target)
    except ValidationError as exc:
        print("FAIL: %s" % exc)
        return 2

    if as_json:
        print(json.dumps({
            "file": str(target),
            "geode": doc.get("geode"),
            "gd": doc.get("gd"),
            "version": doc.get("version"),
            "settings": len(doc.get("settings", {})),
            "errors": errors,
            "warnings": warnings,
        }, indent=2))
    else:
        print("OptiGD Geode metadata validation")
        print("  file      : %s" % target)
        print("  geode     : %s" % doc.get("geode"))
        print("  gd        : %s" % doc.get("gd"))
        print("  version   : %s" % doc.get("version"))
        print("  settings  : %d declared" % len(doc.get("settings", {})))
        for w in warnings:
            print("  [WARN] %s" % w)
        for e in errors:
            print("  [ERROR] %s" % e)
        print("  result    : %s" % ("FAIL" if errors else "PASS"))

    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
