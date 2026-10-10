#pragma once

// ============================================================================
// GeodeVersion.hpp — Geode mod.json `version` grammar validation
// ----------------------------------------------------------------------------
// Why this exists
// ---------------
// OptiGD v0.1.0-beta1 shipped a `mod.json` with `"version": "v0.1.0-beta1"`.
// Geode 5.10.1 rejects that string, and because Geode's JSON validation layer
// discards the detailed parse error, the only symptom surfaced to the user was:
//
//     [coderunknow.optigd/v0.0.0/mod.json].version: unexpected type string
//
// Root cause chain (verified against the pinned SDK sources):
//   1. `ModMetadata::Impl::parse` (loader/src/loader/ModMetadataImpl.cpp:288)
//          root.needs("version").into(impl->m_version);
//      where `m_version` is `geode::VersionInfo`, not `std::string`.
//   2. `matjson::Serialize<VersionInfo>::fromJson` calls `VersionInfo::parse`
//      (loader/include/Geode/utils/VersionInfo.hpp, Serialize specialization).
//   3. `VersionInfo::parse` (loader/src/utils/VersionInfo.cpp) accepts only
//      `alpha`, `beta` / `pr`, `prerelease` tags and — crucially — requires the
//      tag number to be separated by a DOT (`-beta.2`). A trailing digit with no
//      dot leaves the stream non-empty and returns
//          Err("Expected end of version, found '1'")
//   4. `JsonExpectedValue::tryGet<T>()`
//      (loader/include/Geode/utils/JsonValidation.hpp:100-118) only inspects the
//      *JSON* type when `as<T>()` fails:
//          this->setError("unexpected type {}", matJsonTypeToString(...type()));
//      The value is a JSON string, hence exactly "unexpected type string".
//   5. `ModMetadata::hasErrors()` then makes `LoaderImpl` (LoaderImpl.cpp:627)
//      report `LoadProblem::Type::InvalidGeodeFile` and refuse to load the mod.
//
// This header is a faithful, dependency-free port of the Geode 5.10.1
// `VersionInfo::parse` / `VersionTag::parse` grammar so that the project can
// validate its own version strings in unit tests and at runtime startup.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace optigd::core::geodever {

// Mirrors `geode::VersionTag::Type` from Geode 5.10.1
// (loader/include/Geode/utils/VersionInfo.hpp)
enum class VersionTagKind : uint8_t {
    Alpha = 0,
    Beta = 1,
    Prerelease = 2,
};

struct ParsedVersion final {
    bool valid{false};
    size_t major{0};
    size_t minor{0};
    size_t patch{0};
    std::optional<VersionTagKind> tag{};
    std::optional<size_t> tagNumber{};
    // Populated only when `valid == false`
    std::string error{};

    std::string toSuffixString() const;
    std::string toVString() const;
};

// Faithful port of geode::VersionInfo::parse (Geode 5.10.1).
// Accepts an optional leading `v`, MAJOR.MINOR.PATCH and an optional
// `-alpha[.N]` / `-beta[.N]` / `-prerelease[.N]` / `-pr[.N]` suffix.
ParsedVersion parseVersionString(std::string_view text);

// Convenience predicate used by the startup self-check and unit tests.
bool isValidGeodeVersionString(std::string_view text);

} // namespace optigd::core::geodever
