// Style: the host parameter that picks the Character, and choosing a Style through the Plugin Shell, which writes its
// defaults (ADR 0002).

#include "Host.h"
#include "Measure.h"
#include "ParametersOnly.h"
#include "SessionHost.h"
#include "comp1/Style.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <vector>

using comp1::Character;
using comp1::Style;
using Catch::Matchers::WithinRel;
using plugkit::ui::CompareSide;

namespace
{

float styleValue (Style style) { return static_cast<float> (style); }

// What a host's set of plain leaves in the parameter: its value after the trip through the normalised range.
float asStored (juce::AudioProcessor& processor, const juce::String& id, double plain)
{
    auto& p = host::parameter (processor, id);
    return p.convertFrom0to1 (p.convertTo0to1 (static_cast<float> (plain)));
}

// The host parameters choosing style writes, with the plain values ADR 0002 has it write: Style, and the Style's
// default Comp with Comp Follow on, its Colour, Threshold, Ratio, Knee, Attack, Release, Auto Release, Hold, Range, Gate and Limiter settings.
std::map<juce::String, double> writtenBy (Style style)
{
    const auto s = comp1::styled (style);
    return {
        { "style", styleValue (style) },
        { "comp", s.comp },
        { "comp_follow", 1.0 },
        { "colour", s.colour },
        { "threshold", s.thresholdDb },
        { "ratio", s.ratio },
        { "knee", s.kneeDb },
        { "attack", s.attackMs },
        { "release", s.releaseMs },
        { "auto_release", s.autoRelease ? 1.0 : 0.0 },
        { "hold", s.holdMs },
        { "range", s.rangeDb },
        { "gate", s.gate.on ? 1.0 : 0.0 },
        { "gate_threshold", s.gate.thresholdDb },
        { "gate_range", s.gate.rangeDb },
        { "gate_attack", s.gate.attackMs },
        { "gate_release", s.gate.releaseMs },
        { "gate_hold", s.gate.holdMs },
        { "limiter", s.limiter ? 1.0 : 0.0 },
        { "ceiling", s.ceilingDb },
    };
}

// Every host parameter's plain value.
std::map<juce::String, float> values (juce::AudioProcessor& processor)
{
    std::map<juce::String, float> all;
    for (auto* p : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            all[ranged->getParameterID()] = ranged->convertFrom0to1 (ranged->getValue());
    return all;
}

// Moves every host parameter off its default, each as an edit: Style to Bus, a Source Style, and the rest to a fifth
// of their travel from the far end.
void moveEverything (const host::SessionHost& host)
{
    for (auto* p : host.processor->getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p);
        REQUIRE (ranged != nullptr);
        ranged->beginChangeGesture();
        ranged->setValueNotifyingHost (ranged->getDefaultValue() < 0.5f ? 0.8f : 0.2f);
        ranged->endChangeGesture();
        CAPTURE (ranged->getParameterID());
        REQUIRE (ranged->getValue() != ranged->getDefaultValue());
    }
    REQUIRE (host.value ("style") == styleValue (Style::Bus));
}

// The Gain Reduction in dB on each sample of seeded noise through processor, as it is set, in mono.
std::vector<double> gainReductionThrough (comp1::PluginProcessor& processor)
{
    REQUIRE (processor.setBusesLayout (host::layout (juce::AudioChannelSet::mono(), juce::AudioChannelSet::disabled())));
    constexpr int blockSize = 512, blocks = 24;
    processor.prepareToPlay (48000.0, blockSize);
    const auto noise = comp1::test::noise (1, blocks * blockSize, 3);
    std::vector<float> output;
    juce::AudioBuffer<float> buffer (1, blockSize);
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b)
    {
        buffer.copyFrom (0, 0, noise[0].data() + b * blockSize, blockSize);
        processor.processBlock (buffer, midi);
        output.insert (output.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
    }
    return comp1::test::gainReductionDb (noise[0], output);
}

// The Gain Reduction in dB on each sample of seeded noise through a processor at Style style, Colour 0% so the output's
// gain is the compressor's alone, every other parameter at its default.
std::vector<double> gainReduction (Style style)
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    host::set (processor, "style", styleValue (style));
    host::set (processor, "colour", 0.0f);
    return gainReductionThrough (processor);
}

