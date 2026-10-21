FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.16.1
    GIT_SHALLOW TRUE
    SYSTEM)
FetchContent_MakeAvailable(Catch2)
add_compile_options(${comp1_compile_options})

include(Catch)

# One source per line, sorted, so tickets adding tests in parallel merge cleanly.
add_executable(comp1_engine_tests
    engine/AllocationGuard.cpp
    engine/OutputGainTest.cpp
    engine/RealtimeSafetyTest.cpp
    engine/SettingsHandoffTest.cpp
    engine/SubnormalsTest.cpp
)
target_link_libraries(comp1_engine_tests PRIVATE comp1::engine Catch2::Catch2WithMain)
catch_discover_tests(comp1_engine_tests ADD_TAGS_AS_LABELS)

# The Engine is plain C++: no source of it includes JUCE, and its target links nothing of JUCE's.
add_test(NAME engine_has_no_juce
    COMMAND ${CMAKE_COMMAND} "-DENGINE_DIR=${PROJECT_SOURCE_DIR}/engine"
        "-DLINKS=$<TARGET_PROPERTY:comp1_engine,LINK_LIBRARIES>;$<TARGET_PROPERTY:comp1_engine,INTERFACE_LINK_LIBRARIES>"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/engine/NoJuce.cmake)
