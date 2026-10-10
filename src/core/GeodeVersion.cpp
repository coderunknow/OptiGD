#include "GeodeVersion.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace optigd::core::geodever {

namespace {

// Port of geode::VersionTag::parse (loader/src/utils/VersionInfo.cpp).
// Consumes the identifier (lowercase letters only, exactly as the SDK does) and
// an OPTIONAL dot-separated number. Anything else leaves trailing input, which
// the caller (VersionInfo::parse) rejects with "Expected end of version".
struct TagParseResult {
    bool ok{false};
    VersionTagKind kind{VersionTagKind::Alpha};
    std::optional<size_t> number{};
    std::string error{};
};

TagParseResult parseTag(std::stringstream& str) {
    TagParseResult out{};

    std::string iden;
    while ('a' <= str.peek() && str.peek() <= 'z') {
        iden += static_cast<char>(str.get());
    }
    if (str.fail()) {
        out.error = "Unable to parse tag";
        return out;
    }

    if (iden == "alpha") {
        out.kind = VersionTagKind::Alpha;
    } else if (iden == "beta") {
        out.kind = VersionTagKind::Beta;
    } else if (iden == "prerelease" || iden == "pr") {
        out.kind = VersionTagKind::Prerelease;
    } else {
        out.error = "Invalid tag \"" + iden + "\"";
        return out;
    }

    if (str.peek() == '.') {
        str.get();
        size_t num = 0;
        str >> num;
        if (str.fail()) {
            out.error = "Unable to parse tag number";
            return out;
        }
        out.number = num;
    }

    out.ok = true;
    return out;
}

std::string tagToSuffix(VersionTagKind kind) {
    switch (kind) {
        case VersionTagKind::Alpha: return "-alpha";
        case VersionTagKind::Beta: return "-beta";
        case VersionTagKind::Prerelease: return "-prerelease";
    }
    return "";
}

} // namespace

std::string ParsedVersion::toSuffixString() const {
    if (!tag) return std::string();
    std::string res = tagToSuffix(*tag);
    if (tagNumber) {
        res += "." + std::to_string(*tagNumber);
    }
    return res;
}

std::string ParsedVersion::toVString() const {
    std::string res = "v" + std::to_string(major) + "." + std::to_string(minor) +
                      "." + std::to_string(patch) + toSuffixString();
    return res;
}

ParsedVersion parseVersionString(std::string_view text) {
    ParsedVersion out{};
    std::string source(text);
    std::stringstream str(std::move(source));

    // allow leading v
    if (str.peek() == 'v') {
        str.get();
    }

    size_t major = 0;
    str >> major;
    if (str.fail()) {
        out.error = "Unable to parse major";
        return out;
    }

    if (str.get() != '.') {
        out.error = "Minor version missing";
        return out;
    }

    size_t minor = 0;
    str >> minor;
    if (str.fail()) {
        out.error = "Unable to parse minor";
        return out;
    }

    if (str.get() != '.') {
        out.error = "Patch version missing";
        return out;
    }

    size_t patch = 0;
    str >> patch;
    if (str.fail()) {
        out.error = "Unable to parse patch";
        return out;
    }

    out.major = major;
    out.minor = minor;
    out.patch = patch;

    if (str.peek() == '-') {
        str.get();
        TagParseResult tag = parseTag(str);
        if (!tag.ok) {
            out.error = tag.error.empty() ? "Unable to parse tag" : tag.error;
            return out;
        }
        out.tag = tag.kind;
        out.tagNumber = tag.number;
    }

    if (!str.eof()) {
        out.error = std::string("Expected end of version, found '") +
                    std::string(1, static_cast<char>(str.get())) + "'";
        return out;
    }

    out.valid = true;
    return out;
}

bool isValidGeodeVersionString(std::string_view text) {
    return parseVersionString(text).valid;
}

} // namespace optigd::core::geodever
