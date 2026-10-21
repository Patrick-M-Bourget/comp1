# Fails when the Engine's sources include anything of JUCE's or its target links any JUCE library: the Engine is
# plain C++ (the ctest engine_has_no_juce, tests/CMakeLists.txt).
# Usage: cmake -DENGINE_DIR=<engine source dir> "-DLINKS=<the Engine target's link libraries>" -P NoJuce.cmake
file(GLOB_RECURSE sources "${ENGINE_DIR}/*.h" "${ENGINE_DIR}/*.hpp" "${ENGINE_DIR}/*.cpp")
set(found "")
foreach(source IN LISTS sources)
    file(STRINGS "${source}" lines REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"][^>\"]*[Jj][Uu][Cc][Ee]")
    foreach(line IN LISTS lines)
        string(APPEND found "\n  ${source}: ${line}")
    endforeach()
endforeach()
if(LINKS MATCHES "[Jj][Uu][Cc][Ee]")
    string(APPEND found "\n  the Engine target links ${LINKS}")
endif()
if(found)
    message(FATAL_ERROR "The Engine must not depend on JUCE:${found}")
endif()
list(LENGTH sources count)
message(STATUS "No JUCE in the Engine's ${count} sources or its links")
