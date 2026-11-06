#include "plate_models.h"

#include "onnx_session.h"
#include "plate_core.h"

#include <array>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace vpr {

namespace {

constexpr int kDefaultInputSize = 640;

std::string outputNameOf(Ort::Session& session, std::size_t output)
{
    Ort::AllocatorWithDefaultOptions allocator;
    return session.GetOutputNameAllocated(output, allocator).get();
}

std::string inputNameOf(Ort::Session& session)
{
    Ort::AllocatorWithDefaultOptions allocator;
    return session.GetInputNameAllocated(0, allocator).get();
}

} // namespace

struct YoloV9PlateDetector::Impl {
    Ort::Session session{nullptr};
    std::string inputName;
    std::string outputName;
    int inputWidth = kDefaultInputSize;
    int inputHeight = kDefaultInputSize;
    float scoreThreshold = 0.25F;
    std::vector<float> tensor;
};

YoloV9PlateDetector::YoloV9PlateDetector(const std::filesystem::path& modelPath, float scoreThreshold)
    : m_impl(std::make_unique<Impl>())
{
    Impl& d = *m_impl;
    d.scoreThreshold = scoreThreshold;
    d.session = onnx::openSession(modelPath);
    d.inputName = inputNameOf(d.session);
    d.outputName = outputNameOf(d.session, 0);

    const auto shape = d.session.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
    if (shape.size() != 4 || shape[1] != 3)
        throw std::runtime_error("Plate detector: expected an input of batch x 3 x height x width");
    if (shape[2] > 0)
        d.inputHeight = static_cast<int>(shape[2]);
    if (shape[3] > 0)
        d.inputWidth = static_cast<int>(shape[3]);
}

YoloV9PlateDetector::~YoloV9PlateDetector() = default;

std::vector<Detection> YoloV9PlateDetector::detect(const Image& vehicleCrop)
{
    if (vehicleCrop.empty())
        return {};

    Impl& d = *m_impl;
    const plate::Letterbox letterbox = plate::prepareDetectorInput(vehicleCrop, d.inputWidth, d.inputHeight, d.tensor);

    const std::array<std::int64_t, 4> shape{1, 3, d.inputHeight, d.inputWidth};
    const auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    const Ort::Value input = Ort::Value::CreateTensor<float>(memory, d.tensor.data(), d.tensor.size(),
                                                             shape.data(), shape.size());
    const char* inputNames[] = {d.inputName.c_str()};
    const char* outputNames[] = {d.outputName.c_str()};
    const auto outputs = d.session.Run(Ort::RunOptions{nullptr}, inputNames, &input, 1, outputNames, 1);

    const auto outputShape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
    if (outputShape.size() != 2)
        throw std::runtime_error("Plate detector output is not rows x columns");
    return plate::decodeDetections(outputs[0].GetTensorData<float>(), static_cast<std::size_t>(outputShape[0]),
                                   static_cast<std::size_t>(outputShape[1]), letterbox, vehicleCrop.width,
                                   vehicleCrop.height, d.scoreThreshold);
}

struct SlotPlateReader::Impl {
    Ort::Session session{nullptr};
    std::string inputName;
    std::string outputName;
    plate::ReaderConfig config;
    std::vector<std::uint8_t> tensor;
};

SlotPlateReader::SlotPlateReader(const std::filesystem::path& modelPath, const std::filesystem::path& configPath)
    : m_impl(std::make_unique<Impl>())
{
    Impl& d = *m_impl;
    std::ifstream file(configPath, std::ios::binary);
    if (!file)
        throw std::runtime_error("Plate config not found: " + configPath.string());
    std::ostringstream text;
    text << file.rdbuf();
    d.config = plate::parseReaderConfig(text.str());

    d.session = onnx::openSession(modelPath);
    d.inputName = inputNameOf(d.session);
    d.outputName = outputNameOf(d.session, 0);
    for (std::size_t i = 0; i < d.session.GetOutputCount(); ++i) {
        if (outputNameOf(d.session, i) == "plate")
            d.outputName = "plate";
    }

    // The shape info is a view into the type info, so the type info must outlive it. A temporary here
    // left a dangling view: it happened to work on Windows and read freed memory on Linux.
    const Ort::TypeInfo typeInfo = d.session.GetInputTypeInfo(0);
    const auto info = typeInfo.GetTensorTypeAndShapeInfo();
    const auto shape = info.GetShape();
    if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8 || shape.size() != 4 || shape[3] != 3
        || shape[1] != d.config.inputHeight || shape[2] != d.config.inputWidth) {
        throw std::runtime_error("Plate reader: the model input does not match its config");
    }
}

SlotPlateReader::~SlotPlateReader() = default;

std::optional<PlateReading> SlotPlateReader::read(const Image& plateCrop)
{
    if (plateCrop.empty())
        return std::nullopt;

    Impl& d = *m_impl;
    plate::prepareReaderInput(plateCrop, d.config, d.tensor);

    const std::array<std::int64_t, 4> shape{1, d.config.inputHeight, d.config.inputWidth, 3};
    const auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    const Ort::Value input = Ort::Value::CreateTensor<std::uint8_t>(memory, d.tensor.data(), d.tensor.size(),
                                                                    shape.data(), shape.size());
    const char* inputNames[] = {d.inputName.c_str()};
    const char* outputNames[] = {d.outputName.c_str()};
    const auto outputs = d.session.Run(Ort::RunOptions{nullptr}, inputNames, &input, 1, outputNames, 1);

    return plate::decodeText(outputs[0].GetTensorData<float>(),
                             outputs[0].GetTensorTypeAndShapeInfo().GetElementCount(), d.config);
}

} // namespace vpr
