#pragma once

// Small strict JSON reader plus string-quoting helper. JUCE-free; std only.
//
// Why not juce::JSON: the preset contract requires rejection of duplicate object keys, exact control
// over number text (so floats are parsed with correct rounding and range-checked as floats), UTF-8
// validation, depth/size limits and byte-identical deterministic output. This reader is intentionally
// strict: RFC 8259 grammar only (no comments, no trailing commas, no NaN/Infinity, no leading zeros).

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace snaral::json
{
enum class Type { Null, Bool, Number, String, Array, Object };

struct Member;

struct Value
{
    Type type = Type::Null;
    bool boolean = false;
    std::string text;            // Number: the literal token; String: unescaped UTF-8 content
    std::vector<Value> items;    // Array elements
    std::vector<Member> members; // Object members in source order (keys are unique)

    bool isNull() const noexcept { return type == Type::Null; }
    bool isBool() const noexcept { return type == Type::Bool; }
    bool isNumber() const noexcept { return type == Type::Number; }
    bool isString() const noexcept { return type == Type::String; }
    bool isArray() const noexcept { return type == Type::Array; }
    bool isObject() const noexcept { return type == Type::Object; }

    /// Object member lookup; nullptr if not an object or key absent.
    const Value* find (std::string_view key) const noexcept;
    Value* find (std::string_view key) noexcept;
};

struct Member
{
    std::string key;
    Value value;
};

struct ParseError
{
    std::string message;
    std::size_t offset = 0;
    int line = 1;
    int column = 1;

    std::string describe() const;
};

struct ParseOptions
{
    std::size_t maxBytes = 1024 * 1024; // hard cap checked before parsing
    int maxDepth = 16;                  // nesting of arrays/objects
};

/// Parses a complete document (one value, only whitespace around it). One leading UTF-8 BOM is skipped.
/// On failure `out` is left in an unspecified state and `error` describes the first problem.
bool parse (std::string_view text, Value& out, ParseError& error, const ParseOptions& options = {});

/// Returns `utf8` as a quoted JSON string literal (escapes ", \, and control characters; everything else,
/// including non-ASCII UTF-8, is written verbatim). The input must be valid UTF-8.
std::string quote (std::string_view utf8);

/// True if the bytes are well-formed UTF-8 (no overlongs, no surrogates, max U+10FFFF).
bool isValidUtf8 (std::string_view bytes) noexcept;

/// Number of Unicode code points in valid UTF-8 text.
std::size_t codePointCount (std::string_view utf8) noexcept;

/// Returns the longest prefix of `utf8` that has at most `maxCodePoints` code points.
std::string_view truncateCodePoints (std::string_view utf8, std::size_t maxCodePoints) noexcept;
} // namespace snaral::json
