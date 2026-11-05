cmake_minimum_required(VERSION 3.21)
project(VehiclePlateRecognition VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

option(VPR_BUILD_UI "Build the small test UI" ON)
option(VPR_BUILD_TESTS "Build the automated tests" ON)

if(MSVC)
    add_compile_options(/W4 /utf-8 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Core libraries. These must not depend on Qt, on a model runtime or on each other's outer layers.
# tests/ runs cmake/CheckArchitecture.cmake to enforce this on every build.
add_library(vpr_domain STATIC
    src/domain/image.cpp
    src/domain/plate_format.cpp)
target_include_directories(vpr_domain PUBLIC src)

add_library(vpr_application STATIC
    src/application/line_crossing.cpp
    src/application/plate_voter.cpp
    src/application/passage_pipeline.cpp)
target_link_libraries(vpr_application PUBLIC vpr_domain)

add_library(vpr_tracking STATIC src/infrastructure/tracking/byte_tracker.cpp)
target_link_libraries(vpr_tracking PUBLIC vpr_domain)

add_library(vpr_simulation STATIC src/simulation/synthetic_scene.cpp)
target_link_libraries(vpr_simulation PUBLIC vpr_domain)

# YOLOX input and output handling, without a model runtime.
add_library(vpr_model_core STATIC
    src/infrastructure/onnx/resize.cpp
    src/infrastructure/onnx/yolox_core.cpp
    src/infrastructure/onnx/plate_core.cpp)
target_link_libraries(vpr_model_core PUBLIC vpr_domain)

# ONNX Runtime is optional: without it the project builds and tests, but real video shows no detections.
# scripts/fetch-dependencies.sh puts it in third_party/.
file(GLOB vpr_ort_candidates "${PROJECT_SOURCE_DIR}/third_party/onnxruntime-*")
set(vpr_ort_default "")
if(vpr_ort_candidates)
    list(GET vpr_ort_candidates 0 vpr_ort_default)
endif()
set(VPR_ONNXRUNTIME_ROOT "${vpr_ort_default}" CACHE PATH "Folder of an unpacked ONNX Runtime release")

set(VPR_HAS_ONNX OFF)
if(EXISTS "${VPR_ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h")
    set(VPR_HAS_ONNX ON)
    add_library(onnxruntime SHARED IMPORTED)
    target_include_directories(onnxruntime SYSTEM INTERFACE "${VPR_ONNXRUNTIME_ROOT}/include")
    if(WIN32)
        set_target_properties(onnxruntime PROPERTIES
            IMPORTED_LOCATION "${VPR_ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
            IMPORTED_IMPLIB "${VPR_ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
    else()
        set_target_properties(onnxruntime PROPERTIES IMPORTED_LOCATION "${VPR_ONNXRUNTIME_ROOT}/lib/libonnxruntime.so")
    endif()

    add_library(vpr_onnx STATIC
        src/infrastructure/onnx/onnx_session.cpp
        src/infrastructure/onnx/yolox_detector.cpp
        src/infrastructure/onnx/plate_models.cpp)
    target_link_libraries(vpr_onnx PUBLIC vpr_model_core PRIVATE onnxruntime)
    target_compile_definitions(vpr_onnx PUBLIC VPR_HAS_ONNX)
    message(STATUS "ONNX Runtime: ${VPR_ONNXRUNTIME_ROOT}")
else()
    message(STATUS "ONNX Runtime not found: real detection is left out. Run scripts/fetch-dependencies.sh")
endif()

# Windows ships an older onnxruntime.dll in System32, so ours must sit next to the program.
function(vpr_copy_onnx_runtime target)
    if(VPR_HAS_ONNX AND WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${VPR_ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
                "${VPR_ONNXRUNTIME_ROOT}/lib/onnxruntime_providers_shared.dll"
                "$<TARGET_FILE_DIR:${target}>")
    endif()
endfunction()

# Qt adapters: frame sources, storage, worker thread.
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Multimedia Sql Test Widgets)

add_library(vpr_qt STATIC
    src/infrastructure/qt/qt_image.cpp
    src/infrastructure/qt/media_frame_sources.cpp
    src/infrastructure/qt/synthetic_frame_source.cpp
    src/infrastructure/qt/evidence_sink.cpp
    src/infrastructure/qt/sqlite_passage_repository.cpp
    src/infrastructure/qt/pipeline_worker.cpp)
target_link_libraries(vpr_qt PUBLIC vpr_application vpr_simulation Qt6::Core Qt6::Gui Qt6::Multimedia Qt6::Sql)

if(VPR_BUILD_UI)
    add_subdirectory(apps/test-ui)
endif()

if(VPR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
