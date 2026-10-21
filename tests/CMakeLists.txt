FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.16.1
    GIT_SHALLOW TRUE
    SYSTEM)
FetchContent_MakeAvailable(Catch2)
add_compile_options(${comp1_compile_options})

# The sample rates the tests run at (tests/engine/Measure.h) are plugkit.conf's SAMPLE_RATES, the ones pluginval
# runs at: one list for both, read here (a changed plugkit.conf reconfigures).
file(STRINGS "${PROJECT_SOURCE_DIR}/plugkit.conf" comp1_sample_rates REGEX "^SAMPLE_RATES=")
string(REPLACE "SAMPLE_RATES=" "" comp1_sample_rates "${comp1_sample_rates}")
if(NOT comp1_sample_rates MATCHES "^[0-9]+(,[0-9]+)*$")
    message(FATAL_ERROR "plugkit.conf's SAMPLE_RATES is '${comp1_sample_rates}': it must be whole numbers of Hz, comma-separated")
endif()
add_compile_definitions("COMP1_SAMPLE_RATES=${comp1_sample_rates}")

include(Catch)

# One source per line, sorted, so tickets adding tests in parallel merge cleanly.
add_executable(comp1_engine_tests
    engine/AliasingTest.cpp
    engine/AutoGainTest.cpp
    engine/AutoReleaseTest.cpp
    engine/ClickFreeTest.cpp
    engine/DetectionAuditionTest.cpp
    engine/DetectionEqTest.cpp
    engine/DetectionSourceTest.cpp
    engine/DetectionStageTest.cpp
    engine/GlobalBypassTest.cpp
    engine/LatencyTest.cpp
    engine/LevelTapsTest.cpp
    engine/LookaheadTest.cpp
    engine/NonFiniteInputTest.cpp
    engine/OutputGainTest.cpp
    engine/ProcessingModeTest.cpp
    engine/RealtimeSafetyTest.cpp
    engine/SaturationTest.cpp
    engine/SettingsHandoffTest.cpp
    engine/StepResponseTest.cpp
    engine/StereoLinkTest.cpp
    engine/StereoPlacementTest.cpp
    engine/SubnormalsTest.cpp
    engine/TransferCurveTest.cpp
)
# The detection stage is Engine-private (engine/src), and its contract is tested directly, with the plugkit pieces it
# includes.
target_include_directories(comp1_engine_tests PRIVATE ${PROJECT_SOURCE_DIR}/engine/src)
# plugkit::test_support's AllocationGuard replaces operator new and delete in this executable.
target_link_libraries(comp1_engine_tests PRIVATE comp1::engine plugkit::dsp plugkit::test_support Catch2::Catch2WithMain)
catch_discover_tests(comp1_engine_tests ADD_TAGS_AS_LABELS)

# The Engine is plain C++: no source of it includes JUCE, and its target links nothing of JUCE's.
add_test(NAME engine_has_no_juce
    COMMAND ${CMAKE_COMMAND} "-DENGINE_DIR=${PROJECT_SOURCE_DIR}/engine"
        "-DLINKS=$<TARGET_PROPERTY:comp1_engine,LINK_LIBRARIES>;$<TARGET_PROPERTY:comp1_engine,INTERFACE_LINK_LIBRARIES>"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/engine/NoJuce.cmake)

if(COMP1_BUILD_PLUGIN)
    # Host-level checks drive the Plugin Shell the way a host does. One source per line, as above.
    add_executable(comp1_plugin_tests
        plugin/AutoReleaseParameterTest.cpp
        plugin/GlobalBypassParameterTest.cpp
        plugin/LatencyReportTest.cpp
        plugin/PluginShellTest.cpp
        plugin/SaturationParameterTest.cpp
        plugin/SessionTest.cpp
        plugin/StateTest.cpp
        plugin/StereoParametersTest.cpp
    )
    # tests/engine for Measure.h, the test helpers both targets share. plugkit-ui's headers, not its sources, which
    # compile into the plugin target alone.
    target_include_directories(comp1_plugin_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/plugin
        ${CMAKE_CURRENT_SOURCE_DIR}/engine
        $<TARGET_PROPERTY:comp1,INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:plugkit-ui,INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(comp1_plugin_tests PRIVATE $<TARGET_PROPERTY:comp1,COMPILE_DEFINITIONS>)
    target_link_libraries(comp1_plugin_tests PRIVATE comp1 comp1::engine Catch2::Catch2WithMain)
    # The tests compare samples and values exactly, on purpose. Set on the sources, whose options come after the
    # -Wfloat-equal JUCE's recommended warnings bring in through the plugin target.
    get_target_property(comp1_plugin_test_sources comp1_plugin_tests SOURCES)
    set_property(SOURCE ${comp1_plugin_test_sources} APPEND PROPERTY COMPILE_OPTIONS
        $<$<CXX_COMPILER_ID:AppleClang,Clang>:-Wno-float-equal>)
    catch_discover_tests(comp1_plugin_tests)

    # Hosts the built plugin through its VST3 or AU wrapper (the check harness's validate stage).
    juce_add_console_app(comp1_sidechain_routing_check PRODUCT_NAME "comp1_sidechain_routing_check")
    target_sources(comp1_sidechain_routing_check PRIVATE host/SidechainRoutingCheck.cpp)
    target_compile_definitions(comp1_sidechain_routing_check PRIVATE
        JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(comp1_sidechain_routing_check PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
    if(APPLE)
        target_link_libraries(comp1_sidechain_routing_check PRIVATE "-framework CoreAudioKit")
    endif()
endif()
