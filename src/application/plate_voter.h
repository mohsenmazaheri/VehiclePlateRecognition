#pragma once
#include "domain/plate_format.h"
#include "domain/types.h"

#include <optional>
#include <string>
#include <vector>

namespace vpr {

struct VoteResult {
    bool valid = false;         // true when the text fits a configured format
    std::string canonical;
    std::string display;
    std::string countryCode;
    float confidence = 0.0F;    // share of the vote weight that supports the winning characters
};

// Combines the readings of one vehicle across frames. One blurred frame must not decide the plate.
class PlateVoter {
public:
    explicit PlateVoter(const PlateFormatRegistry* formats);

    void add(const PlateReading& reading);
    [[nodiscard]] bool hasReadings() const { return !m_readings.empty(); }
    [[nodiscard]] std::optional<VoteResult> result() const;

private:
    struct Entry {
        std::u32string text;
        float weight = 0.0F;
    };

    const PlateFormatRegistry* m_formats;
    std::vector<Entry> m_readings;
};

} // namespace vpr
