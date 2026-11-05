#include "plate_format.h"

#include <utility>

namespace vpr {

std::u32string decodeUtf8(std::string_view text)
{
    std::u32string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        int extra = 0;
        char32_t cp = 0xFFFD;
        if (lead < 0x80) { cp = lead; }
        else if ((lead >> 5) == 0x6) { cp = lead & 0x1F; extra = 1; }
        else if ((lead >> 4) == 0xE) { cp = lead & 0x0F; extra = 2; }
        else if ((lead >> 3) == 0x1E) { cp = lead & 0x07; extra = 3; }
        ++i;
        for (int k = 0; k < extra && i < text.size(); ++k, ++i)
            cp = (cp << 6) | (static_cast<unsigned char>(text[i]) & 0x3F);
        out.push_back(cp);
    }
    return out;
}

std::string encodeUtf8(std::u32string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char32_t cp : text) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

PatternPlateFormat::PatternPlateFormat(std::string countryCode, std::string pattern,
                                       std::u32string letters, std::string displayTemplate)
    : m_country(std::move(countryCode))
    , m_pattern(std::move(pattern))
    , m_letters(std::move(letters))
    , m_template(std::move(displayTemplate))
{
}

bool PatternPlateFormat::matches(std::u32string_view canonical) const
{
    if (canonical.size() != m_pattern.size())
        return false;
    for (std::size_t i = 0; i < canonical.size(); ++i) {
        const char32_t c = canonical[i];
        if (m_pattern[i] == 'D') {
            if (c < U'0' || c > U'9')
                return false;
        } else if (m_letters.find(c) == std::u32string::npos) {
            return false;
        }
    }
    return true;
}

std::string PatternPlateFormat::display(std::u32string_view canonical) const
{
    std::u32string out;
    std::size_t next = 0;
    for (const char t : m_template) {
        if (t == '#') {
            if (next < canonical.size()) {
                const char32_t c = canonical[next++];
                // Keep a right to left letter from reordering the digits around it.
                const bool wrap = c > 0x7F;
                if (wrap)
                    out.push_back(U'‎');
                out.push_back(c);
                if (wrap)
                    out.push_back(U'‎');
            }
        } else {
            out.push_back(static_cast<char32_t>(static_cast<unsigned char>(t)));
        }
    }
    return encodeUtf8(out);
}

void PlateFormatRegistry::add(std::shared_ptr<const IPlateFormat> format)
{
    m_formats.push_back(std::move(format));
}

std::u32string PlateFormatRegistry::normalize(std::string_view rawText)
{
    std::u32string out;
    for (char32_t c : decodeUtf8(rawText)) {
        if (c == U' ' || c == U'-' || c == U'_' || c == U'\t')
            continue;
        if (c >= U'۰' && c <= U'۹')       // Persian digits
            c = U'0' + (c - U'۰');
        else if (c >= U'٠' && c <= U'٩')  // Arabic digits
            c = U'0' + (c - U'٠');
        else if (c >= U'a' && c <= U'z')
            c = c - U'a' + U'A';
        else if (c == U'ي')                    // Arabic yeh to Persian yeh
            c = U'ی';
        out.push_back(c);
    }
    return out;
}

std::optional<PlateMatch> PlateFormatRegistry::classify(std::string_view rawText) const
{
    const std::u32string canonical = normalize(rawText);
    for (const auto& format : m_formats) {
        if (format->matches(canonical))
            return PlateMatch{format->countryCode(), encodeUtf8(canonical), format->display(canonical)};
    }
    return std::nullopt;
}

std::shared_ptr<const IPlateFormat> armeniaPrivateFormat()
{
    // Private series: 2 digits, 2 Latin letters, 3 digits, for example 12 AB 345.
    // The 3 digit, 2 letter, 2 digit series is a separate format, see armeniaPrivateThreeDigitFormat.
    return std::make_shared<PatternPlateFormat>(
        "AM", "DDLLDDD", U"ABCDEFGHIJKLMNOPQRSTUVWXYZ", "## ## ###");
}

std::shared_ptr<const IPlateFormat> armeniaPrivateThreeDigitFormat()
{
    // Private series: 3 digits, 2 Latin letters, 2 digits, for example 123 AB 45. Common on Yerevan
    // footage, confirmed as private by the project owner on 5 November 2026.
    return std::make_shared<PatternPlateFormat>(
        "AM", "DDDLLDD", U"ABCDEFGHIJKLMNOPQRSTUVWXYZ", "### ## ##");
}

std::shared_ptr<const IPlateFormat> iranPrivateFormat()
{
    // Private series: 2 digits, 1 Persian letter, 3 digits, 2 digit region code.
    // Letter list to be confirmed against labelled data, see the data and evaluation plan.
    return std::make_shared<PatternPlateFormat>(
        "IR", "DDLDDDDD",
        U"بجدسصطقلمنوهی",
        "## # ### ##");
}

PlateFormatRegistry defaultPlateFormats()
{
    PlateFormatRegistry registry;
    registry.add(armeniaPrivateFormat());
    registry.add(armeniaPrivateThreeDigitFormat());
    registry.add(iranPrivateFormat());
    return registry;
}

} // namespace vpr