// The most two Gain Reductions differ by, sample for sample, in dB.
double mostApart (const std::vector<double>& a, const std::vector<double>& b)
{
    double most = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
        most = std::max (most, std::abs (a[i] - b[i]));
    return most;
}

} // namespace

TEST_CASE ("Style is a host parameter, listed first, with the eight Styles in their frozen order, Clean by default, not automatable")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    auto& style = host::parameter (processor, "style");
    const auto* choice = dynamic_cast<juce::AudioParameterChoice*> (&style);
    REQUIRE (choice != nullptr);
    CHECK (style.getName (100) == "Style");
    CHECK (choice->choices == juce::StringArray { "Clean", "VCA", "FET", "Opto", "Vocal", "Instrument", "Bus", "Drums" });
    CHECK (style.convertFrom0to1 (style.getDefaultValue()) == styleValue (Style::Clean));
    CHECK_FALSE (style.isAutomatable());
    CHECK (style.getParameterIndex() == 0);
}

TEST_CASE ("Style alone decides the Settings' Character and Comp Curve, each Source Style its own Character's")
{
    juce::ScopedJuceInitialiser_GUI juce;
    host::ParametersOnly processor;
    const comp1::parameters::SettingsReader reader (processor.parameters);
    CHECK (reader.read().character == Character::Clean);
    for (const auto& [style, character] : std::vector<std::pair<Style, Character>> {
             { Style::VCA, Character::VCA },
             { Style::FET, Character::FET },
             { Style::Opto, Character::Opto },
             { Style::Clean, Character::Clean },
             { Style::Vocal, Character::Clean },
             { Style::Instrument, Character::Opto },
             { Style::Bus, Character::VCA },
             { Style::Drums, Character::FET },
         })
    {
        CAPTURE (static_cast<int> (style));
        host::set (processor, "style", styleValue (style));
        CHECK (reader.read().character == character);
        CHECK (reader.read().compCurve == comp1::compCurveOf (style));
    }
}

TEST_CASE ("Style set by the host reaches the Engine as its Character: every Style but Vocal compresses unlike Clean, and Vocal as Clean")
{
    // Every other parameter at its default, Comp Follow off among them, so each Style plays the same stored settings
    // and only its Character can differ: Vocal's is Clean.
    const auto clean = gainReduction (Style::Clean);
    REQUIRE (*std::max_element (clean.begin(), clean.end()) > 3.0);
    for (const auto style : { Style::VCA, Style::FET, Style::Opto, Style::Instrument, Style::Bus, Style::Drums })
    {
        CAPTURE (static_cast<int> (style));
        CHECK (mostApart (gainReduction (style), clean) > 1.0);
    }
    CHECK (gainReduction (Style::Vocal) == clean);
}

TEST_CASE ("The Styles offered are all eight, in the frozen order")
{
    CHECK (comp1::PluginProcessor::offeredStyles() == comp1::test::everyStyle());
}

TEST_CASE ("Choosing a Style sets Style and exactly its defaults, as one undo step inside gestures, and undo and redo put it all back")
{
    const auto style = GENERATE (from_range (comp1::test::everyStyle()));
    CAPTURE (static_cast<int> (style));
    host::SessionHost host;
    moveEverything (host);
    const auto before = values (*host.processor);
    const int steps = host.history.undoSteps();

    host::GestureRecorder recorder;
    host.processor->addListener (&recorder);
    REQUIRE (host.processor->chooseStyle (style));
    host.processor->removeListener (&recorder);

    const auto written = writtenBy (style);
    const auto after = values (*host.processor);
    for (const auto& [id, value] : after)
    {
        CAPTURE (id);
        if (const auto w = written.find (id); w != written.end())
            CHECK_THAT (value, WithinRel (asStored (*host.processor, id, w->second), 1.0e-6f));
        else
            CHECK (value == before.at (id)); // Auto Gain, the routing, the levels and the whole plugin's keep theirs
    }
    CHECK (host.history.undoSteps() == steps + 1);
    REQUIRE_FALSE (recorder.changes.empty());
    for (const auto& [index, inGesture] : recorder.changes)
    {
        CAPTURE (index);
        CHECK (inGesture);
    }

    host.history.undo();
    CHECK (values (*host.processor) == before);
    host.history.redo();
    CHECK (values (*host.processor) == after);
}

