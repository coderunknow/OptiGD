#pragma once

// Minimal, dependency-free JSON reader/writer for OptiGD.
//
// Why this exists
// ---------------
// `DiagnosticsEngine::generateJsonReport()` hand-concatenates JSON and there was
// no way to *read* a report back inside the mod. Benchmark Mode has to reopen and
// compare previously saved sessions without asking the user to run Python, so a
// small parser is required. Pulling in a third-party JSON library is not an
// option for a Geode mod that must build for Win64/Android32/Android64 from a
// pinned SDK, so this is a deliberately tiny, allocation-light implementation
// covering exactly the subset OptiGD emits: objects, arrays, strings, numbers,
// `true`/`false`/`null`.
//
// Guarantees this file makes:
//   * Object key order is preserved on parse and on serialize, so a
//     read-modify-write round trip produces a stable diff.
//   * Integers round-trip exactly (`long long`); other numbers round-trip
//     through the shortest `%g` representation that survives `strtod`.
//   * Non-finite doubles are refused at write time (JSON has no NaN/Inf
//     literal); `serialize()` emits `null` and `hadNonFiniteNumber()` reports it
//     so a caller can flag the report instead of emitting unparseable JSON.
//   * `parse()` is total: it never throws and never recurses without bound.
//     Depth, string length and container size are capped, and every failure
//     yields a message with a byte offset.
//
// It is NOT a general-purpose JSON library: it does not preserve number
// formatting, does not support comments or trailing commas, and does not
// implement big integers.

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace optigd::core::json {

class Value;

using Array = std::vector<Value>;
// Ordered map: a vector of pairs keeps insertion order and is cheaper than
// std::map for the handful of keys OptiGD emits per object.
using Object = std::vector<std::pair<std::string, Value>>;

enum class Type : uint8_t {
    Null = 0,
    Bool,
    Int,
    Double,
    String,
    Array,
    Object,
};

const char* typeName(Type t);

// Hard limits. These exist so a truncated or hostile session file cannot make
// the parser recurse or allocate without bound inside the game process.
struct Limits {
    size_t maxDepth{64};
    size_t maxStringLength{1u << 20};   // 1 MiB per string
    size_t maxArrayElements{1u << 16};  // 65536 entries
    size_t maxObjectMembers{1u << 16};
};

class Value {
public:
    Value() = default;

    static Value null();
    static Value boolean(bool v);
    static Value integer(long long v);
    static Value number(double v);
    static Value string(std::string v);
    static Value array(Array v = {});
    static Value object(Object v = {});

    Type type() const { return m_type; }
    bool isNull() const { return m_type == Type::Null; }
    bool isBool() const { return m_type == Type::Bool; }
    bool isInt() const { return m_type == Type::Int; }
    bool isDouble() const { return m_type == Type::Double; }
    bool isNumber() const { return m_type == Type::Int || m_type == Type::Double; }
    bool isString() const { return m_type == Type::String; }
    bool isArray() const { return m_type == Type::Array; }
    bool isObject() const { return m_type == Type::Object; }

    bool asBool(bool fallback = false) const;
    long long asInt(long long fallback = 0) const;
    unsigned long long asUInt(unsigned long long fallback = 0) const;
    double asDouble(double fallback = 0.0) const;
    const std::string& asString() const;
    const Array& asArray() const;
    const Object& asObject() const;

    Array& arrayRef() { return m_array; }
    Object& objectRef() { return m_object; }

    // Object access. Missing keys / wrong types return the fallback instead of
    // throwing, because a session file may have been written by an older build.
    const Value* find(std::string_view key) const;
    bool getBool(std::string_view key, bool fallback) const;
    long long getInt(std::string_view key, long long fallback) const;
    double getDouble(std::string_view key, double fallback) const;
    std::string getString(std::string_view key, std::string_view fallback = {}) const;
    const Value* getArray(std::string_view key) const;
    const Value* getObject(std::string_view key) const;

    // Object mutation (append-or-replace, preserves first-insertion position).
    void set(std::string_view key, Value value);

    void push(Value value);

private:
    Type m_type{Type::Null};
    bool m_bool{false};
    long long m_int{0};
    double m_double{0.0};
    std::string m_string;
    Array m_array;
    Object m_object;
};

struct ParseResult {
    Value value;
    bool ok{false};
    std::string error;   // empty on success
    size_t errorOffset{0};
};

// Parses `text`. On failure `ok == false` and `error` describes the problem
// with a byte offset. Never throws.
ParseResult parse(std::string_view text, const Limits& limits = Limits{});

// Serializes `value`. `indent < 0` produces compact output.
// `sawNonFinite` (optional) is set when a NaN/Inf double was replaced by `null`.
std::string serialize(const Value& value, int indent = 2, bool* sawNonFinite = nullptr);

// Shortest representation that round-trips through strtod. Integral values are
// written without a decimal point; non-finite values write as "null".
std::string formatNumber(double v, bool* wasNonFinite = nullptr);

// Escapes `input` as a JSON string body (without the surrounding quotes).
std::string escapeString(std::string_view input);

} // namespace optigd::core::json
