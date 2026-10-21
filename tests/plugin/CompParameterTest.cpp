// Comp and Comp Follow through the Plugin Shell (ADR 0004): the host parameters, the entry points the editor turns Comp
// Follow on and off through, and how sessions, Presets, A/B and undo keep them.

#include "Host.h"
#include "Measure.h"
#include "ParametersOnly.h"
#include "SessionHost.h"
#include "comp1/Style.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <utility>
#include <vector>

using comp1::Style;
using Catch::Matchers::WithinAbs;
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

// Every host parameter's plain value.
std::map<juce::String, float> values (juce::AudioProcessor& processor)
{
    std::map<juce::String, float> all;
    for (auto* p : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            all[ranged->getParameterID()] = ranged->convertFrom0to1 (ranged->getValue());
    return all;
}

// Records, for each parameter change the host hears, whether it came inside that parameter's gesture.
struct GestureRecorder final : juce::AudioProcessorListener
{
    std::set<int> open;
    std::vector<std::pair<int, bool>> changes; // the parameter's index, and whether its gesture was open
    void audioProcessorParameterChanged (juce::AudioProcessor*, int index, float) override { changes.emplace_back (index, open.contains (index)); }
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int index) override { open.insert (index); }
    void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int index) override { open.erase (index); }
};

// Opto chosen (Comp Follow on, Comp 50%), then Comp moved to 80%, and the stored Threshold, Ratio and Auto Gain moved
// well off the Comp Curve by Host Automation, which the Engine ignores while following.
void followingOffTheCurve (const host::SessionHost& host)
{
    REQUIRE (host.processor->chooseStyle (Style::Opto));
    host.edit ("comp", 80.0f);
    host::set (*host.processor, "threshold", -6.0f);
    host::set (*host.processor, "ratio", 1.5f);
    host::set (*host.processor, "auto_gain", 0.0f);
}

// Seeded noise through processor, mono at 48 kHz, in blocks of 512: before() before each block, given its index.
std::vector<float> play (comp1::PluginProcessor& processor, int blocks, const std::function<void (int)>& before = [] (int) {})
{
    REQUIRE (processor.setBusesLayout (host::layout (juce::AudioChannelSet::mono(), juce::AudioChannelSet::disabled())));
    constexpr int blockSize = 512;
    processor.prepareToPlay (48000.0, blockSize);
    const auto noise = comp1::test::noise (1, blocks * blockSize, 5);
    std::vector<float> output;
    juce::AudioBuffer<float> buffer (1, blockSize);
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b)
    {
        before (b);
        buffer.copyFrom (0, 0, noise[0].data() + b * blockSize, blockSize);
        processor.processBlock (buffer, midi);
        output.insert (output.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
    }
    return output;
}

// Takes the parameter id out of a saved state, wherever it is held: the settings, and each A/B side's.
void removeParameter (juce::XmlElement& xml, const juce::String& id)
{
    while (auto* element = xml.getChildByAttribute ("id", id))
        xml.removeChildElement (element, true);
    for (auto* child : xml.getChildIterator())
        removeParameter (*child, id);
}

// The largest difference between two outputs, sample for sample.
double mostApart (const std::vector<float>& a, const std::vector<float>& b)
{
    REQUIRE (a.size() == b.size());
    double most = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
        most = std::max (most, std::abs (static_cast<double> (a[i]) - b[i]));
    return most;
}

} // namespace

TEST_CASE ("Comp and Comp Follow are automatable host parameters, first among the compressor's, before Threshold")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    auto& comp = host::parameter (processor, "comp");
    auto& compFollow = host::parameter (processor, "comp_follow");
    CHECK (comp.getParameterIndex() == host::parameter (processor, "gate_hold").getParameterIndex() + 1);
    CHECK (compFollow.getParameterIndex() == comp.getParameterIndex() + 1);
    CHECK (host::parameter (processor, "threshold").getParameterIndex() == comp.getParameterIndex() + 2);

    CHECK (comp.getName (100) == "Comp");
    CHECK (comp.getLabel() == "%");
    CHECK (comp.getNormalisableRange().start == 0.0f);
    CHECK (comp.getNormalisableRange().end == 100.0f);
    // Linear: half way is 50%, Clean's default Comp, which is the default.
    CHECK (comp.convertFrom0to1 (0.5f) == 50.0f);
    CHECK (comp.convertFrom0to1 (comp.getDefaultValue()) == 50.0f);
    CHECK (comp.isAutomatable());
    CHECK (comp.getText (comp.convertTo0to1 (37.5f), 100) == "37.5");

    CHECK (compFollow.getName (100) == "Comp Follow");
    CHECK (compFollow.getNumSteps() == 2);
    CHECK (compFollow.getDefaultValue() == 0.0f);
    CHECK (compFollow.isAutomatable());
}

