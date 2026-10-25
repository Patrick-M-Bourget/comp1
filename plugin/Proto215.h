#pragma once

// PROTOTYPE #215, throwaway, never to merge: Attack and Release grabbers on the Level and Gain Reduction Graph.
// Candidate A (freeze and grab the trace) and candidate B (a Timing Response inset), switchable from a bar in the
// display's tab row, with a test signal so the Standalone has something to compress. See
// docs/research/attack-release-on-the-level-graph.md.

#include "LevelGraph.h"
#include "PluginProcessor.h"
#include "TransferCurveInset.h"
#include "comp1/Engine.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace comp1::proto215
{

enum class Candidate { a, b, off };
enum class Freeze { hover, press, never };
enum class DragScale { knob, graph };
enum class AutoReleaseLook { band, curve };
enum class Place { beside, below, swap };

struct State
{
    Candidate candidate = Candidate::a;
    Freeze freeze = Freeze::hover;
    DragScale dragScale = DragScale::knob;
    AutoReleaseLook autoReleaseLook = AutoReleaseLook::band;
    Place place = Place::beside;
    std::function<void()> changed;
};

// A host parameter written through a hidden slider, as the inset's Knee is: one undo step per begin/end.
class ParamEdit
{
public:
    explicit ParamEdit (juce::RangedAudioParameter& p);
    double value() const { return slider.getValue(); }
    double travel() const { return param.convertTo0to1 (static_cast<float> (slider.getValue())); }
    void set (double plain);
    void setTravel (double t);
    void begin();
    void end() { edit.reset(); }
    juce::RangedAudioParameter& param;

private:
    juce::Slider slider;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
    std::optional<juce::Slider::ScopedDragNotification> edit;
};

// An offline Engine: the real compressor run on a synthetic signal, read back as Gain Reduction per 16-sample run.
class Replay
{
public:
    // Plays a run of each level in inputDb (oldest first) as a tone whose peak in that run is that level; returns the
    // Gain Reduction per run, oldest first.
    std::vector<float> levels (Settings settings, double rate, const std::vector<float>& inputDb);
    // A burst at levelDb: preRuns of silence, onRuns of the burst, postRuns of silence. Gain Reduction per run.
    std::vector<float> burst (Settings settings, double rate, double levelDb, int preRuns, int onRuns, int postRuns);

private:
    std::vector<float> play (Settings settings, double rate, int runs, const std::function<double (int)>& levelOfRun);
    Engine engine;
};

// Candidate A: over the Level graph, taking the mouse; freezes the graph and puts handles on the newest event.
class OverlayA final : public juce::Component, private juce::Timer
{
public:
    OverlayA (LevelGraph& graph, PluginProcessor& processor, State& state);
    ~OverlayA() override;

    void paint (juce::Graphics& g) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void visibilityChanged() override;

    // For the PNG renders: freeze as if hovered, and a drag of a handle to a value without the mouse.
    bool forceFreeze = false;
    bool debugDrag (bool attackHandle, double plain);
    void debugLetGo();
    juce::String infoText() const;

private:
    struct Event
    {
        int start = 0, peak = 0, end = 0; // columns from the right edge: start the oldest
        float peakDb = 0.0f, baseDb = 0.0f;
        juce::Point<float> attack, release;
    };
    enum class Which { attack, release };
    struct Drag
    {
        Which which;
        double from;
        float x;
    };

    void timerCallback() override;
    std::vector<float> columnsOf (const std::vector<float>& perRunOldestFirst) const;
    std::vector<float> liveColumns() const;
    std::optional<Event> findEvent (const std::vector<float>& cols) const;
    std::optional<Event> findAround (const std::vector<float>& cols, const Event& near) const;
    juce::Point<float> pointAt (const std::vector<float>& cols, int c) const;
    void replayGhost();
    ParamEdit& editOf (Which w) { return w == Which::attack ? attack : release; }

    LevelGraph& graph;
    PluginProcessor& processor;
    State& state;
    ParamEdit attack, release;
    Replay replay;
    std::vector<float> cols, ghost;
    std::optional<Event> event, ghostEvent, anchorEvent;
    std::optional<Drag> drag;
    std::optional<double> replayErrorDb;
    std::optional<Which> hovered;
    Settings lastGhostSettings;
    bool wasFrozen = false;
};

// Candidate B: the Timing Response inset, a test burst's Gain Reduction on a log time axis, Attack on the left half
// and Release on the right, each with a horizontal handle.
class InsetB final : public juce::Component, private juce::Timer
{
public:
    InsetB (PluginProcessor& processor, State& state);

    static constexpr int width = 260, height = 160;

    void paint (juce::Graphics& g) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& key) override;
    bool keyStateChanged (bool isKeyDown) override;
    void focusLost (FocusChangeType) override;

private:
    enum class Which { attack, release };
    void timerCallback() override;
    void simulate (const Settings& s);
    juce::Rectangle<float> plot() const;
    float xAttack (double ms) const;
    float xRelease (double ms) const;
    double attackAtX (float x) const;
    double releaseAtX (float x) const;
    float yOf (double db) const;
    double attackCurveAt (double ms) const;
    double releaseCurveAt (double ms) const;
    juce::Point<float> handleAt (Which w) const;
    std::optional<Which> handleUnder (juce::Point<float> p) const;
    ParamEdit& editOf (Which w) { return w == Which::attack ? attack : release; }

    PluginProcessor& processor;
    State& state;
    ParamEdit attack, release;
    Replay replay;
    std::optional<Settings> simulated;
    // Gain Reduction per run after the onset (attack) and after the burst's end (release), and their runs' lengths.
    std::vector<float> rise, fall;
    double riseRunMs = 0.0, fallRunMs = 0.0, peakDb = 1.0, endDb = 0.0, steadyDb = 0.0;
    Character character = Character::Clean;
    bool autoRelease = false;
    std::optional<Which> hovered;
    struct Drag
    {
        Which which;
        float x;
        double from;
    };
    std::optional<Drag> drag;
    bool heldKey = false;
};

// The switch bar in the display's tab row.
class Bar final : public juce::Component
{
public:
    Bar (State& state, PluginProcessor& processor);
    void resized() override;
    // Shows the state's choices, after something other than the bar changed them.
    void sync();

private:
    State& state;
    juce::ComboBox candidate, freeze, dragScale, autoRelease, place, signal;
};

} // namespace comp1::proto215
