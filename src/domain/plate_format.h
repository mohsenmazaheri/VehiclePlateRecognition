#pragma once
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vpr {

// UTF-8 helpers. Plates mix ASCII digits and Persian letters, so voting works on code points.
[[nodiscard]] std::u32string decodeUtf8(std::string_view text);
[[nodiscard]] std::string encodeUtf8(std::u32string_view text);

class IPlateFormat {
public:
    virtual ~IPlateFormat() = default;
    [[nodiscard]] virtual std::string countryCode() const = 0;
    [[nodiscard]] virtual bool matches(std::u32string_view canonical) const = 0;
    [[nodiscard]] virtual std::string display(std::u32string_view canonical) const = 0;
};

// Data driven format, so a new country needs a new definition and no change to the core (OCP).
// pattern: 'D' = digit, 'L' = one of letters. displayTemplate: '#' takes the next character.
class PatternPlateFormat final : public IPlateFormat {
public:
    PatternPlateFormat(std::string countryCode, std::string pattern, std::u32string letters,
                       std::string displayTemplate);

    [[nodiscard]] std::string countryCode() const override { return m_country; }
    [[nodiscard]] bool matches(std::u32string_view canonical) const override;
    [[nodiscard]] std::string display(std::u32string_view canonical) const override;

private:
    std::string m_country;
    std::string m_pattern;
    std::u32string m_letters;
    std::string m_template;
};

struct PlateMatch {
    std::string countryCode;
    std::string canonical;
    std::string display;
};

class PlateFormatRegistry {
public:
    void add(std::shared_ptr<const IPlateFormat> format);
    [[nodiscard]] std::optional<PlateMatch> classify(std::string_view rawText) const;
    [[nodiscard]] static std::u32string normalize(std::string_view rawText);

private:
    std::vector<std::shared_ptr<const IPlateFormat>> m_formats;
};

// Standard private plates of the two target countries.
[[nodiscard]] std::shared_ptr<const IPlateFormat> armeniaPrivateFormat();
[[nodiscard]] std::shared_ptr<const IPlateFormat> armeniaPrivateThreeDigitFormat();
[[nodiscard]] std::shared_ptr<const IPlateFormat> iranPrivateFormat();
[[nodiscard]] PlateFormatRegistry defaultPlateFormats();

} // namespace vpr
