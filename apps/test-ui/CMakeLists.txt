add_executable(vpr-test-ui WIN32 main.cpp main_window.cpp video_view.cpp image_box.cpp language.cpp)
target_link_libraries(vpr-test-ui PRIVATE vpr_qt vpr_tracking Qt6::Widgets)
if(VPR_HAS_ONNX)
    target_link_libraries(vpr-test-ui PRIVATE vpr_onnx)
    vpr_copy_onnx_runtime(vpr-test-ui)
endif()

# Translations of the window. English is the source language and needs no file. To add a language,
# add its .ts file here and its name in Language::available(). The update_translations target
# refreshes the .ts files from the code; the .qm files are built into the program.
find_package(Qt6 OPTIONAL_COMPONENTS LinguistTools)
if(Qt6LinguistTools_FOUND)
    qt_add_translations(vpr-test-ui TS_FILES translations/vpr-test-ui_fa.ts
                        LUPDATE_OPTIONS -locations none -no-obsolete)
else()
    message(STATUS "Qt Linguist tools not found: the test tool is built in English only")
endif()