TEST_CASE ("Choosing the Style already chosen writes its defaults again")
{
    host::SessionHost host;
    REQUIRE (host.processor->chooseStyle (Style::FET));
    host.edit ("attack", 30.0f);
    host.edit ("knee", 20.0f);
    REQUIRE (host.processor->chooseStyle (Style::FET));
    CHECK_THAT (host.value ("attack"), WithinRel (asStored (*host.processor, "attack", 0.5), 1.0e-6f));
    CHECK_THAT (host.value ("knee"), WithinRel (3.0f, 1.0e-6f));
}

TEST_CASE ("Choosing Vocal writes the Gate on with its five settings, Auto Release on, and the Limiter on with its Ceiling")
{
    host::SessionHost host;
    moveEverything (host);
    host.edit ("gate", 0.0f);
    host.edit ("auto_release", 0.0f);
    host.edit ("limiter", 0.0f);
    REQUIRE (host.processor->chooseStyle (Style::Vocal));
    const auto vocal = comp1::styled (Style::Vocal);
    CHECK (host.value ("gate") == 1.0f);
    CHECK_THAT (host.value ("gate_threshold"), WithinRel (asStored (*host.processor, "gate_threshold", vocal.gate.thresholdDb), 1.0e-6f));
    CHECK_THAT (host.value ("gate_range"), WithinRel (asStored (*host.processor, "gate_range", vocal.gate.rangeDb), 1.0e-6f));
    CHECK_THAT (host.value ("gate_attack"), WithinRel (asStored (*host.processor, "gate_attack", vocal.gate.attackMs), 1.0e-6f));
    CHECK_THAT (host.value ("gate_release"), WithinRel (asStored (*host.processor, "gate_release", vocal.gate.releaseMs), 1.0e-6f));
    CHECK_THAT (host.value ("gate_hold"), WithinRel (asStored (*host.processor, "gate_hold", vocal.gate.holdMs), 1.0e-6f));
    CHECK (host.value ("auto_release") == 1.0f);
    CHECK (host.value ("limiter") == 1.0f);
    CHECK (host.value ("ceiling") == asStored (*host.processor, "ceiling", vocal.ceilingDb));
}

TEST_CASE ("The Style box alone sets Style, not its defaults, and undo and redo put back the Style it held")
{
    host::SessionHost host;
    host.edit ("knee", 20.0f);
    host.edit ("style", styleValue (Style::Opto));
    CHECK (host.value ("style") == styleValue (Style::Opto));
    CHECK (host.value ("knee") == 20.0f);
    CHECK (host.value ("ratio") == 4.0f);
    host.history.undo();
    CHECK (host.value ("style") == styleValue (Style::Clean));
    CHECK (host.value ("knee") == 20.0f);
    host.history.redo();
    CHECK (host.value ("style") == styleValue (Style::Opto));
    CHECK (host.value ("ratio") == 4.0f);
}

TEST_CASE ("Restoring a session, loading a Preset and switching A/B put back a saved Style with its saved settings, never its defaults")
{
    // FET's Style, its Knee and Attack moved off FET's defaults.
    host::SessionHost maker;
    REQUIRE (maker.processor->chooseStyle (Style::FET));
    maker.edit ("knee", 20.0f);
    maker.edit ("attack", 30.0f);
    const auto saved = values (*maker.processor);
    const auto checkAsSaved = [&saved] (const host::SessionHost& host) {
        for (const auto& id : host::presetSettingIds (*host.processor))
        {
            CAPTURE (id);
            CHECK (host.value (id) == saved.at (id));
        }
    };

    SECTION ("A session")
    {
        host::SessionHost host;
        host::restore (*host.processor, host::saved (*maker.processor));
        checkAsSaved (host);
    }
    SECTION ("A Preset")
    {
        host::SessionHost host;
        REQUIRE (host.session.loadPreset (maker.session.presetState(), "Moved FET"));
        checkAsSaved (host);
        host.history.undo();
        CHECK (host.value ("style") == styleValue (Style::Clean));
        host.history.redo();
        checkAsSaved (host);
    }
    SECTION ("A/B")
    {
        // B holds VCA's Style over the same settings: set by the Style box, so without VCA's defaults.
        maker.session.selectCompareSide (CompareSide::B);
        maker.edit ("style", styleValue (Style::VCA));
        maker.session.selectCompareSide (CompareSide::A);
        checkAsSaved (maker);
        maker.session.selectCompareSide (CompareSide::B);
        CHECK (maker.value ("style") == styleValue (Style::VCA));
        CHECK (maker.value ("knee") == saved.at ("knee"));
        CHECK (maker.value ("attack") == saved.at ("attack"));
        maker.history.undo();
        checkAsSaved (maker);
        maker.history.redo();
        CHECK (maker.value ("style") == styleValue (Style::VCA));
        CHECK (maker.value ("knee") == saved.at ("knee"));
    }
}