TEST_CASE ("The Settings read hold Comp, Comp Follow and the Style's Comp Curve")
{
    juce::ScopedJuceInitialiser_GUI juce;
    host::ParametersOnly processor;
    const comp1::parameters::SettingsReader reader (processor.parameters);
    CHECK (reader.read().comp == 50.0);
    CHECK_FALSE (reader.read().compFollow);
    CHECK (reader.read().compCurve == comp1::compCurveOf (Style::Clean));
    host::set (processor, "comp", 72.5f);
    host::set (processor, "comp_follow", 1.0f);
    for (int s = 0; s < comp1::styleCount; ++s)
    {
        const auto style = static_cast<Style> (s);
        CAPTURE (s);
        host::set (processor, "style", styleValue (style));
        const auto settings = reader.read();
        CHECK (settings.comp == 72.5);
        CHECK (settings.compFollow);
        CHECK (settings.compCurve == comp1::compCurveOf (style));
    }
}

TEST_CASE ("The followed Threshold and Ratio are the Style's Comp Curve at the current Comp")
{
    host::SessionHost host;
    host.edit ("style", styleValue (Style::FET));
    host.edit ("comp", 100.0f);
    CHECK (host.processor->followedThresholdDb() == -28.0);
    CHECK_THAT (host.processor->followedRatio(), WithinRel (20.0, 1.0e-12));
    host.edit ("comp", 50.0f);
    CHECK (host.processor->followedThresholdDb() == -18.0);
    CHECK (host.processor->followedRatio() == 4.0);
}

