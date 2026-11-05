add_executable(test_core test_core.cpp)
target_link_libraries(test_core PRIVATE vpr_application vpr_tracking vpr_simulation Qt6::Test)
add_test(NAME core COMMAND test_core)

add_executable(test_qt_adapters test_qt_adapters.cpp)
target_link_libraries(test_qt_adapters PRIVATE vpr_qt vpr_tracking Qt6::Test)
add_test(NAME qt_adapters COMMAND test_qt_adapters)
set_tests_properties(qt_adapters PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(test_yolox_core test_yolox_core.cpp)
target_link_libraries(test_yolox_core PRIVATE vpr_model_core Qt6::Test)
add_test(NAME yolox_core COMMAND test_yolox_core)

add_executable(test_plate_core test_plate_core.cpp)
target_link_libraries(test_plate_core PRIVATE vpr_model_core Qt6::Test)
add_test(NAME plate_core COMMAND test_plate_core)

if(VPR_HAS_ONNX)
    add_executable(test_real_plates test_real_plates.cpp)
    target_link_libraries(test_real_plates PRIVATE vpr_onnx vpr_qt Qt6::Test)
    target_compile_definitions(test_real_plates PRIVATE
        VPR_MODELS_DIR="${PROJECT_SOURCE_DIR}/models"
        VPR_PLATE_SAMPLES_DIR="${PROJECT_SOURCE_DIR}/data/plate-samples")
    vpr_copy_onnx_runtime(test_real_plates)
    add_test(NAME real_plates COMMAND test_real_plates)
    set_tests_properties(real_plates PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_real_detector test_real_detector.cpp)
    target_link_libraries(test_real_detector PRIVATE vpr_onnx vpr_qt vpr_tracking Qt6::Test)
    target_compile_definitions(test_real_detector PRIVATE VPR_MODELS_DIR="${PROJECT_SOURCE_DIR}/models")
    vpr_copy_onnx_runtime(test_real_detector)
    add_test(NAME real_detector COMMAND test_real_detector)
    set_tests_properties(real_detector PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

add_test(NAME architecture_rules
    COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/CheckArchitecture.cmake)

if(VPR_HAS_ONNX)
    add_executable(test_video_file test_video_file.cpp)
    target_link_libraries(test_video_file PRIVATE vpr_onnx vpr_qt vpr_tracking Qt6::Test)
    target_compile_definitions(test_video_file PRIVATE VPR_MODELS_DIR="${PROJECT_SOURCE_DIR}/models")
    vpr_copy_onnx_runtime(test_video_file)
    add_test(NAME video_file COMMAND test_video_file)
    set_tests_properties(video_file PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 180)
endif()
