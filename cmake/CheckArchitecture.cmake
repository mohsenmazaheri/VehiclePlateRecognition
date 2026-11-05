# Fails when an include points the wrong way. Dependencies may only point inwards:
#   infrastructure, apps -> application -> domain
# Run as: cmake -DROOT=<repo> -P cmake/CheckArchitecture.cmake

set(FOREIGN "^#include[ ]*[<\"](Q[A-Z]|qt|opencv|onnxruntime)")
set(failures "")

function(check_dir dir forbidden_regex)
    file(GLOB_RECURSE files "${ROOT}/${dir}/*.h" "${ROOT}/${dir}/*.cpp")
    list(LENGTH files count)
    if(count EQUAL 0)
        message(FATAL_ERROR "No source files found in ${dir}, the check would pass for the wrong reason")
    endif()
    foreach(file IN LISTS files)
        file(STRINGS "${file}" lines REGEX "^#include")
        foreach(line IN LISTS lines)
            if(line MATCHES "${FOREIGN}" OR line MATCHES "${forbidden_regex}")
                string(APPEND failures "${file}: ${line}\n")
            endif()
        endforeach()
    endforeach()
    set(failures "${failures}" PARENT_SCOPE)
endfunction()

check_dir("src/domain" "(application|infrastructure|simulation|apps)/")
check_dir("src/application" "(infrastructure|simulation|apps)/")
check_dir("src/infrastructure/tracking" "(application|simulation|apps)/")
check_dir("src/simulation" "(application|infrastructure|apps)/")

# The ONNX adapter is the one place allowed to see the model runtime. It still sees no Qt.
set(FOREIGN "^#include[ ]*[<\"](Q[A-Z]|qt|opencv)")
check_dir("src/infrastructure/onnx" "(application|simulation|apps|infrastructure/qt)/")

if(failures)
    message(FATAL_ERROR "Architecture rule violations:\n${failures}")
endif()
message(STATUS "Architecture rules hold")
