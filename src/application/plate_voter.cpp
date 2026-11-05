#include "plate_voter.h"

#include <algorithm>
#include <map>
#include <numeric>

namespace vpr {

namespace {

constexpr std::size_t kMisreadDistance = 2;

std::size_t editDistance(const std::u32string& a, const std::u32string& b)
{
    std::vector<std::size_t> row(b.size() + 1);
    std::iota(row.begin(), row.end(), std::size_t{0});
    for (std::size_t i = 1; i <= a.size(); ++i) {
        std::size_t diagonal = row[0];
        row[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t above = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diagonal = above;
        }
    }
    return row[b.size()];
}

} // namespace

PlateVoter::PlateVoter(const PlateFormatRegistry* formats) : m_formats(formats) {}

void PlateVoter::add(const PlateReading& reading)
{
    std::u32string text = PlateFormatRegistry::normalize(reading.text);
    if (!text.empty())
        m_readings.push_back({std::move(text), reading.confidence > 0.0F ? reading.confidence : 0.01F});
}

std::optional<VoteResult> PlateVoter::result() const
{
    if (m_readings.empty())
        return std::nullopt;

    // Readings that fit a format are trusted first. Others only count when nothing fits.
    std::vector<Entry> pool;
    float validWeight = 0.0F;
    float totalWeight = 0.0F;
    for (const Entry& e : m_readings) {
        totalWeight += e.weight;
        if (m_formats->classify(encodeUtf8(e.text))) {
            pool.push_back(e);
            validWeight += e.weight;
        }
    }

    // That trust only holds while the other readings look like misreads of the valid ones. When most
    // of the weight is on a clearly different text, such as a bus plate that fits no format, a few
    // valid readings probably came from a neighbouring vehicle and must not win.
    if (!pool.empty() && validWeight < totalWeight / 2.0F) {
        const Entry& strongest = *std::max_element(pool.begin(), pool.end(), [](const Entry& a, const Entry& b) {
            return a.weight < b.weight;
        });
        float related = validWeight;
        for (const Entry& e : m_readings) {
            if (!m_formats->classify(encodeUtf8(e.text)) && editDistance(e.text, strongest.text) <= kMisreadDistance)
                related += e.weight;
        }
        if (related < totalWeight / 2.0F)
            pool.clear();
    }

    const bool anyValid = !pool.empty();
    if (!anyValid)
        pool = m_readings;

    std::map<std::size_t, float> lengthWeight;
    for (const Entry& e : pool)
        lengthWeight[e.text.size()] += e.weight;
    std::size_t length = 0;
    float bestLength = -1.0F;
    for (const auto& [len, weight] : lengthWeight) {
        if (weight > bestLength) {
            bestLength = weight;
            length = len;
        }
    }

    std::u32string voted(length, U'?');
    float supported = 0.0F;
    float total = 0.0F;
    for (std::size_t i = 0; i < length; ++i) {
        std::map<char32_t, float> votes;
        float positionTotal = 0.0F;
        for (const Entry& e : pool) {
            if (e.text.size() == length) {
                votes[e.text[i]] += e.weight;
                positionTotal += e.weight;
            }
        }
        float bestVote = -1.0F;
        for (const auto& [c, weight] : votes) {
            if (weight > bestVote) {
                bestVote = weight;
                voted[i] = c;
            }
        }
        supported += bestVote;
        total += positionTotal;
    }

    VoteResult out;
    out.confidence = total > 0.0F ? supported / total : 0.0F;
    const auto fill = [&out](const PlateMatch& match) {
        out.valid = true;
        out.canonical = match.canonical;
        out.display = match.display;
        out.countryCode = match.countryCode;
    };

    if (const auto match = m_formats->classify(encodeUtf8(voted))) {
        fill(*match);
        return out;
    }

    // Character voting can mix two formats. Fall back to the strongest single valid reading.
    if (anyValid) {
        const Entry* best = nullptr;
        for (const Entry& e : pool) {
            if (!best || e.weight > best->weight)
                best = &e;
        }
        if (const auto match = m_formats->classify(encodeUtf8(best->text))) {
            fill(*match);
            return out;
        }
    }

    out.canonical = encodeUtf8(voted);
    out.display = out.canonical;
    return out;
}

} // namespace vpr