TEST_CASE ("Style survives save and load, in sessions and Presets, and save, load and save again gives the same bytes")
{
    const auto style = GENERATE (Style::VCA, Style::Opto, Style::Vocal, Style::Instrument, Style::Bus, Style::Drums);
    CAPTURE (static_cast<int> (style));
    host::SessionHost maker;
    maker.edit ("knee", 20.0f); // off every Style's Knee, so a restore that wrote the Style's defaults would show
    maker.edit ("colour", 0.0f);
    maker.edit ("style", styleValue (style));
    const auto first = host::saved (*maker.processor);
    const auto saved = values (*maker.processor);

    host::SessionHost restored;
    host::restore (*restored.processor, first);
    CHECK (values (*restored.processor) == saved);
    CHECK (host::saved (*restored.processor) == first);
    // It plays the saved Style's Character on the saved settings, as a fresh instance set to them does.
    host::SessionHost fresh;
    for (const auto& [id, value] : saved)
        host::set (*fresh.processor, id, value);
    CHECK (gainReductionThrough (*restored.processor) == gainReductionThrough (*fresh.processor));

    host::SessionHost loaded;
    REQUIRE (loaded.session.loadPreset (maker.session.presetState(), "Styled"));
    CHECK (loaded.value ("style") == styleValue (style));
    CHECK (loaded.value ("knee") == 20.0f);
}

TEST_CASE ("A session holding a Source Style plays that Style's Character, not Clean")
{
    const auto style = GENERATE (Style::Instrument, Style::Bus, Style::Drums);
    CAPTURE (static_cast<int> (style));
    host::SessionHost maker;
    maker.edit ("colour", 0.0f);
    maker.edit ("style", styleValue (style));
    host::SessionHost restored;
    host::restore (*restored.processor, host::saved (*maker.processor));
    CHECK (mostApart (gainReductionThrough (*restored.processor), gainReduction (Style::Clean)) > 1.0);
}

TEST_CASE ("A session or a Preset from before Style restores Clean, with every other setting as saved and no defaults written")
{
    const int version = GENERATE (1, 2);
    CAPTURE (version);
    host::SessionHost maker;
    maker.edit ("knee", 20.0f);
    maker.edit ("attack", 0.2f);
    maker.edit ("gate", 1.0f);
    maker.edit ("style", styleValue (Style::Opto)); // removed below, as a state from before Style lacks it
    auto xml = host::savedXml (*maker.processor);
    REQUIRE (xml != nullptr);
    auto* style = xml->getChildByAttribute ("id", "style");
    REQUIRE (style != nullptr);
    xml->removeChildElement (style, true);
    if (version == 1)
    {
        xml->deleteAllChildElementsWithTagName (plugkit::ui::ABCompare::stateType.toString());
        for (const auto* property : { "editorWidth", "editorHeight", "uiScalePercent" })
            xml->removeAttribute (property);
    }
    xml->setAttribute ("version", version);

    host::SessionHost host;
    host::restore (*host.processor, *xml);
    CHECK (host.value ("style") == styleValue (Style::Clean));
    for (const auto& id : host::presetSettingIds (*host.processor))
    {
        CAPTURE (id);
        if (id != "style")
            CHECK (host.value (id) == maker.value (id));
    }

    auto preset = maker.session.presetState().createCopy();
    preset.removeChild (preset.getChildWithProperty ("id", "style"), nullptr);
    host::SessionHost loaded;
    REQUIRE (loaded.session.loadPreset (preset, "Before Style"));
    CHECK (loaded.value ("style") == styleValue (Style::Clean));
    CHECK (loaded.value ("knee") == maker.value ("knee"));
    CHECK (loaded.value ("attack") == maker.value ("attack"));
}
