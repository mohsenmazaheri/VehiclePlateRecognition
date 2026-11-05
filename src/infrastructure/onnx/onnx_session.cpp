#include "onnx_session.h"

#include <stdexcept>

namespace vpr::onnx {

Ort::Session openSession(const std::filesystem::path& modelPath)
{
    static Ort::Env environment(ORT_LOGGING_LEVEL_WARNING, "vpr");
    if (!std::filesystem::exists(modelPath))
        throw std::runtime_error("Model file not found: " + modelPath.string());
    try {
        Ort::SessionOptions options;
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        // Several models run one after another on the same thread. Idle threads of one model must not
        // keep spinning and take the processor from the next one.
        options.AddConfigEntry("session.intra_op.allow_spinning", "0");
        return Ort::Session(environment, modelPath.c_str(), options);
    } catch (const std::exception& error) {
        throw std::runtime_error("Could not load " + modelPath.string() + ": " + error.what());
    }
}

} // namespace vpr::onnx
