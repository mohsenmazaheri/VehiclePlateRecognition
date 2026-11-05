#pragma once
#include <onnxruntime_cxx_api.h>

#include <filesystem>

namespace vpr::onnx {

// Opens a model on the default provider. Throws std::runtime_error when the file is missing or bad.
[[nodiscard]] Ort::Session openSession(const std::filesystem::path& modelPath);

} // namespace vpr::onnx
