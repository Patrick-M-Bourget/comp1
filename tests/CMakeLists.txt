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
    engine/CharacterTest.cpp
    engine/ClickFreeTest.cpp
    engine/CompFollowTest.cpp
    engine/ColourTest.cpp
    engine/DetectionAuditionTest.cpp
    engine/DetectionEqTest.cpp
    engine/DetectionSourceTest.cpp
    engine/DetectionStageTest.cpp
    engine/DryWetTest.cpp
    engine/GateTest.cpp
    engine/GlobalBypassTest.cpp
    engine/LatencyTest.cpp
    engine/LevelTapsTest.cpp
    engine/LimiterTest.cpp
    engine/LookaheadTest.cpp
    engine/NonFiniteInputTest.cpp
    engine/OutputGainTest.cpp
    engine/OversamplerTest.cpp
    engine/OversamplingTest.cpp
    engine/ProcessingModeTest.cpp
    engine/RealtimeSafetyTest.cpp
    engine/RenderScenesTest.cpp
    engine/SaturationTest.cpp
    engine/SettingsHandoffTest.cpp
    engine/SpectrumTapsTest.cpp
    engine/StepResponseTest.cpp
    engine/StereoLinkTest.cpp
    engine/StereoPlacementTest.cpp
    engine/StyleTest.cpp
    engine/SubnormalsTest.cpp
    engine/TransferCurveTest.cpp
    engine/VocalStyleTest.cpp
)
# The detection stage is Engine-private (engine/src), and its contract is tested directly, with the plugkit pieces it
# includes.
target_include_directories(comp1_engine_tests PRIVATE ${PROJECT_SOURCE_DIR}/engine/src)
# dyn-lab's script, whose compressor defaults StyleTest pins to the Engine's.
target_compile_definitions(comp1_engine_tests PRIVATE COMP1_DYN_LAB="${PROJECT_SOURCE_DIR}/tools/dyn-lab/dynlab.py")
# plugkit::test_support's AllocationGuard replaces operator new and delete in this executable.
target_link_libraries(comp1_engine_tests PRIVATE comp1::engine plugkit::dsp plugkit::test_support Catch2::Catch2WithMain)
catch_discover_tests(comp1_engine_tests ADD_TAGS_AS_LABELS)

# The performance tools (docs/performance.md), linked against the Engine only and built with the tests. Neither is a
# ctest test: the CPU budget (scripts/check.sh cpu) needs the machine to itself, and the render tool (scripts/check.sh
# null) compares two trees' renders; its scenes are tested in comp1_engine_tests.
add_executable(comp1_cpu_budget cpu/CpuBudget.cpp)
target_link_libraries(comp1_cpu_budget PRIVATE comp1::engine)
add_executable(comp1_render render/RenderTool.cpp)
target_link_libraries(comp1_render PRIVATE comp1::engine)

# The Engine is plain C++: no source of it includes JUCE, and its target links nothing of JUCE's.
add_test(NAME engine_has_no_juce
    COMMAND ${CMAKE_COMMAND} "-DENGINE_DIR=${PROJECT_SOURCE_DIR}/engine"
        "-DLINKS=$<TARGET_PROPERTY:comp1_engine,LINK_LIBRARIES>;$<TARGET_PROPERTY:comp1_engine,INTERFACE_LINK_LIBRARIES>"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/engine/NoJuce.cmake)

# On macOS each plugin bundle's signature seals everything in it, moduleinfo.json included, or hosts such as Live
# refuse to load it (plugin/CMakeLists.txt signs the bundle last).
if(COMP1_BUILD_PLUGIN AND APPLE)
    foreach(format VST3 AU AAX Standalone CLAP)
        if(TARGET comp1_${format})
            add_test(NAME "The ${format} bundle's signature seals its contents"
                COMMAND codesign --verify --strict "$<TARGET_BUNDLE_DIR:comp1_${format}>")
        endif()
    endforeach()
endif()

if(COMP1_BUILD_PLUGIN)
    # Host-level checks drive the Plugin Shell the way a host does. One source per line, as above.
    add_executable(comp1_plugin_tests
        plugin/AutoReleaseParameterTest.cpp
        plugin/CachedGradientFillTest.cpp
        plugin/ColourParameterTest.cpp
        plugin/CompactViewTest.cpp
        plugin/CompFollowEditorTest.cpp
        plugin/CompParameterTest.cpp
        plugin/DetectionSectionTest.cpp
        plugin/DisplayTabTest.cpp
        plugin/DryWetParameterTest.cpp
        plugin/EditorControlsTest.cpp
        plugin/EditorCpuTest.cpp
        plugin/EditorLayoutTest.cpp
        plugin/EditorMetersTest.cpp
        plugin/EditorScreensTest.cpp
        plugin/EditorTest.cpp
        plugin/FactoryPresetsTest.cpp
        plugin/GateParameterTest.cpp
        plugin/GlobalBypassFadeTest.cpp
        plugin/GlobalBypassParameterTest.cpp
        plugin/LatencyReportTest.cpp
        plugin/LevelGraphTest.cpp
        plugin/LevelHistoryTest.cpp
        plugin/LimiterParameterTest.cpp
        plugin/MeterLabelTest.cpp
        plugin/OutputMeterTest.cpp
        plugin/OutputPopoverTest.cpp
        plugin/OversamplingParameterTest.cpp
        plugin/PluginShellTest.cpp
        plugin/SaturationParameterTest.cpp
        plugin/SessionTest.cpp
        plugin/SpectrumDisplayTest.cpp
        plugin/StateTest.cpp
        plugin/StereoParametersTest.cpp
        plugin/StyleChipTest.cpp
        plugin/StyleParameterTest.cpp
        plugin/TransferCurveGraphTest.cpp
        plugin/TransferCurveInsetTest.cpp
        plugin/UiScaleTest.cpp
        plugin/ViewSwitchTest.cpp
    )
    # tests/engine for Measure.h, the test helpers both targets share. plugkit-ui's headers, not its sources, which
    # compile into the plugin target alone.
    target_include_directories(comp1_plugin_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/plugin
        ${CMAKE_CURRENT_SOURCE_DIR}/engine
        $<TARGET_PROPERTY:comp1,INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:plugkit-ui,INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(comp1_plugin_tests PRIVATE $<TARGET_PROPERTY:comp1,COMPILE_DEFINITIONS>)
    # The Factory folder, so a test can check every file in it is bundled.
    target_compile_definitions(comp1_plugin_tests PRIVATE COMP1_FACTORY_PRESETS="${PROJECT_SOURCE_DIR}/presets/factory")
    target_link_libraries(comp1_plugin_tests PRIVATE comp1 comp1::engine plugkit::editor_harness Catch2::Catch2WithMain)
    # The editor harness reads COMP1_SCREENS, where check.sh screens writes the snapshots (plugkit's README.md,
    # "Linking plugkit-ui").
    target_compile_definitions(comp1_plugin_tests PRIVATE PLUGKIT_HARNESS_VAR_PREFIX="COMP1_")
    # The editor's accessible names are checked against the glossary's words.
    target_compile_definitions(comp1_plugin_tests PRIVATE COMP1_GLOSSARY="${PROJECT_SOURCE_DIR}/GLOSSARY.md")
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
