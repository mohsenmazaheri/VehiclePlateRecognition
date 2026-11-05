#include "plate_core.h"

#include "domain/plate_format.h"
#include "resize.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <stdexcept>

namespace vpr::plate {

namespace {

constexpr int kPadding = 114;
constexpr std::size_t kColumns = 7;

std::string_view trimmed(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos)
        return {};
    const auto last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

std::string_view unquoted(std::string_view text)
{
    if (text.size() >= 2 && (text.front() == '\'' || text.front() == '"') && text.back() == text.front())
        return text.substr(1, text.size() - 2);
    return text;
}

} // namespace

Letterbox prepareDetectorInput(const Image& image, int inputWidth, int inputHeight, std::vector<float>& tensor)
{
    if (image.empty())
        throw std::invalid_argument("prepareDetectorInput: empty image");

    const double scale = std::min(static_cast<double>(inputWidth) / image.width,
                                  static_cast<double>(inputHeight) / image.height);
    const int width = std::clamp(static_cast<int>(std::lround(image.width * scale)), 1, inputWidth);
    const int height = std::clamp(static_cast<int>(std::lround(image.height * scale)), 1, inputHeight);
    const int padX = (inputWidth - width) / 2;
    const int padY = (inputHeight - height) / 2;

    std::vector<std::uint8_t> resized(static_cast<std::size_t>(width) * height * 3);
    onnx::resizeRgb(image, width, height, resized.data());

    const auto planeSize = static_cast<std::size_t>(inputWidth) * inputHeight;
    tensor.assign(planeSize * 3, kPadding / 255.0F);
    for (int y = 0; y < height; ++y) {
        const std::size_t target = static_cast<std::size_t>(y + padY) * inputWidth + padX;
        const std::uint8_t* source = resized.data() + static_cast<std::size_t>(y) * width * 3;
        for (int x = 0; x < width; ++x, source += 3) {
            for (std::size_t c = 0; c < 3; ++c)
                tensor[c * planeSize + target + static_cast<std::size_t>(x)] = source[c] / 255.0F;
        }
    }
    return {scale, padX, padY};
}

std::vector<Detection> decodeDetections(const float* rows, std::size_t count, std::size_t columns,
                                        const Letterbox& letterbox, int imageWidth, int imageHeight,
                                        float scoreThreshold)
{
    if (count > 0 && columns != kColumns)
        throw std::runtime_error("Plate detector output must have 7 columns");

    const auto toImage = [&letterbox](float value, int pad, int limit) {
        return std::clamp((value - pad) / letterbox.scale, 0.0, static_cast<double>(limit));
    };

    std::vector<Detection> detections;
    for (std::size_t i = 0; i < count; ++i) {
        const float* row = rows + i * kColumns;
        const float score = row[6];
        if (score < scoreThreshold)
            continue;
        const double left = toImage(row[1], letterbox.padX, imageWidth);
        const double top = toImage(row[2], letterbox.padY, imageHeight);
        const double right = toImage(row[3], letterbox.padX, imageWidth);
        const double bottom = toImage(row[4], letterbox.padY, imageHeight);
        if (right > left && bottom > top)
            detections.push_back({RectF{left, top, right - left, bottom - top}, score});
    }
    return detections;
}

ReaderConfig parseReaderConfig(std::string_view yaml)
{
    std::map<std::string, std::string, std::less<>> values;
    while (!yaml.empty()) {
        const auto end = yaml.find('\n');
        const std::string_view line = yaml.substr(0, end);
        yaml = end == std::string_view::npos ? std::string_view{} : yaml.substr(end + 1);
        const auto colon = line.find(':');
        if (colon == std::string_view::npos || line.empty() || line.front() == '#' || line.front() == ' ')
            continue;
        values.emplace(std::string(trimmed(line.substr(0, colon))),
                       std::string(unquoted(trimmed(line.substr(colon + 1)))));
    }

    const auto text = [&values](const char* key) -> const std::string& {
        const auto it = values.find(key);
        if (it == values.end() || it->second.empty())
            throw std::runtime_error(std::string("Plate config has no ") + key);
        return it->second;
    };
    const auto number = [&text](const char* key) {
        const int parsed = std::atoi(text(key).c_str());
        if (parsed <= 0)
            throw std::runtime_error(std::string("Plate config has a bad ") + key);
        return parsed;
    };

    ReaderConfig config;
    config.alphabet = decodeUtf8(text("alphabet"));
    const std::u32string pad = decodeUtf8(text("pad_char"));
    if (pad.size() != 1 || config.alphabet.find(pad[0]) == std::u32string::npos)
        throw std::runtime_error("Plate config pad_char must be one character of the alphabet");
    config.padChar = pad[0];
    config.plateSlots = number("max_plate_slots");
    config.inputWidth = number("img_width");
    config.inputHeight = number("img_height");
    return config;
}

void prepareReaderInput(const Image& plate, const ReaderConfig& config, std::vector<std::uint8_t>& tensor)
{
    tensor.resize(static_cast<std::size_t>(config.inputWidth) * config.inputHeight * 3);
    onnx::resizeRgb(plate, config.inputWidth, config.inputHeight, tensor.data());
}

std::optional<PlateReading> decodeText(const float* probabilities, std::size_t values, const ReaderConfig& config)
{
    const std::size_t letters = config.alphabet.size();
    if (values != static_cast<std::size_t>(config.plateSlots) * letters)
        throw std::runtime_error("Plate reader output does not match its config");

    std::u32string text;
    std::vector<float> scores;
    for (int slot = 0; slot < config.plateSlots; ++slot) {
        const float* row = probabilities + static_cast<std::size_t>(slot) * letters;
        const auto best = std::max_element(row, row + letters);
        text.push_back(config.alphabet[static_cast<std::size_t>(best - row)]);
        scores.push_back(*best);
    }
    while (!text.empty() && text.back() == config.padChar) {
        text.pop_back();
        scores.pop_back();
    }
    if (text.empty())
        return std::nullopt;
    return PlateReading{encodeUtf8(text), *std::min_element(scores.begin(), scores.end())};
}

} // namespace vpr::plate
