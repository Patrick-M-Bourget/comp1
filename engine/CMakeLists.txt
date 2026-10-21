# The DSP Engine: plain C++. It includes and links nothing of JUCE's (tests/CMakeLists.txt checks it).
add_compile_options(${comp1_compile_options})
add_library(comp1_engine STATIC
    src/Engine.cpp
)
add_library(comp1::engine ALIAS comp1_engine)

target_include_directories(comp1_engine PUBLIC include PRIVATE src)
target_compile_features(comp1_engine PUBLIC cxx_std_20)
# plugkit's shared Engine pieces: the Smoother, the run grid, subnormal flushing (ADR 0006).
target_link_libraries(comp1_engine PRIVATE plugkit::dsp)

if(MSVC)
    target_compile_options(comp1_engine PRIVATE /W4)
else()
    target_compile_options(comp1_engine PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()
