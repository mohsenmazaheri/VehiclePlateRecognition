#include "yolox_detector.h"

#include "onnx_session.h"

#include <array>
#include <stdexcept>

namespace vpr {

namespace {

constexpr int kDefaultInputSize = 640;
constexpr int kCocoCar = 2;
constexpr int kCocoMotorcycle = 3;
constexpr int kCocoBus = 5;
constexpr int kCocoTruck = 7;

} // namespace

struct YoloxEngine::Impl {
    Ort::Session session{nullptr};
    std::string inputName;
    std::string outputName;
    int inputWidth = kDefaultInputSize;
    int inputHeight = kDefaultInputSize;
    yolox::DecodeOptions options;
    std::vector<float> tensor;
};

YoloxEngine::YoloxEngine(const std::filesystem::path& modelPath, yolox::DecodeOptions options)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->options = std::move(options);
    m_impl->session = onnx::openSession(modelPath);

    try {
        Ort::AllocatorWithDefaultOptions allocator;
        m_impl->inputName = m_impl->session.GetInputNameAllocated(0, allocator).get();
        m_impl->outputName = m_impl->session.GetOutputNameAllocated(0, allocator).get();

        const auto shape = m_impl->session.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if (shape.size() != 4 || shape[1] != 3)
            throw std::runtime_error("expected an input of batch x 3 x height x width");
        if (shape[2] > 0)
            m_impl->inputHeight = static_cast<int>(shape[2]);
        if (shape[3] > 0)
            m_impl->inputWidth = static_cast<int>(shape[3]);
    } catch (const std::exception& error) {
        throw std::runtime_error("Could not load " + modelPath.string() + ": " + error.what());
    }
}

YoloxEngine::~YoloxEngine() = default;

std::vector<Detection> YoloxEngine::detect(const Image& image)
{
    if (image.empty())
        return {};

    Impl& d = *m_impl;
    const double scale = yolox::prepareInput(image, d.inputWidth, d.inputHeight, d.tensor);

    const std::array<std::int64_t, 4> shape{1, 3, d.inputHeight, d.inputWidth};
    const auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    const Ort::Value input = Ort::Value::CreateTensor<float>(memory, d.tensor.data(), d.tensor.size(),
                                                             shape.data(), shape.size());
    const char* inputNames[] = {d.inputName.c_str()};
    const char* outputNames[] = {d.outputName.c_str()};
    const auto outputs = d.session.Run(Ort::RunOptions{nullptr}, inputNames, &input, 1, outputNames, 1);

    const auto outputShape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
    if (outputShape.size() != 3 || outputShape[0] != 1)
        throw std::runtime_error("YOLOX output is not batch x rows x columns");
    return yolox::decode(outputs[0].GetTensorData<float>(), static_cast<std::size_t>(outputShape[1]),
                         static_cast<std::size_t>(outputShape[2]), d.inputWidth, d.inputHeight, scale,
                         image.width, image.height, d.options);
}

std::string YoloxEngine::executionProvider() const
{
    return "CPU";  // only the default provider is requested so far
}

YoloxVehicleDetector::YoloxVehicleDetector(const std::filesystem::path& modelPath)
    : m_engine(modelPath, yolox::DecodeOptions{0.1F, 0.45F, {kCocoCar, kCocoMotorcycle, kCocoBus, kCocoTruck}})
{
}

std::vector<Detection> YoloxVehicleDetector::detect(const Image& frame)
{
    return m_engine.detect(frame);
}

} // namespace vpr