TEST_CASE ("Stopping following writes Comp Follow off, the Comp Curve's Threshold and Ratio and Auto Gain on, as one undo step inside gestures, and the output doesn't change")
{
    host::SessionHost host;
    followingOffTheCurve (host);
    const auto before = values (*host.processor);
    const int steps = host.history.undoSteps();
    // Opto's Comp Curve at 80%: -32.4 dB, 4.29:1 (python3 tools/dyn-lab/dynlab.py compcurve --style opto).
    const double threshold = host.processor->followedThresholdDb(), ratio = host.processor->followedRatio();
    CHECK_THAT (threshold, WithinAbs (-32.4, 1.0e-9));
    CHECK_THAT (ratio, WithinAbs (4.29, 0.005));

    GestureRecorder recorder;
    host.processor->addListener (&recorder);
    REQUIRE (host.processor->stopFollowingComp());
    host.processor->removeListener (&recorder);

    const auto after = values (*host.processor);
    for (const auto& [id, value] : after)
    {
        CAPTURE (id);
        if (id == "comp_follow")
            CHECK (value == 0.0f);
        else if (id == "threshold")
            CHECK (value == asStored (*host.processor, id, threshold));
        else if (id == "ratio")
            CHECK (value == asStored (*host.processor, id, ratio));
        else if (id == "auto_gain")
            CHECK (value == 1.0f);
        else
            CHECK (value == before.at (id));
    }
    CHECK (host.history.undoSteps() == steps + 1);
    REQUIRE (recorder.changes.size() == 4);
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

TEST_CASE ("After stopping following, the output is what following would have played, within the float rounding of the written parameters")
{
    // Following throughout, against stopping following 0.3 s in.
    const auto playStopping = [] (bool stop) {
        host::SessionHost host;
        followingOffTheCurve (host);
        return play (*host.processor, 94, [&host, stop] (int block) {
            if (stop && block == 28)
                REQUIRE (host.processor->stopFollowingComp());
        });
    };
    const auto following = playStopping (false);
    const auto stopped = playStopping (true);
    // The noise peaks at -6 dBFS; the stored Threshold and Ratio, as floats, move the Gain Reduction by far less than
    // 0.001 dB.
    CHECK (mostApart (following, stopped) < 1.0e-4);
    // Without stopFollowingComp()'s writes, switching Comp Follow off plays the stored settings: far from it.
    const auto switchedOff = [] {
        host::SessionHost host;
        followingOffTheCurve (host);
        return play (*host.processor, 94, [&host] (int block) {
            if (block == 28)
                host::set (*host.processor, "comp_follow", 0.0f);
        });
    }();
    CHECK (mostApart (following, switchedOff) > 0.01);
}

TEST_CASE ("Following writes Comp at the position nearest the stored Threshold, clamped to the Comp Curve, and Comp Follow on, as one undo step inside gestures")
{
    // Clean's Comp Curve: 0 dB at 0%, -18 dB at 50%, -36 dB at 100%.
    const auto [threshold, comp] = GENERATE (table<float, float> ({ { -9.0f, 25.0f }, { -27.0f, 75.0f }, { -50.0f, 100.0f }, { 0.0f, 0.0f } }));
    CAPTURE (threshold);
    host::SessionHost host;
    host.edit ("threshold", threshold);
    host.edit ("ratio", 2.0f);
    const auto before = values (*host.processor);
    const int steps = host.history.undoSteps();

    GestureRecorder recorder;
    host.processor->addListener (&recorder);
    REQUIRE (host.processor->followComp());
    host.processor->removeListener (&recorder);

    const auto after = values (*host.processor);
    for (const auto& [id, value] : after)
    {
        CAPTURE (id);
        if (id == "comp")
            CHECK_THAT (value, WithinAbs (comp, 1.0e-4));
        else if (id == "comp_follow")
            CHECK (value == 1.0f);
        else
            CHECK (value == before.at (id)); // the stored Threshold and Ratio stay, for stopping following to replace
    }
    CHECK (host.history.undoSteps() == steps + 1);
    REQUIRE_FALSE (recorder.changes.empty());
    for (const auto& [index, inGesture] : recorder.changes)
        CHECK (inGesture);

    host.history.undo();
    CHECK (values (*host.processor) == before);
    host.history.redo();
    CHECK (values (*host.processor) == after);
}

TEST_CASE ("Each Comp Follow entry point changes nothing and returns false where it doesn't apply")
{
    host::SessionHost host;
    host.edit ("threshold", -30.0f);
    auto before = values (*host.processor);
    int steps = host.history.undoSteps();
    CHECK_FALSE (host.processor->stopFollowingComp());
    CHECK (values (*host.processor) == before);
    CHECK (host.history.undoSteps() == steps);

    REQUIRE (host.processor->followComp());
    before = values (*host.processor);
    steps = host.history.undoSteps();
    CHECK_FALSE (host.processor->followComp());
    CHECK (values (*host.processor) == before);
    CHECK (host.history.undoSteps() == steps);
}

TEST_CASE ("Inside the editor's own transaction, stopping following and the Threshold drag that follows are one undo step")
{
    host::SessionHost host;
    REQUIRE (host.processor->chooseStyle (Style::VCA));
    const auto before = values (*host.processor);
    const int steps = host.history.undoSteps();
    host.history.beginTransaction();
    REQUIRE (host.processor->stopFollowingComp());
    host.edit ("threshold", -25.0f);
    host.edit ("threshold", -30.0f);
    host.history.endTransaction();
    CHECK (host.value ("comp_follow") == 0.0f);
    CHECK (host.value ("threshold") == -30.0f);
    CHECK (host.history.undoSteps() == steps + 1);
    host.history.undo();
    CHECK (values (*host.processor) == before);
}

TEST_CASE ("Inside the editor's own transaction, following and the Comp drag that follows are one undo step")
{
    host::SessionHost host;
    host.edit ("threshold", -30.0f);
    const auto before = values (*host.processor);
    const int steps = host.history.undoSteps();
    host.history.beginTransaction();
    REQUIRE (host.processor->followComp());
    host.edit ("comp", 60.0f);
    host.edit ("comp", 70.0f);
    host.history.endTransaction();
    CHECK (host.value ("comp_follow") == 1.0f);
    CHECK (host.value ("comp") == 70.0f);
    CHECK (host.history.undoSteps() == steps + 1);
    host.history.undo();
    CHECK (values (*host.processor) == before);
}

TEST_CASE ("Host Automation of Comp Follow, on and off mid-glide with Comp moving, plays the same twice")
{
    const auto automated = [] {
        host::SessionHost host;
        REQUIRE (host.processor->chooseStyle (Style::FET));
        host::set (*host.processor, "comp_follow", 0.0f);
        host::set (*host.processor, "threshold", -40.0f);
        return play (*host.processor, 64, [&host] (int block) {
            host::set (*host.processor, "comp_follow", (block / 12) % 2 == 1 ? 1.0f : 0.0f);
            host::set (*host.processor, "comp", static_cast<float> (40 + block % 50));
        });
    };
    const auto first = automated();
    CHECK (automated() == first);
}

TEST_CASE ("While following, Host Automation of Threshold, Ratio and Auto Gain has no effect")
{
    // FET chosen, following from the start, Comp moving; from block 20 on, a glide's worth after any start, the stored
    // settings move every block too.
    const auto automated = [] (bool moveStored) {
        host::SessionHost host;
        REQUIRE (host.processor->chooseStyle (Style::FET));
        return play (*host.processor, 64, [&host, moveStored] (int block) {
            host::set (*host.processor, "comp", static_cast<float> (30 + block));
            if (moveStored && block >= 20)
            {
                host::set (*host.processor, "threshold", static_cast<float> (-(block % 60)));
                host::set (*host.processor, "ratio", 1.0f + static_cast<float> (block % 7));
                host::set (*host.processor, "auto_gain", static_cast<float> (block % 2));
            }
        });
    };
    CHECK (automated (true) == automated (false));
}

TEST_CASE ("Comp and Comp Follow round-trip through sessions, Presets and A/B, and save, load and save again gives the same bytes")
{
    host::SessionHost maker;
    REQUIRE (maker.processor->chooseStyle (Style::Opto));
    maker.edit ("comp", 63.5f);
    const auto first = host::saved (*maker.processor);

    host::SessionHost restored;
    host::restore (*restored.processor, first);
    CHECK (restored.value ("comp") == 63.5f);
    CHECK (restored.value ("comp_follow") == 1.0f);
    CHECK (host::saved (*restored.processor) == first);

    host::SessionHost loaded;
    REQUIRE (loaded.session.loadPreset (maker.session.presetState(), "Following"));
    CHECK (loaded.value ("comp") == 63.5f);
    CHECK (loaded.value ("comp_follow") == 1.0f);

    // B stops following at another Comp; A keeps following.
    maker.session.selectCompareSide (CompareSide::B);
    maker.edit ("comp", 20.0f);
    REQUIRE (maker.processor->stopFollowingComp());
    maker.session.selectCompareSide (CompareSide::A);
    CHECK (maker.value ("comp") == 63.5f);
    CHECK (maker.value ("comp_follow") == 1.0f);
    maker.session.selectCompareSide (CompareSide::B);
    CHECK (maker.value ("comp") == 20.0f);
    CHECK (maker.value ("comp_follow") == 0.0f);
}

TEST_CASE ("A session or Preset without Comp and Comp Follow restores Comp Follow off and Comp at its default, and plays as saved")
{
    const int version = GENERATE (1, 2);
    CAPTURE (version);
    host::SessionHost maker;
    host::set (*maker.processor, "threshold", -40.0f);
    host::set (*maker.processor, "ratio", 8.0f);
    const auto played = play (*maker.processor, 16);
    // Following Comp somewhere else, removed below, as a state from before them lacks both.
    maker.edit ("comp", 90.0f);
    maker.edit ("comp_follow", 1.0f);
    auto xml = host::savedXml (*maker.processor);
    REQUIRE (xml != nullptr);
    for (const auto* id : { "comp", "comp_follow" })
        removeParameter (*xml, id);
    if (version == 1)
    {
        xml->deleteAllChildElementsWithTagName (plugkit::ui::ABCompare::stateType.toString());
        for (const auto* property : { "editorWidth", "editorHeight", "uiScalePercent" })
            xml->removeAttribute (property);
    }
    xml->setAttribute ("version", version);

    host::SessionHost host;
    host::restore (*host.processor, *xml);
    CHECK (host.value ("comp_follow") == 0.0f);
    CHECK (host.value ("comp") == 50.0f);
    CHECK (host.value ("threshold") == -40.0f);
    CHECK (play (*host.processor, 16) == played);

    auto preset = maker.session.presetState().createCopy();
    for (const auto* id : { "comp", "comp_follow" })
        preset.removeChild (preset.getChildWithProperty ("id", id), nullptr);
    host::SessionHost loaded;
    REQUIRE (loaded.session.loadPreset (preset, "Before Comp"));
    CHECK (loaded.value ("comp_follow") == 0.0f);
    CHECK (loaded.value ("comp") == 50.0f);
    CHECK (loaded.value ("threshold") == -40.0f);
}
