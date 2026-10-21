#include "NumberText.h"

#include <cerrno>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace snaral::text
{
namespace
{
std::string localeDecimalPoint()
{
    const auto* lc = std::localeconv();
    if (lc != nullptr && lc->decimal_point != nullptr && lc->decimal_point[0] != '\0')
        return lc->decimal_point;
    return ".";
}

std::string toLocaleText (std::string_view token)
{
    std::string s (token);
    const auto dp = localeDecimalPoint();
    if (dp != ".")
    {
        const auto pos = s.find ('.');
        if (pos != std::string::npos)
            s.replace (pos, 1, dp);
    }
    return s;
}

bool validToken (std::string_view t) noexcept
{
    if (t.empty())
        return false;
    for (const char c : t)
        if (! ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E'))
            return false;
    return true;
}
} // namespace

std::string formatFloat (float value)
{
    if (! std::isfinite (value) || value == 0.0f)
        return "0";

    const bool negative = value < 0.0f;
    const float a = std::fabs (value);

    char buf[48] = {};
    for (int precision = 1; precision <= 9; ++precision)
    {
        std::snprintf (buf, sizeof (buf), "%.*e", precision - 1, static_cast<double> (a));
        if (std::strtof (buf, nullptr) == a)
            break;
    }

    // buf looks like "d<dp>ddde+XX" in the current locale.
    std::string digits;
    const char* p = buf;
    while (*p != '\0' && *p != 'e' && *p != 'E')
    {
        if (*p >= '0' && *p <= '9')
            digits.push_back (*p);
        ++p;
    }
    const int exponent = (*p == 'e' || *p == 'E') ? std::atoi (p + 1) : 0;
    while (digits.size() > 1 && digits.back() == '0')
        digits.pop_back();

    const int n = static_cast<int> (digits.size());
    std::string integerPart, fractionPart;
    if (exponent >= 0)
    {
        if (n <= exponent + 1)
        {
            integerPart = digits + std::string (static_cast<std::size_t> (exponent + 1 - n), '0');
        }
        else
        {
            integerPart = digits.substr (0, static_cast<std::size_t> (exponent + 1));
            fractionPart = digits.substr (static_cast<std::size_t> (exponent + 1));
        }
    }
    else
    {
        integerPart = "0";
        fractionPart = std::string (static_cast<std::size_t> (-exponent - 1), '0') + digits;
    }

    std::string out;
    if (negative)
        out.push_back ('-');
    out += integerPart;
    if (! fractionPart.empty())
    {
        out.push_back ('.');
        out += fractionPart;
    }
    return out;
}

bool parseFloat (std::string_view token, float& out) noexcept
{
    if (! validToken (token))
        return false;
    try
    {
        const auto s = toLocaleText (token);
        char* end = nullptr;
        const float v = std::strtof (s.c_str(), &end);
        if (end == nullptr || end == s.c_str() || *end != '\0' || ! std::isfinite (v))
            return false;
        out = v;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool parseDouble (std::string_view token, double& out) noexcept
{
    if (! validToken (token))
        return false;
    try
    {
        const auto s = toLocaleText (token);
        char* end = nullptr;
        const double v = std::strtod (s.c_str(), &end);
        if (end == nullptr || end == s.c_str() || *end != '\0' || ! std::isfinite (v))
            return false;
        out = v;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool parseInt (std::string_view token, long long& out) noexcept
{
    if (token.empty() || token.size() > 18)
        return false;
    std::size_t i = 0;
    bool negative = false;
    if (token[0] == '-')
    {
        negative = true;
        i = 1;
    }
    if (i >= token.size())
        return false;
    long long v = 0;
    for (; i < token.size(); ++i)
    {
        if (token[i] < '0' || token[i] > '9')
            return false;
        v = v * 10 + (token[i] - '0');
    }
    out = negative ? -v : v;
    return true;
}
} // namespace snaral::text
