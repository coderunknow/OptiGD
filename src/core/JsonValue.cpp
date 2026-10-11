#include "JsonValue.hpp"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sstream>

namespace optigd::core::json {

const char* typeName(Type t) {
    switch (t) {
        case Type::Null: return "null";
        case Type::Bool: return "bool";
        case Type::Int: return "int";
        case Type::Double: return "double";
        case Type::String: return "string";
        case Type::Array: return "array";
        case Type::Object: return "object";
        default: return "unknown";
    }
}

namespace {

const std::string kEmptyString;
const Array kEmptyArray;
const Object kEmptyObject;

void appendUtf8(std::string& out, unsigned int codePoint) {
    if (codePoint <= 0x7F) {
        out.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else if (codePoint <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else if (codePoint <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else {
        // Not a valid scalar value: emit the replacement character rather than
        // silently dropping the data.
        appendUtf8(out, 0xFFFD);
    }
}

class Parser {
public:
    Parser(std::string_view text, const Limits& limits) : m_text(text), m_limits(limits) {}

    ParseResult run() {
        ParseResult result;
        skipWhitespace();
        if (!parseValue(result.value, 0)) {
            result.ok = false;
            result.error = m_error;
            result.errorOffset = m_errorOffset;
            return result;
        }
        skipWhitespace();
        if (m_pos != m_text.size()) {
            return fail("trailing data after the top-level value");
        }
        result.ok = true;
        return result;
    }

private:
    ParseResult fail(const std::string& message) {
        ParseResult result;
        result.ok = false;
        result.error = message;
        result.errorOffset = m_errorOffset;
        return result;
    }

    bool failAt(const char* message) {
        m_error = message;
        m_errorOffset = m_pos;
        return false;
    }

    bool failAt(const std::string& message) {
        m_error = message;
        m_errorOffset = m_pos;
        return false;
    }

    void skipWhitespace() {
        while (m_pos < m_text.size()) {
            char c = m_text[m_pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++m_pos;
            } else {
                break;
            }
        }
    }

    bool peek(char& out) const {
        if (m_pos >= m_text.size()) return false;
        out = m_text[m_pos];
        return true;
    }

    bool parseValue(Value& out, size_t depth) {
        if (depth > m_limits.maxDepth) {
            return failAt("maximum nesting depth exceeded");
        }
        char c = 0;
        if (!peek(c)) return failAt("unexpected end of input");

        switch (c) {
            case '{': return parseObject(out, depth);
            case '[': return parseArray(out, depth);
            case '"': {
                std::string s;
                if (!parseString(s)) return false;
                out = Value::string(std::move(s));
                return true;
            }
            case 't':
                if (m_text.compare(m_pos, 4, "true") == 0) {
                    m_pos += 4;
                    out = Value::boolean(true);
                    return true;
                }
                return failAt("invalid literal");
            case 'f':
                if (m_text.compare(m_pos, 5, "false") == 0) {
                    m_pos += 5;
                    out = Value::boolean(false);
                    return true;
                }
                return failAt("invalid literal");
            case 'n':
                if (m_text.compare(m_pos, 4, "null") == 0) {
                    m_pos += 4;
                    out = Value::null();
                    return true;
                }
                return failAt("invalid literal");
            default:
                return parseNumber(out);
        }
    }

    bool parseNumber(Value& out) {
        size_t start = m_pos;
        if (m_pos < m_text.size() && (m_text[m_pos] == '-' || m_text[m_pos] == '+')) ++m_pos;
        bool anyDigit = false;
        bool isFloat = false;
        while (m_pos < m_text.size()) {
            char c = m_text[m_pos];
            if (c >= '0' && c <= '9') {
                anyDigit = true;
                ++m_pos;
            } else if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
                isFloat = isFloat || (c == '.' || c == 'e' || c == 'E');
                ++m_pos;
            } else {
                break;
            }
        }
        if (!anyDigit) {
            m_pos = start;
            return failAt("expected a JSON value");
        }
        std::string raw(m_text.substr(start, m_pos - start));

        // Strictness: JSON forbids a leading '+', but a report written by a
        // hand-rolled generator or edited by a user may contain one. Accept it
        // for reading; OptiGD's own writer never produces it.
        const char* cstr = raw.c_str();
        char* end = nullptr;
        if (!isFloat) {
            errno = 0;
            long long iv = std::strtoll(cstr, &end, 10);
            if (end == cstr + raw.size() && errno != ERANGE) {
                out = Value::integer(iv);
                return true;
            }
            // Fall through to double: the integer does not fit in long long.
        }
        errno = 0;
        double dv = std::strtod(cstr, &end);
        if (end != cstr + raw.size()) {
            m_pos = start;
            return failAt("malformed number: '" + raw + "'");
        }
        out = Value::number(dv);
        return true;
    }

    bool parseString(std::string& out) {
        if (m_text[m_pos] != '"') return failAt("expected '\"'");
        ++m_pos;
        out.clear();
        while (true) {
            if (m_pos >= m_text.size()) return failAt("unterminated string");
            char c = m_text[m_pos++];
            if (c == '"') return true;
            if (c != '\\') {
                if (static_cast<unsigned char>(c) < 0x20) {
                    return failAt("unescaped control character in string");
                }
                out.push_back(c);
                if (out.size() > m_limits.maxStringLength) {
                    return failAt("string exceeds the configured length limit");
                }
                continue;
            }
            if (m_pos >= m_text.size()) return failAt("unterminated escape sequence");
            char esc = m_text[m_pos++];
            switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    unsigned int cp = 0;
                    if (!parseHex4(cp)) return false;
                    // Combine a surrogate pair when present.
                    if (cp >= 0xD800 && cp <= 0xDBFF &&
                        m_pos + 1 < m_text.size() &&
                        m_text[m_pos] == '\\' && m_text[m_pos + 1] == 'u') {
                        size_t save = m_pos;
                        m_pos += 2;
                        unsigned int low = 0;
                        if (parseHex4(low) && low >= 0xDC00 && low <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            m_pos = save; // not a valid pair; emit the lone surrogate as U+FFFD
                            cp = 0xFFFD;
                        }
                    } else if (cp >= 0xD800 && cp <= 0xDFFF) {
                        cp = 0xFFFD;
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default:
                    return failAt("invalid escape sequence");
            }
            if (out.size() > m_limits.maxStringLength) {
                return failAt("string exceeds the configured length limit");
            }
        }
    }

    bool parseHex4(unsigned int& out) {
        if (m_pos + 4 > m_text.size()) return failAt("truncated \\u escape");
        unsigned int value = 0;
        for (int i = 0; i < 4; ++i) {
            char c = m_text[m_pos++];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned>(c - 'A' + 10);
            else return failAt("invalid hex digit in \\u escape");
        }
        out = value;
        return true;
    }

    bool parseArray(Value& out, size_t depth) {
        ++m_pos; // consume '['
        Array items;
        skipWhitespace();
        char c = 0;
        if (peek(c) && c == ']') {
            ++m_pos;
            out = Value::array(std::move(items));
            return true;
        }
        while (true) {
            skipWhitespace();
            Value element;
            if (!parseValue(element, depth + 1)) return false;
            items.push_back(std::move(element));
            if (items.size() > m_limits.maxArrayElements) {
                return failAt("array exceeds the configured element limit");
            }
            skipWhitespace();
            if (!peek(c)) return failAt("unterminated array");
            if (c == ',') { ++m_pos; continue; }
            if (c == ']') {
                ++m_pos;
                out = Value::array(std::move(items));
                return true;
            }
            return failAt("expected ',' or ']' in array");
        }
    }

    bool parseObject(Value& out, size_t depth) {
        ++m_pos; // consume '{'
        Object members;
        skipWhitespace();
        char c = 0;
        if (peek(c) && c == '}') {
            ++m_pos;
            out = Value::object(std::move(members));
            return true;
        }
        while (true) {
            skipWhitespace();
            if (!peek(c) || c != '"') return failAt("expected a string key in object");
            std::string key;
            if (!parseString(key)) return false;
            skipWhitespace();
            if (!peek(c) || c != ':') return failAt("expected ':' after object key");
            ++m_pos;
            skipWhitespace();
            Value member;
            if (!parseValue(member, depth + 1)) return false;
            members.emplace_back(std::move(key), std::move(member));
            if (members.size() > m_limits.maxObjectMembers) {
                return failAt("object exceeds the configured member limit");
            }
            skipWhitespace();
            if (!peek(c)) return failAt("unterminated object");
            if (c == ',') { ++m_pos; continue; }
            if (c == '}') {
                ++m_pos;
                out = Value::object(std::move(members));
                return true;
            }
            return failAt("expected ',' or '}' in object");
        }
    }

    std::string_view m_text;
    Limits m_limits;
    size_t m_pos{0};
    std::string m_error;
    size_t m_errorOffset{0};
};

void writeIndent(std::string& out, int indent, int level) {
    if (indent <= 0) return;
    out.push_back('\n');
    out.append(static_cast<size_t>(indent) * static_cast<size_t>(level), ' ');
}

void serializeInto(const Value& value, int indent, int level, std::string& out, bool& sawNonFinite) {
    switch (value.type()) {
        case Type::Null:
            out += "null";
            break;
        case Type::Bool:
            out += (value.asBool() ? "true" : "false");
            break;
        case Type::Int:
            out += std::to_string(value.asInt());
            break;
        case Type::Double: {
            bool nonFinite = false;
            out += formatNumber(value.asDouble(), &nonFinite);
            sawNonFinite = sawNonFinite || nonFinite;
            break;
        }
        case Type::String:
            out.push_back('"');
            out += escapeString(value.asString());
            out.push_back('"');
            break;
        case Type::Array: {
            const Array& items = value.asArray();
            if (items.empty()) {
                out += "[]";
                break;
            }
            out.push_back('[');
            for (size_t i = 0; i < items.size(); ++i) {
                if (i) out.push_back(',');
                writeIndent(out, indent, level + 1);
                serializeInto(items[i], indent, level + 1, out, sawNonFinite);
            }
            writeIndent(out, indent, level);
            out.push_back(']');
            break;
        }
        case Type::Object: {
            const Object& members = value.asObject();
            if (members.empty()) {
                out += "{}";
                break;
            }
            out.push_back('{');
            for (size_t i = 0; i < members.size(); ++i) {
                if (i) out.push_back(',');
                writeIndent(out, indent, level + 1);
                out.push_back('"');
                out += escapeString(members[i].first);
                out.push_back('"');
                out.push_back(':');
                if (indent > 0) out.push_back(' ');
                serializeInto(members[i].second, indent, level + 1, out, sawNonFinite);
            }
            writeIndent(out, indent, level);
            out.push_back('}');
            break;
        }
        default:
            out += "null";
            break;
    }
}

} // namespace

Value Value::null() { return Value(); }

Value Value::boolean(bool v) {
    Value out;
    out.m_type = Type::Bool;
    out.m_bool = v;
    return out;
}

Value Value::integer(long long v) {
    Value out;
    out.m_type = Type::Int;
    out.m_int = v;
    out.m_double = static_cast<double>(v);
    return out;
}

Value Value::number(double v) {
    Value out;
    out.m_type = Type::Double;
    out.m_double = v;
    // Keep an exact integer copy when the double happens to be integral, so
    // asInt() does not lose precision on values like 3600.0.
    if (std::isfinite(v) && v == std::floor(v) &&
        v >= -9.2e18 && v <= 9.2e18) {
        out.m_int = static_cast<long long>(v);
    } else {
        out.m_int = 0;
    }
    return out;
}

Value Value::string(std::string v) {
    Value out;
    out.m_type = Type::String;
    out.m_string = std::move(v);
    return out;
}

Value Value::array(Array v) {
    Value out;
    out.m_type = Type::Array;
    out.m_array = std::move(v);
    return out;
}

Value Value::object(Object v) {
    Value out;
    out.m_type = Type::Object;
    out.m_object = std::move(v);
    return out;
}

bool Value::asBool(bool fallback) const {
    if (m_type == Type::Bool) return m_bool;
    return fallback;
}

long long Value::asInt(long long fallback) const {
    switch (m_type) {
        case Type::Int: return m_int;
        case Type::Double:
            if (std::isfinite(m_double)) return static_cast<long long>(m_double);
            return fallback;
        case Type::Bool: return m_bool ? 1 : 0;
        default: return fallback;
    }
}

unsigned long long Value::asUInt(unsigned long long fallback) const {
    long long v = asInt(-1);
    if (v < 0) return fallback;
    return static_cast<unsigned long long>(v);
}

double Value::asDouble(double fallback) const {
    switch (m_type) {
        case Type::Int: return static_cast<double>(m_int);
        // A stored double is returned EXACTLY, including NaN and infinity.
        // Returning `fallback` here looked defensive but was not: the serializer
        // asks for the value in order to decide how to write it, so substituting
        // 0.0 turned "this metric was not a number" into "this metric was zero"
        // - a plausible-looking measurement that never happened. Sanitizing is
        // the writer's job (`formatNumber` emits JSON null and sets the
        // non-finite flag); a reader that needs a finite value must check with
        // std::isfinite and decide what to do itself.
        case Type::Double: return m_double;
        case Type::Bool: return m_bool ? 1.0 : 0.0;
        default: return fallback;
    }
}

const std::string& Value::asString() const {
    return m_type == Type::String ? m_string : kEmptyString;
}

const Array& Value::asArray() const {
    return m_type == Type::Array ? m_array : kEmptyArray;
}

const Object& Value::asObject() const {
    return m_type == Type::Object ? m_object : kEmptyObject;
}

const Value* Value::find(std::string_view key) const {
    if (m_type != Type::Object) return nullptr;
    for (const auto& kv : m_object) {
        if (kv.first == key) return &kv.second;
    }
    return nullptr;
}

bool Value::getBool(std::string_view key, bool fallback) const {
    const Value* v = find(key);
    return v ? v->asBool(fallback) : fallback;
}

long long Value::getInt(std::string_view key, long long fallback) const {
    const Value* v = find(key);
    return v ? v->asInt(fallback) : fallback;
}

double Value::getDouble(std::string_view key, double fallback) const {
    const Value* v = find(key);
    return v ? v->asDouble(fallback) : fallback;
}

std::string Value::getString(std::string_view key, std::string_view fallback) const {
    const Value* v = find(key);
    if (v && v->isString()) return v->asString();
    return std::string(fallback);
}

const Value* Value::getArray(std::string_view key) const {
    const Value* v = find(key);
    return (v && v->isArray()) ? v : nullptr;
}

const Value* Value::getObject(std::string_view key) const {
    const Value* v = find(key);
    return (v && v->isObject()) ? v : nullptr;
}

void Value::set(std::string_view key, Value value) {
    if (m_type != Type::Object) {
        m_type = Type::Object;
        m_object.clear();
    }
    for (auto& kv : m_object) {
        if (kv.first == key) {
            kv.second = std::move(value);
            return;
        }
    }
    m_object.emplace_back(std::string(key), std::move(value));
}

void Value::push(Value value) {
    if (m_type != Type::Array) {
        m_type = Type::Array;
        m_array.clear();
    }
    m_array.push_back(std::move(value));
}

ParseResult parse(std::string_view text, const Limits& limits) {
    Parser parser(text, limits);
    return parser.run();
}

std::string serialize(const Value& value, int indent, bool* sawNonFinite) {
    std::string out;
    out.reserve(256);
    bool nonFinite = false;
    serializeInto(value, indent, 0, out, nonFinite);
    if (indent > 0) out.push_back('\n');
    if (sawNonFinite) *sawNonFinite = nonFinite;
    return out;
}

std::string formatNumber(double v, bool* wasNonFinite) {
    if (!std::isfinite(v)) {
        if (wasNonFinite) *wasNonFinite = true;
        return "null";
    }
    // Integral values print without a decimal point so integer-looking metrics
    // (frame counts, byte counts) stay readable.
    if (v == std::floor(v) && std::fabs(v) < 1e15) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f", v);
        return std::string(buf);
    }
    // Shortest representation that survives a strtod round trip.
    for (int precision = 1; precision <= 17; ++precision) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%.*g", precision, v);
        char* end = nullptr;
        double back = std::strtod(buf, &end);
        if (end != buf && back == v) {
            return std::string(buf);
        }
    }
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return std::string(buf);
}

std::string escapeString(std::string_view input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (char ch : input) {
        unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    static const char* kHex = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(kHex[(c >> 4) & 0xF]);
                    out.push_back(kHex[c & 0xF]);
                } else {
                    out.push_back(ch);
                }
                break;
        }
    }
    return out;
}

} // namespace optigd::core::json
