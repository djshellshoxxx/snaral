#include "Json.h"

#include <cstdint>
#include <new>
#include <set>

namespace snaral::json
{
const Value* Value::find (std::string_view key) const noexcept
{
    if (type != Type::Object)
        return nullptr;
    for (const auto& m : members)
        if (m.key == key)
            return &m.value;
    return nullptr;
}

Value* Value::find (std::string_view key) noexcept
{
    if (type != Type::Object)
        return nullptr;
    for (auto& m : members)
        if (m.key == key)
            return &m.value;
    return nullptr;
}

std::string ParseError::describe() const
{
    return message + " (line " + std::to_string (line) + ", column " + std::to_string (column) + ")";
}

bool isValidUtf8 (std::string_view s) noexcept
{
    std::size_t i = 0;
    const auto n = s.size();
    while (i < n)
    {
        const auto c = static_cast<unsigned char> (s[i]);
        if (c < 0x80)
        {
            ++i;
            continue;
        }
        int extra = 0;
        std::uint32_t cp = 0;
        std::uint32_t minimum = 0;
        if ((c & 0xE0) == 0xC0)      { extra = 1; cp = c & 0x1Fu; minimum = 0x80; }
        else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0Fu; minimum = 0x800; }
        else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07u; minimum = 0x10000; }
        else return false;
        if (i + static_cast<std::size_t> (extra) >= n)
            return false;
        for (int k = 1; k <= extra; ++k)
        {
            const auto cc = static_cast<unsigned char> (s[i + static_cast<std::size_t> (k)]);
            if ((cc & 0xC0) != 0x80)
                return false;
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            return false;
        i += static_cast<std::size_t> (extra) + 1;
    }
    return true;
}

std::size_t codePointCount (std::string_view s) noexcept
{
    std::size_t count = 0;
    for (const char ch : s)
        if ((static_cast<unsigned char> (ch) & 0xC0) != 0x80)
            ++count;
    return count;
}

std::string_view truncateCodePoints (std::string_view s, std::size_t maxCodePoints) noexcept
{
    std::size_t count = 0;
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        if ((static_cast<unsigned char> (s[i]) & 0xC0) != 0x80)
        {
            if (count == maxCodePoints)
                return s.substr (0, i);
            ++count;
        }
    }
    return s;
}

std::string quote (std::string_view s)
{
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve (s.size() + 2);
    out.push_back ('"');
    for (const char ch : s)
    {
        const auto c = static_cast<unsigned char> (ch);
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20 || c == 0x7F)
                {
                    out += "\\u00";
                    out.push_back (hex[c >> 4]);
                    out.push_back (hex[c & 15]);
                }
                else
                {
                    out.push_back (ch);
                }
        }
    }
    out.push_back ('"');
    return out;
}

namespace
{
class Parser
{
public:
    Parser (std::string_view t, const ParseOptions& o) : text (t), options (o) {}

    bool run (Value& out, ParseError& err)
    {
        error = &err;
        if (text.size() >= 3 && static_cast<unsigned char> (text[0]) == 0xEF
            && static_cast<unsigned char> (text[1]) == 0xBB && static_cast<unsigned char> (text[2]) == 0xBF)
            pos = 3;
        skipWs();
        if (! parseValue (out, 0))
            return false;
        skipWs();
        if (pos != text.size())
            return fail ("unexpected content after the JSON document");
        return true;
    }

private:
    std::string_view text;
    ParseOptions options;
    std::size_t pos = 0;
    ParseError* error = nullptr;

    bool fail (const std::string& message)
    {
        error->message = message;
        error->offset = pos;
        int line = 1, column = 1;
        for (std::size_t i = 0; i < pos && i < text.size(); ++i)
        {
            if (text[i] == '\n')
            {
                ++line;
                column = 1;
            }
            else if ((static_cast<unsigned char> (text[i]) & 0xC0) != 0x80)
            {
                ++column;
            }
        }
        error->line = line;
        error->column = column;
        return false;
    }

