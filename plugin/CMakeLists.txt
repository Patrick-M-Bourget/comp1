# The Plugin Shell: the JUCE layer around the DSP Engine. JUCE is fetched in the root CMakeLists.txt, before plugkit.

# CLAP comes from clap-juce-extensions until JUCE ships native CLAP, pinned to a commit, which can't move under the
# build: no release supports JUCE 9 yet. The patch asks the host to rescan parameter values after a state load
# (upstream PR free-audio/clap-juce-extensions#189); drop it once merged.
FetchContent_Declare(clap-juce-extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG 7adee3a1bd4684d4caa5601100e364abccff4b4d
    PATCH_COMMAND ${CMAKE_COMMAND}
        -DPATCH=${PROJECT_SOURCE_DIR}/cmake/patches/clap-juce-extensions-rescan-after-state-load.patch
        -P ${PROJECT_SOURCE_DIR}/cmake/ApplyPatch.cmake
    SYSTEM
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(clap-juce-extensions)
add_compile_options(${comp1_compile_options})

# The codes and IDs are permanent once released: hosts find the plugin and its sessions by them.
juce_add_plugin(comp1
    PRODUCT_NAME "comp1"
    COMPANY_NAME "comp1"
    BUNDLE_ID "com.pbourget.comp1"
    PLUGIN_MANUFACTURER_CODE Pmbg
    PLUGIN_CODE Cp01
    FORMATS VST3 AU AAX Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Fx Dynamics
    AU_MAIN_TYPE kAudioUnitType_Effect
    AAX_CATEGORY AAX_ePlugInCategory_Dynamics
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(comp1 PRIVATE
    CachedGradientFill.cpp
    CompactView.cpp
    CompFollowAttachment.cpp
    Controls.cpp
    Display.cpp
    DragHandle.cpp
    DisplayFades.cpp
    Footer.cpp
    GainReductionMeter.cpp
    GlobalBypassFade.cpp
    HandlePainting.cpp
    HeroComp.cpp
    LevelGraph.cpp
    LevelHistory.cpp
    LevelRing.cpp
    MeterLabel.cpp
    OutputPopover.cpp
    Parameters.cpp
    PluginEditor.cpp
    PluginProcessor.cpp
    Section.cpp
    Sections.cpp
    ShownAutoGain.cpp
    SpectrumDisplay.cpp
    TransferCurveGraph.cpp
    TransferCurveInset.cpp)

# The Factory Presets, bundled into the plugin: every file in presets/factory, found again at each build
# (CONFIGURE_DEPENDS), so adding or removing one needs no edit here.
file(GLOB comp1_factory_preset_files CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/presets/factory/*.comp1preset")
juce_add_binary_data(comp1_factory_presets
    NAMESPACE FactoryPresets
    HEADER_NAME FactoryPresets.h
    SOURCES ${comp1_factory_preset_files})

# comp1 reads and writes no audio or image file, and opens no web page: every codec and browser JUCE would
# otherwise compile in is off.
target_compile_definitions(comp1 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_FLAC=0
    JUCE_USE_OGGVORBIS=0
    JUCE_USE_OPUS=0
    JUCE_USE_MP3AUDIOFORMAT=0
    JUCE_USE_WINDOWS_MEDIA_FORMAT=0
    JUCE_USE_WEBP=0)

target_link_libraries(comp1
    PRIVATE
        comp1::engine
        comp1_factory_presets
        juce::juce_audio_utils
        # From the plugin target only, which compiles its sources (plugkit's README.md, "Linking plugkit-ui").
        plugkit::ui
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if(COMP1_LTO)
    target_link_libraries(comp1 PUBLIC juce::juce_recommended_lto_flags)
endif()

clap_juce_extensions_plugin(TARGET comp1
    CLAP_ID "com.pbourget.comp1"
    CLAP_FEATURES audio-effect compressor mono stereo)

# Each format's binary is linked without the code nothing reaches from its entry points. A shipping build
# (COMP1_STRIP) compiles with debug information, keeps it beside the bundle (a .dSYM on macOS, a PDB on Windows)
# and strips the binary of its local symbols. Both are decided per configuration, so a multi-config generator gets
# each right: with COMP1_STRIP empty every configuration but Debug strips and is dead-stripped; ON strips and
# dead-strips every configuration; OFF strips none but leaves dead-stripping as by default (the check harness sets
# it, so its Release test builds keep their symbols and link like the shipping build).
set(comp1_release_config "$<NOT:$<CONFIG:Debug>>")
if(COMP1_STRIP STREQUAL "")
    set(comp1_strip "${comp1_release_config}")
elseif(COMP1_STRIP)
    set(comp1_strip 1)
else()
    set(comp1_strip 0)
endif()
set(comp1_dead_strip "$<OR:${comp1_strip},${comp1_release_config}>")
if(APPLE)
    target_compile_options(comp1 PUBLIC $<${comp1_strip}:-g>)
endif()
foreach(format VST3 AU AAX Standalone CLAP)
    set(binary comp1_${format})
    # AU is built on macOS only (and AAX only where its SDK is found).
    if(NOT TARGET ${binary})
        continue()
    endif()
    if(APPLE)
        target_link_options(${binary} PRIVATE $<${comp1_dead_strip}:LINKER:-dead_strip>)
        # With link-time optimisation the linker compiles everything into one temporary object, which dsymutil
        # reads the debug information from: each architecture of a Universal link keeps its own.
        list(LENGTH CMAKE_OSX_ARCHITECTURES arch_count)
        if(arch_count GREATER 1)
            foreach(arch IN LISTS CMAKE_OSX_ARCHITECTURES)
                target_link_options(${binary} PRIVATE
                    "$<${comp1_strip}:SHELL:-Xarch_${arch} -Wl,-object_path_lto,${CMAKE_CURRENT_BINARY_DIR}/${binary}-${arch}-lto.o>")
            endforeach()
        else()
            target_link_options(${binary} PRIVATE
                $<${comp1_strip}:LINKER:-object_path_lto,${CMAKE_CURRENT_BINARY_DIR}/${binary}-lto.o>)
        endif()
        # Stripping invalidates the linker's ad hoc signature, without which macOS loads no arm64 code, so the
        # binary is signed ad hoc again. A command whose expression is empty isn't run.
        if(NOT comp1_strip STREQUAL "0")
            add_custom_command(TARGET ${binary} POST_BUILD
                COMMAND "$<${comp1_strip}:dsymutil;$<TARGET_FILE:${binary}>;-o;$<TARGET_BUNDLE_DIR:${binary}>.dSYM>"
                COMMAND "$<${comp1_strip}:strip;-x;-no_code_signature_warning;$<TARGET_FILE:${binary}>>"
                COMMAND "$<${comp1_strip}:codesign;--force;--sign;-;$<TARGET_FILE:${binary}>>"
                COMMAND_EXPAND_LISTS
                VERBATIM)
        endif()
        # The linker signs only the binary, and JUCE then writes the VST3's moduleinfo.json into the bundle, so the
        # bundle's seal no longer matches and hosts such as Live refuse to load it. Signing the whole bundle ad hoc,
        # after every other step, seals it as it ships; the release signature replaces this one.
        add_custom_command(TARGET ${binary} POST_BUILD
            COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:${binary}>"
            VERBATIM)
    elseif(MSVC)
        # /OPT:REF and /OPT:ICF are the linker's defaults until /DEBUG turns them off, so they are spelled out.
        target_link_options(${binary} PRIVATE $<${comp1_dead_strip}:/OPT:REF;/OPT:ICF>)
        set_property(TARGET comp1 ${binary} PROPERTY MSVC_DEBUG_INFORMATION_FORMAT
            $<$<OR:${comp1_strip},$<CONFIG:Debug,RelWithDebInfo>>:ProgramDatabase>)
        target_link_options(${binary} PRIVATE $<${comp1_strip}:/DEBUG>)
    endif()
endforeach()