    void skipWs() noexcept
    {
        while (pos < text.size()
               && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' || text[pos] == '\r'))
            ++pos;
    }

    bool parseValue (Value& out, int depth)
    {
        if (pos >= text.size())
            return fail ("unexpected end of input (truncated document)");
        const char c = text[pos];
        switch (c)
        {
            case '{': return parseObject (out, depth);
            case '[': return parseArray (out, depth);
            case '"':
                out.type = Type::String;
                return parseString (out.text);
            case 't': return parseLiteral ("true", out, Type::Bool, true);
            case 'f': return parseLiteral ("false", out, Type::Bool, false);
            case 'n': return parseLiteral ("null", out, Type::Null, false);
            default:
                if (c == '-' || (c >= '0' && c <= '9'))
                    return parseNumber (out);
                return fail ("unexpected character");
        }
    }

    bool parseLiteral (const char* word, Value& out, Type type, bool b)
    {
        const std::string_view w (word);
        if (text.substr (pos, w.size()) != w)
            return fail ("invalid literal");
        pos += w.size();
        out.type = type;
        out.boolean = b;
        return true;
    }

    bool parseNumber (Value& out)
    {
        const auto start = pos;
        if (text[pos] == '-')
            ++pos;
        if (pos >= text.size())
            return fail ("incomplete number");
        if (text[pos] == '0')
        {
            ++pos;
            if (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
                return fail ("leading zeros are not allowed in numbers");
        }
        else if (text[pos] >= '1' && text[pos] <= '9')
        {
            while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
                ++pos;
        }
        else
        {
            return fail ("invalid number");
        }
        if (pos < text.size() && text[pos] == '.')
        {
            ++pos;
            if (pos >= text.size() || text[pos] < '0' || text[pos] > '9')
                return fail ("digits expected after decimal point");
            while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
                ++pos;
        }
        if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E'))
        {
            ++pos;
            if (pos < text.size() && (text[pos] == '+' || text[pos] == '-'))
                ++pos;
            if (pos >= text.size() || text[pos] < '0' || text[pos] > '9')
                return fail ("digits expected in exponent");
            while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
                ++pos;
        }
        out.type = Type::Number;
        out.text.assign (text.substr (start, pos - start));
        return true;
    }

    static int hexValue (char c) noexcept
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    bool parseHex4 (std::uint32_t& out)
    {
        if (pos + 4 > text.size())
            return fail ("truncated \\u escape");
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
        {
            const int h = hexValue (text[pos + static_cast<std::size_t> (i)]);
            if (h < 0)
                return fail ("invalid \\u escape");
            v = (v << 4) | static_cast<std::uint32_t> (h);
        }
        pos += 4;
        out = v;
        return true;
    }

    static void appendUtf8 (std::string& s, std::uint32_t cp)
    {
        if (cp < 0x80)
        {
            s.push_back (static_cast<char> (cp));
        }
        else if (cp < 0x800)
        {
            s.push_back (static_cast<char> (0xC0 | (cp >> 6)));
            s.push_back (static_cast<char> (0x80 | (cp & 0x3F)));
        }
        else if (cp < 0x10000)
        {
            s.push_back (static_cast<char> (0xE0 | (cp >> 12)));
            s.push_back (static_cast<char> (0x80 | ((cp >> 6) & 0x3F)));
            s.push_back (static_cast<char> (0x80 | (cp & 0x3F)));
        }
        else
        {
            s.push_back (static_cast<char> (0xF0 | (cp >> 18)));
            s.push_back (static_cast<char> (0x80 | ((cp >> 12) & 0x3F)));
            s.push_back (static_cast<char> (0x80 | ((cp >> 6) & 0x3F)));
            s.push_back (static_cast<char> (0x80 | (cp & 0x3F)));
        }
    }

    bool parseString (std::string& out)
    {
        ++pos; // opening quote
        out.clear();
        const auto rawStart = pos;
        bool hasHighBytes = false;
        while (true)
        {
            if (pos >= text.size())
                return fail ("unterminated string (truncated document)");
            const auto c = static_cast<unsigned char> (text[pos]);
            if (c == '"')
            {
                ++pos;
                break;
            }
            if (c < 0x20)
                return fail ("unescaped control character in string");
            if (c >= 0x80)
                hasHighBytes = true;
            if (c == '\\')
            {
                ++pos;
                if (pos >= text.size())
                    return fail ("unterminated escape (truncated document)");
                const char e = text[pos++];
                switch (e)
                {
                    case '"':  out.push_back ('"'); break;
                    case '\\': out.push_back ('\\'); break;
                    case '/':  out.push_back ('/'); break;
                    case 'b':  out.push_back ('\b'); break;
                    case 'f':  out.push_back ('\f'); break;
                    case 'n':  out.push_back ('\n'); break;
                    case 'r':  out.push_back ('\r'); break;
                    case 't':  out.push_back ('\t'); break;
                    case 'u':
                    {
                        std::uint32_t cp = 0;
                        if (! parseHex4 (cp))
                            return false;
                        if (cp >= 0xD800 && cp <= 0xDBFF)
                        {
                            if (pos + 2 > text.size() || text[pos] != '\\' || text[pos + 1] != 'u')
                                return fail ("unpaired surrogate in \\u escape");
                            pos += 2;
                            std::uint32_t lo = 0;
                            if (! parseHex4 (lo))
                                return false;
                            if (lo < 0xDC00 || lo > 0xDFFF)
                                return fail ("invalid low surrogate in \\u escape");
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        else if (cp >= 0xDC00 && cp <= 0xDFFF)
                        {
                            return fail ("unpaired surrogate in \\u escape");
                        }
                        appendUtf8 (out, cp);
                        break;
                    }
                    default:
                        --pos;
                        return fail ("invalid escape sequence");
                }
                continue;
            }
            out.push_back (static_cast<char> (c));
            ++pos;
        }
        if (hasHighBytes && ! isValidUtf8 (out))
        {
            pos = rawStart;
            return fail ("string is not valid UTF-8");
        }
        return true;
    }

    bool parseArray (Value& out, int depth)
    {
        if (depth + 1 > options.maxDepth)
            return fail ("nesting too deep");
        ++pos;
        out.type = Type::Array;
        skipWs();
        if (pos < text.size() && text[pos] == ']')
        {
            ++pos;
            return true;
        }
        while (true)
        {
            skipWs();
            Value element;
            if (! parseValue (element, depth + 1))
                return false;
            out.items.push_back (std::move (element));
            skipWs();
            if (pos >= text.size())
                return fail ("unexpected end of input in array (truncated document)");
            if (text[pos] == ',')
            {
                ++pos;
                continue;
            }
            if (text[pos] == ']')
            {
                ++pos;
                return true;
            }
            return fail ("expected ',' or ']' in array");
        }
    }

    bool parseObject (Value& out, int depth)
    {
        if (depth + 1 > options.maxDepth)
            return fail ("nesting too deep");
        ++pos;
        out.type = Type::Object;
        std::set<std::string> seen;
        skipWs();
        if (pos < text.size() && text[pos] == '}')
        {
            ++pos;
            return true;
        }
        while (true)
        {
            skipWs();
            if (pos >= text.size())
                return fail ("unexpected end of input in object (truncated document)");
            if (text[pos] != '"')
                return fail ("object key must be a string");
            Member m;
            const auto keyPos = pos;
            if (! parseString (m.key))
                return false;
            if (! seen.insert (m.key).second)
            {
                pos = keyPos;
                return fail ("duplicate object key \"" + m.key + "\"");
            }
            skipWs();
            if (pos >= text.size())
                return fail ("unexpected end of input (truncated document)");
            if (text[pos] != ':')
                return fail ("expected ':' after object key");
            ++pos;
            skipWs();
            if (! parseValue (m.value, depth + 1))
                return false;
            out.members.push_back (std::move (m));
            skipWs();
            if (pos >= text.size())
                return fail ("unexpected end of input in object (truncated document)");
            if (text[pos] == ',')
            {
                ++pos;
                continue;
            }
            if (text[pos] == '}')
            {
                ++pos;
                return true;
            }
            return fail ("expected ',' or '}' in object");
        }
    }
};
} // namespace

bool parse (std::string_view text, Value& out, ParseError& error, const ParseOptions& options)
{
    out = Value {};
    error = ParseError {};
    if (text.size() > options.maxBytes)
    {
        error.message = "document exceeds the maximum size of " + std::to_string (options.maxBytes) + " bytes";
        return false;
    }
    try
    {
        Parser parser (text, options);
        return parser.run (out, error);
    }
    catch (const std::bad_alloc&)
    {
        error.message = "out of memory while parsing";
        return false;
    }
}
} // namespace snaral::json
