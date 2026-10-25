// PROTOTYPE #215, throwaway, never to merge. See Proto215.h.

#include "Proto215.h"

#include "Controls.h"
#include "HandlePainting.h"
#include "Parameters.h"
#include "plugkit/RunGrid.h"
#include "plugkit/ui/staple/Tokens.h"

#include <algorithm>
#include <cmath>

namespace comp1::proto215
{

namespace
{
namespace colour = staple::tokens::colour;
namespace knob = staple::tokens::knob;
constexpr float handleRadius = 6.0f, hitRadius = 10.0f;
constexpr int runSamples = plugkit::runSamples;

double dbToGain (double db) { return db <= -120.0 ? 0.0 : std::pow (10.0, db / 20.0); }

void textAt (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Colour c, float size,
             juce::Justification j = juce::Justification::centredLeft)
{
    g.setColour (c);
    g.setFont (juce::FontOptions (size));
    g.drawText (text, r, j, false);
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------------

ParamEdit::ParamEdit (juce::RangedAudioParameter& p) : param (p)
{
    controls::showRangeOf (slider, p);
    attachment = std::make_unique<juce::SliderParameterAttachment> (p, slider);
}

void ParamEdit::set (double plain)
{
    const auto range = param.getNormalisableRange();
    slider.setValue (std::clamp (plain, static_cast<double> (range.start), static_cast<double> (range.end)), juce::sendNotificationSync);
}

void ParamEdit::setTravel (double t) { slider.setValue (param.convertFrom0to1 (static_cast<float> (std::clamp (t, 0.0, 1.0))), juce::sendNotificationSync); }

void ParamEdit::begin()
{
    if (! edit)
        edit.emplace (slider);
}

// ---------------------------------------------------------------------------------------------------------------------

std::vector<float> Replay::play (Settings s, double rate, int runs, const std::function<double (int)>& levelOfRun)
{
    s.globalBypass = false;
    s.detectionAudition = false;
    s.gate.on = false;
    s.limiter = false;
    s.autoGain = false;
    constexpr int block = 32 * runSamples;
    engine.prepare (rate, block, 2);
    engine.setSettings (s);
    std::vector<float> left (block), right (block);
    std::vector<LevelRecord> records (static_cast<size_t> (std::max (64, engine.levelHistoryCapacity())));
    std::vector<float> out;
    out.reserve (static_cast<size_t> (runs));
    const double twoPi = juce::MathConstants<double>::twoPi;
    for (int first = 0; first < runs; first += 32)
    {
        const int n = std::min (32, runs - first);
        for (int r = 0; r < n; ++r)
        {
            const double a = dbToGain (levelOfRun (first + r));
            for (int j = 0; j < runSamples; ++j)
            {
                // A tone of 16 samples' period: every run holds its peak.
                const auto x = static_cast<float> (a * std::sin (twoPi * j / runSamples));
                left[static_cast<size_t> (r * runSamples + j)] = x;
                right[static_cast<size_t> (r * runSamples + j)] = x;
            }
        }
        float* channels[] { left.data(), right.data() };
        engine.process ({ channels, 2, n * runSamples });
        for (int got; (got = engine.readLevels (records.data(), static_cast<int> (records.size()))) > 0;)
            for (int i = 0; i < got; ++i)
                out.push_back (static_cast<float> (records[static_cast<size_t> (i)].gainReductionDb));
    }
    out.resize (static_cast<size_t> (runs), out.empty() ? 0.0f : out.back());
    return out;
}

std::vector<float> Replay::levels (Settings s, double rate, const std::vector<float>& inputDb)
{
    return play (s, rate, static_cast<int> (inputDb.size()), [&] (int r) { return static_cast<double> (inputDb[static_cast<size_t> (r)]); });
}

std::vector<float> Replay::burst (Settings s, double rate, double levelDb, int pre, int on, int post)
{
    return play (s, rate, pre + on + post, [=] (int r) { return r >= pre && r < pre + on ? levelDb : -200.0; });
}

// ---------------------------------------------------------------------------------------------------------------------
// Candidate A

OverlayA::OverlayA (LevelGraph& g, PluginProcessor& p, State& s)
    : graph (g), processor (p), state (s), attack (parameters::parameterOf (p.parameterState(), parameters::attackId)),
      release (parameters::parameterOf (p.parameterState(), parameters::releaseId))
{
    setName ("Proto A overlay");
    setInterceptsMouseClicks (true, false);
    setWantsKeyboardFocus (false);
    startTimerHz (30);
}

OverlayA::~OverlayA() { graph.protoFreeze (false); }

void OverlayA::visibilityChanged()
{
    if (! isVisible())
    {
        graph.protoFreeze (false);
        wasFrozen = false;
    }
}

std::vector<float> OverlayA::columnsOf (const std::vector<float>& perRun) const
{
    // perRun is oldest first, as many runs as the graph holds; columns from the right edge, each the most of its runs.
    const int width = std::max (1, getWidth());
    const int held = static_cast<int> (perRun.size());
    const double per = graph.protoRunsPerColumn();
    std::vector<float> out (static_cast<size_t> (width), 0.0f);
    for (int c = 0; c < width; ++c)
    {
        const int from = static_cast<int> (std::ceil (c * per)), to = std::min (held, static_cast<int> (std::ceil ((c + 1) * per)));
        float most = 0.0f;
        for (int ago = from; ago < to; ++ago)
            most = std::max (most, perRun[static_cast<size_t> (held - 1 - ago)]);
        out[static_cast<size_t> (c)] = std::min (most, static_cast<float> (LevelGraph::scaleDb));
    }
    return out;
}

std::vector<float> OverlayA::liveColumns() const
{
    std::vector<float> out (static_cast<size_t> (std::max (1, getWidth())));
    for (int c = 0; c < static_cast<int> (out.size()); ++c)
        out[static_cast<size_t> (c)] = static_cast<float> (graph.protoColumnReduction (c));
    return out;
}

juce::Point<float> OverlayA::pointAt (const std::vector<float>& c, int column) const
{
    const auto h = static_cast<float> (getHeight());
    return { static_cast<float> (getWidth()) - static_cast<float> (column) - 0.5f,
             std::clamp (c[static_cast<size_t> (column)] / static_cast<float> (LevelGraph::scaleDb), 0.0f, 1.0f) * h };
}

std::optional<OverlayA::Event> OverlayA::findEvent (const std::vector<float>& c) const
{
    // The newest finished event, kept clear of the insets over the oldest audio: a peak that rose at least 3 dB from
    // the valley before it (older), whose recovery fell back 63% of that rise before the next rise or the newest audio.
    const int n = static_cast<int> (c.size()), limit = n - 300;
    const auto at = [&] (int k) { return c[static_cast<size_t> (std::clamp (k, 0, n - 1))]; };
    for (int p = 2; p < limit; ++p)
    {
        if (! (at (p) >= at (p - 1) && at (p) >= at (p + 1) && at (p) >= at (p - 2) && at (p) >= at (p + 2)))
            continue;
        int start = p;
        while (start + 1 < n && at (start + 1) <= at (start) + 0.05f)
            ++start;
        const float base = at (start), rise = at (p) - base;
        if (rise < 3.0f || start >= limit)
            continue;
        int rel = -1;
        for (int k = p; k >= 0; --k)
        {
            if (at (k) < base + 0.37f * rise)
            {
                rel = k;
                break;
            }
            if (k < p - 2 && at (k) > at (k + 1) + 0.5f)
                break; // the next event began first
        }
        if (rel < 1)
            continue;
        while (start > p && at (start) <= base + 0.1f * rise)
            --start;
        ++start;
        int att = p;
        for (int k = start; k >= p; --k)
            if (at (k) >= base + 0.63f * rise)
            {
                att = k;
                break;
            }
        Event e;
        e.start = start;
        e.peak = p;
        e.end = rel;
        e.peakDb = at (p);
        e.baseDb = base;
        e.attack = pointAt (c, att);
        e.release = pointAt (c, rel);
        return e;
    }
    return std::nullopt;
}

std::optional<OverlayA::Event> OverlayA::findAround (const std::vector<float>& c, const Event& near) const
{
    // The same event in the ghost: from its valley, the peak and the 63% and 37% points, the recovery free to run
    // right (newer) as far as the newest audio.
    const int n = static_cast<int> (c.size());
    const auto at = [&] (int k) { return c[static_cast<size_t> (std::clamp (k, 0, n - 1))]; };
    Event e = near;
    e.peak = near.start;
    for (int k = near.start; k >= std::max (0, near.peak - 40); --k)
        if (at (k) > at (e.peak))
            e.peak = k;
    const float base = at (near.start), rise = at (e.peak) - base;
    e.peakDb = at (e.peak);
    e.baseDb = base;
    if (rise < 0.3f)
        return std::nullopt;
    int att = e.peak;
    for (int k = near.start; k >= e.peak; --k)
        if (at (k) >= base + 0.63f * rise)
        {
            att = k;
            break;
        }
    int rel = 0;
    for (int k = e.peak; k >= 0; --k)
        if (at (k) < base + 0.37f * rise)
        {
            rel = k;
            break;
        }
    e.end = rel;
    e.attack = pointAt (c, att);
    e.release = pointAt (c, rel);
    return e;
}

void OverlayA::replayGhost()
{
    const int held = graph.protoHeld();
    if (held <= 0)
        return;
    std::vector<float> inputs (static_cast<size_t> (held));
    for (int ago = 0; ago < held; ++ago)
        inputs[static_cast<size_t> (held - 1 - ago)] = static_cast<float> (graph.protoInputDb (ago));
    lastGhostSettings = processor.protoSettings();
    ghost = columnsOf (replay.levels (lastGhostSettings, graph.protoSampleRate(), inputs));
}

void OverlayA::timerCallback()
{
    if (! isShowing())
        return;
    const bool freeze = forceFreeze || (state.freeze == Freeze::hover && (isMouseOver() || drag.has_value()))
                        || (state.freeze == Freeze::press && drag.has_value());
    graph.protoFreeze (freeze);
    if (freeze && ! wasFrozen)
    {
        // Test 5: how far the replay at the settings as they are is from what the Engine did.
        cols = liveColumns();
        replayGhost();
        double sum = 0.0;
        int n = 0;
        for (size_t c = 0; c < cols.size(); ++c)
            if (std::max (cols[c], ghost[c]) > 0.5f)
            {
                sum += std::abs (cols[c] - ghost[c]);
                ++n;
            }
        replayErrorDb = n > 0 ? std::optional<double> (sum / n) : std::nullopt;
    }
    wasFrozen = freeze;
    if (! freeze)
        ghost.clear();

    cols = liveColumns();
    if (drag)
    {
        // The ghost follows the settings the drag wrote; the handles sit on it.
        if (! (processor.protoSettings() == lastGhostSettings) || ghost.empty())
            replayGhost();
        // Not frozen: the event scrolls away under the handle (test 2), so it is found again each time.
        if (! freeze || ! anchorEvent)
            anchorEvent = findEvent (cols);
        ghostEvent = anchorEvent ? findAround (ghost, *anchorEvent) : std::nullopt;
        event = anchorEvent;
    }
    else
    {
        event = findEvent (cols);
        ghostEvent.reset();
    }
    repaint();
}

void OverlayA::paint (juce::Graphics& g)
{
    const auto w = static_cast<float> (getWidth()), h = static_cast<float> (getHeight());
    if (! ghost.empty() && drag)
    {
        juce::Path p;
        for (int c = 0; c < static_cast<int> (ghost.size()); ++c)
        {
            const auto pt = pointAt (ghost, c);
            c == 0 ? p.startNewSubPath (pt) : p.lineTo (pt);
        }
        g.setColour (colour::anSc.withAlpha (0.9f));
        juce::Path dashed;
        const float dashes[] { 4.0f, 3.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, p, dashes, 2);
        g.fillPath (dashed);
    }

    const auto shown = ghostEvent ? ghostEvent : event;
    if (shown)
    {
        // The event's span, faintly.
        const float left = w - static_cast<float> (event->start) - 1.0f, right = w - static_cast<float> (event->end);
        g.setColour (colour::fill1);
        g.fillRect (juce::Rectangle<float> (left, 0.0f, right - left, h));
        for (const auto which : { Which::attack, Which::release })
        {
            const auto at = which == Which::attack ? shown->attack : shown->release;
            handles::paint (g, at, { .radius = handleRadius, .selected = drag && drag->which == which, .hovered = hovered == which, .alpha = 1.0f });
        }
        if (drag)
        {
            const auto& e = editOf (drag->which);
            const auto at = drag->which == Which::attack ? shown->attack : shown->release;
            handles::paintReadout (g, at, handleRadius, w, drag->which == Which::attack ? "Attack" : "Release",
                                   controls::spokenValue (e.param, e.value()));
        }
    }

    textAt (g, infoText(), { 8.0f, h - 20.0f, w - 60.0f, 16.0f }, colour::text2, 11.0f);
}

juce::String OverlayA::infoText() const
{
    const auto w = static_cast<float> (getWidth());
    // The prototype's own state, bottom left, clear of the inset.
    juce::String info = "A  freeze: ";
    info << (state.freeze == Freeze::hover ? "on hover" : state.freeze == Freeze::press ? "on press" : "never")
         << (graph.protoFrozen() ? " (FROZEN)" : " (live)") << "   drag scale: " << (state.dragScale == DragScale::knob ? "knob travel" : "graph time")
         << "   " << juce::String (graph.protoRunsPerColumn() * runSamples / graph.protoSampleRate() * 1000.0, 2) << " ms/px";
    if (event)
    {
        const double msPerPx = graph.protoRunsPerColumn() * runSamples / graph.protoSampleRate() * 1000.0;
        const float px = std::abs (event->attack.x - (w - static_cast<float> (event->start) - 0.5f));
        info << "   event rise " << juce::String (event->peakDb - event->baseDb, 1) << " dB, onset(10%) to 63%: " << juce::String (px, 0)
             << " px = " << juce::String (px * msPerPx, 1) << " ms, attack-release handle gap " << juce::String (event->release.x - event->attack.x, 0) << " px";
    }
    else
        info << "   no finished event in view";
    if (replayErrorDb)
        info << "   replay vs Engine: " << juce::String (*replayErrorDb, 2) << " dB mean |diff|";
    return info;
}

void OverlayA::mouseMove (const juce::MouseEvent& e)
{
    std::optional<Which> now;
    if (event)
    {
        if (e.position.getDistanceFrom (event->attack) <= hitRadius)
            now = Which::attack;
        else if (e.position.getDistanceFrom (event->release) <= hitRadius)
            now = Which::release;
    }
    if (now != hovered)
    {
        hovered = now;
        setMouseCursor (now ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void OverlayA::mouseExit (const juce::MouseEvent&)
{
    hovered.reset();
    repaint();
}

void OverlayA::mouseDown (const juce::MouseEvent& e)
{
    mouseMove (e);
    if (! hovered || ! event)
        return;
    auto& edit = editOf (*hovered);
    edit.begin();
    drag = Drag { *hovered, state.dragScale == DragScale::knob ? edit.travel() : edit.value(), e.position.x };
    anchorEvent = event;
    ghost.clear();
}

void OverlayA::mouseDrag (const juce::MouseEvent& e)
{
    if (! drag)
        return;
    auto& edit = editOf (drag->which);
    const double speed = e.mods.isShiftDown() ? knob::dragPixels / knob::fineDragPixels : 1.0;
    const double dx = (e.position.x - drag->x) * speed;
    if (state.dragScale == DragScale::knob)
        edit.setTravel (drag->from + dx / knob::dragPixels);
    else
        // The graph's own time scale: a pixel is a column's milliseconds.
        edit.set (drag->from + dx * graph.protoRunsPerColumn() * runSamples / graph.protoSampleRate() * 1000.0);
}

bool OverlayA::debugDrag (bool attackHandle, double plain)
{
    if (! drag)
    {
        if (! event)
            return false;
        const auto which = attackHandle ? Which::attack : Which::release;
        editOf (which).begin();
        drag = Drag { which, editOf (which).value(), 0.0f };
        anchorEvent = event;
        ghost.clear();
    }
    editOf (drag->which).set (plain);
    return true;
}

void OverlayA::mouseUp (const juce::MouseEvent&) { debugLetGo(); }

void OverlayA::debugLetGo()
{
    if (! drag)
        return;
    editOf (drag->which).end();
    drag.reset();
    anchorEvent.reset();
    ghostEvent.reset();
}

// ---------------------------------------------------------------------------------------------------------------------
// Candidate B

namespace
{
constexpr double attackLow = 0.005, attackHigh = 1000.0, releaseLow = 1.0, releaseHigh = 10000.0;
constexpr double attackRate = 192000.0, releaseRate = 44100.0;
constexpr float halfGap = 10.0f, attackShare = 0.42f;
} // namespace

InsetB::InsetB (PluginProcessor& p, State& s)
    : processor (p), state (s), attack (parameters::parameterOf (p.parameterState(), parameters::attackId)),
      release (parameters::parameterOf (p.parameterState(), parameters::releaseId))
{
    setName ("Timing Response inset");
    setTitle ("Timing Response inset");
    setWantsKeyboardFocus (true);
    setSize (width, height);
    startTimerHz (30);
}

juce::Rectangle<float> InsetB::plot() const { return getLocalBounds().toFloat().reduced (10.0f, 20.0f).withTrimmedBottom (-6.0f); }

float InsetB::xAttack (double ms) const
{
    const auto r = plot();
    const double f = (std::log10 (std::clamp (ms, attackLow, attackHigh)) - std::log10 (attackLow)) / (std::log10 (attackHigh) - std::log10 (attackLow));
    return r.getX() + static_cast<float> (f) * r.getWidth() * attackShare;
}

float InsetB::xRelease (double ms) const
{
    const auto r = plot();
    const float x0 = r.getX() + r.getWidth() * attackShare + halfGap, span = r.getRight() - x0;
    const double f = (std::log10 (std::clamp (ms, releaseLow, releaseHigh)) - std::log10 (releaseLow)) / (std::log10 (releaseHigh) - std::log10 (releaseLow));
    return x0 + static_cast<float> (f) * span;
}

double InsetB::attackAtX (float x) const
{
    const auto r = plot();
    const double f = (x - r.getX()) / (r.getWidth() * attackShare);
    return std::pow (10.0, std::log10 (attackLow) + f * (std::log10 (attackHigh) - std::log10 (attackLow)));
}

double InsetB::releaseAtX (float x) const
{
    const auto r = plot();
    const float x0 = r.getX() + r.getWidth() * attackShare + halfGap, span = r.getRight() - x0;
    const double f = (x - x0) / span;
    return std::pow (10.0, std::log10 (releaseLow) + f * (std::log10 (releaseHigh) - std::log10 (releaseLow)));
}

float InsetB::yOf (double db) const
{
    const auto r = plot();
    return r.getY() + static_cast<float> (std::clamp (db / (peakDb * 1.1), 0.0, 1.0)) * r.getHeight();
}

double InsetB::attackCurveAt (double ms) const
{
    if (rise.empty())
        return 0.0;
    const double k = ms / riseRunMs - 1.0;
    const int i = std::clamp (static_cast<int> (std::floor (k)), 0, static_cast<int> (rise.size()) - 2);
    const double f = std::clamp (k - i, 0.0, 1.0);
    return k < 0.0 ? rise[0] * std::max (0.0, ms / riseRunMs) : rise[static_cast<size_t> (i)] * (1.0 - f) + rise[static_cast<size_t> (i + 1)] * f;
}

double InsetB::releaseCurveAt (double ms) const
{
    if (fall.empty())
        return 0.0;
    const double k = ms / fallRunMs - 1.0;
    const int i = std::clamp (static_cast<int> (std::floor (k)), 0, static_cast<int> (fall.size()) - 2);
    const double f = std::clamp (k - i, 0.0, 1.0);
    return k < 0.0 ? endDb : fall[static_cast<size_t> (i)] * (1.0 - f) + fall[static_cast<size_t> (i + 1)] * f;
}

void InsetB::simulate (const Settings& s)
{
    // A test burst Threshold + 20 dB (at most 0 dBFS), one second long: the rise at 192 kHz for resolution, the
    // recovery at 44.1 kHz for length. Zero Latency, so the onset is at the start of its run.
    auto run = s;
    run.processingMode = ProcessingMode::ZeroLatency;
    const double level = std::min (0.0, processor.playedCurve().thresholdDb + 20.0);
    const int preA = 64, onA = static_cast<int> (0.5 * attackRate / runSamples);
    auto a = replay.burst (run, attackRate, level, preA, onA, 0);
    rise.assign (a.begin() + preA, a.end());
    riseRunMs = runSamples / attackRate * 1000.0;
    const int preR = 64, onR = static_cast<int> (1.0 * releaseRate / runSamples), postR = static_cast<int> (10.5 * releaseRate / runSamples);
    auto r = replay.burst (run, releaseRate, level, preR, onR, postR);
    endDb = r[static_cast<size_t> (preR + onR - 1)];
    fall.assign (r.begin() + preR + onR, r.end());
    fallRunMs = runSamples / releaseRate * 1000.0;
    steadyDb = rise.empty() ? 0.0 : rise.back();
    peakDb = std::max ({ 1.0, steadyDb, endDb, static_cast<double> (*std::max_element (a.begin(), a.end())) });
    character = s.character;
    autoRelease = s.autoRelease;
}

void InsetB::timerCallback()
{
    if (! isShowing())
        return;
    auto s = processor.protoSettings();
    // Only what shapes the curve.
    s.globalBypass = false;
    s.outputGainDb = 0.0;
    s.dryDb = 0.0;
    s.wetDb = 0.0;
    if (! simulated || ! (*simulated == s))
    {
        simulate (s);
        simulated = s;
    }
    repaint();
}

juce::Point<float> InsetB::handleAt (Which w) const
{
    if (w == Which::attack)
    {
        const double ms = attack.value();
        return { xAttack (ms), yOf (attackCurveAt (ms)) };
    }
    const double ms = release.value();
    return { xRelease (ms), yOf (releaseCurveAt (ms)) };
}

std::optional<InsetB::Which> InsetB::handleUnder (juce::Point<float> p) const
{
    for (const auto w : { Which::attack, Which::release })
        if (p.getDistanceFrom (handleAt (w)) <= hitRadius)
            return w;
    return std::nullopt;
}

void InsetB::paint (juce::Graphics& g)
{
    g.setColour (colour::raised);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), staple::tokens::size::r3);
    if (hasKeyboardFocus (false))
    {
        g.setColour (colour::focus);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), staple::tokens::size::r3, 1.0f);
    }
    const auto r = plot();
    const float split = r.getX() + r.getWidth() * attackShare;
    textAt (g, "Attack", { r.getX(), 3.0f, 80.0f, 14.0f }, colour::text3, 10.0f);
    textAt (g, "Release", { split + halfGap, 3.0f, 80.0f, 14.0f }, colour::text3, 10.0f);
    textAt (g, "B", { r.getRight() - 20.0f, 3.0f, 20.0f, 14.0f }, colour::text4, 10.0f, juce::Justification::centredRight);

    // Grid: decades.
    g.setColour (colour::line2);
    for (double ms : { 0.01, 0.1, 1.0, 10.0, 100.0, 1000.0 })
        g.drawVerticalLine (static_cast<int> (xAttack (ms)), r.getY(), r.getBottom());
    for (double ms : { 1.0, 10.0, 100.0, 1000.0, 10000.0 })
        g.drawVerticalLine (static_cast<int> (xRelease (ms)), r.getY(), r.getBottom());
    for (auto [ms, label] : { std::pair { 0.01, "10u" }, { 1.0, "1m" }, { 100.0, "100m" } })
        textAt (g, label, { xAttack (ms) - 15.0f, r.getBottom() + 1.0f, 30.0f, 12.0f }, colour::text4, 9.0f, juce::Justification::centred);
    for (auto [ms, label] : { std::pair { 10.0, "10m" }, { 100.0, "100m" }, { 1000.0, "1s" }, { 10000.0, "10s" } })
        textAt (g, label, { xRelease (ms) - 15.0f, r.getBottom() + 1.0f, 30.0f, 12.0f }, colour::text4, 9.0f, juce::Justification::centred);
    g.drawHorizontalLine (static_cast<int> (r.getY()), r.getX(), r.getRight());

    // Bands (tests 3 and 4).
    if (character == Character::Opto && ! rise.empty())
    {
        // The overshoot scales Attack from 2x to 0.5x: a band of one-pole rises at the two ends.
        juce::Path band;
        const double a = attack.value();
        for (int pass = 0; pass < 2; ++pass)
            for (int i = 0; i <= 80; ++i)
            {
                const int k = pass == 0 ? i : 80 - i;
                const double ms = std::pow (10.0, std::log10 (attackLow) + k / 80.0 * (std::log10 (attackHigh) - std::log10 (attackLow)));
                const double tau = pass == 0 ? 0.5 * a : 2.0 * a;
                const juce::Point<float> p { xAttack (ms), yOf (steadyDb * (1.0 - std::exp (-ms / tau))) };
                i == 0 && pass == 0 ? band.startNewSubPath (p) : band.lineTo (p);
            }
        band.closeSubPath();
        g.setColour (colour::curveMain.withAlpha (0.18f));
        g.fillPath (band);
        // The slow part's own time, 500 to 5000 ms, which Release doesn't move.
        const float x0 = xRelease (500.0), x1 = xRelease (5000.0);
        g.setColour (colour::placeSide.withAlpha (0.14f));
        g.fillRect (juce::Rectangle<float> (x0, r.getY(), x1 - x0, r.getHeight()));
        textAt (g, "Opto slow (fixed)", { x0 - 30.0f, r.getBottom() - 13.0f, x1 - x0 + 30.0f, 12.0f }, colour::placeSide, 9.0f);
    }
    if (autoRelease && state.autoReleaseLook == AutoReleaseLook::band)
    {
        // Release up to 5 x Release, from the Gain Reduction at the burst's end.
        juce::Path band;
        const double rel = release.value();
        for (int pass = 0; pass < 2; ++pass)
            for (int i = 0; i <= 80; ++i)
            {
                const int k = pass == 0 ? i : 80 - i;
                const double ms = std::pow (10.0, std::log10 (releaseLow) + k / 80.0 * (std::log10 (releaseHigh) - std::log10 (releaseLow)));
                const double tau = pass == 0 ? rel : 5.0 * rel;
                const juce::Point<float> p { xRelease (ms), yOf (endDb * std::exp (-ms / tau)) };
                i == 0 && pass == 0 ? band.startNewSubPath (p) : band.lineTo (p);
            }
        band.closeSubPath();
        g.setColour (colour::dynRange.withAlpha (0.22f));
        g.fillPath (band);
        textAt (g, "Auto Rel: R to 5R", { xRelease (10.0), r.getY() + 2.0f, 100.0f, 12.0f }, colour::dynRange, 9.0f);
    }

    // The Engine's own curves.
    juce::Path rising, falling;
    bool startedRise = false;
    for (size_t i = 0; i < rise.size(); ++i)
    {
        const double ms = (static_cast<double> (i) + 1.0) * riseRunMs;
        if (ms > attackHigh)
            break;
        const juce::Point<float> p { xAttack (ms), yOf (rise[i]) };
        if (! startedRise)
        {
            rising.startNewSubPath (xAttack (attackLow), yOf (0.0));
            startedRise = true;
        }
        rising.lineTo (p);
    }
    falling.startNewSubPath (xRelease (releaseLow), yOf (endDb));
    for (size_t i = 0; i < fall.size(); ++i)
    {
        const double ms = (static_cast<double> (i) + 1.0) * fallRunMs;
        if (ms < releaseLow)
            continue;
        falling.lineTo (xRelease (ms), yOf (fall[i]));
    }
    g.setColour (colour::dynRange);
    g.strokePath (rising, juce::PathStrokeType (1.5f));
    g.strokePath (falling, juce::PathStrokeType (1.5f));
    if (autoRelease && state.autoReleaseLook == AutoReleaseLook::curve)
        textAt (g, "Auto Rel: after 1 s", { xRelease (10.0), r.getY() + 2.0f, 180.0f, 12.0f }, colour::dynRange, 9.0f);

    for (const auto w : { Which::attack, Which::release })
        handles::paint (g, handleAt (w), { .radius = handleRadius, .selected = drag && drag->which == w, .hovered = hovered == w, .alpha = 1.0f });
    if (drag)
    {
        const auto& e = editOf (drag->which);
        handles::paintReadout (g, handleAt (drag->which), handleRadius, static_cast<float> (getWidth()),
                               drag->which == Which::attack ? "Attack" : "Release", controls::spokenValue (e.param, e.value()));
    }
}

void InsetB::mouseMove (const juce::MouseEvent& e)
{
    const auto now = handleUnder (e.position);
    if (now != hovered)
    {
        hovered = now;
        setMouseCursor (now ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void InsetB::mouseExit (const juce::MouseEvent&)
{
    hovered.reset();
    repaint();
}

void InsetB::mouseDown (const juce::MouseEvent& e)
{
    mouseMove (e);
    if (! hovered)
        return;
    editOf (*hovered).begin();
    drag = Drag { *hovered, e.position.x, editOf (*hovered).value() };
}

void InsetB::mouseDrag (const juce::MouseEvent& e)
{
    if (! drag)
        return;
    const double speed = e.mods.isShiftDown() ? 0.25 : 1.0;
    const float x0 = drag->which == Which::attack ? xAttack (drag->from) : xRelease (drag->from);
    const float x = x0 + static_cast<float> ((e.position.x - drag->x) * speed);
    // Along the log axis, past its ends too.
    const auto r = plot();
    if (drag->which == Which::attack)
    {
        const double perPx = (std::log10 (attackHigh) - std::log10 (attackLow)) / (r.getWidth() * attackShare);
        attack.set (std::pow (10.0, std::log10 (drag->from) + (x - x0) * perPx));
    }
    else
    {
        const float span = r.getRight() - (r.getX() + r.getWidth() * attackShare + halfGap);
        const double perPx = (std::log10 (releaseHigh) - std::log10 (releaseLow)) / span;
        release.set (std::pow (10.0, std::log10 (drag->from) + (x - x0) * perPx));
    }
    repaint();
}

void InsetB::mouseUp (const juce::MouseEvent&)
{
    if (! drag)
        return;
    editOf (drag->which).end();
    drag.reset();
    repaint();
}

bool InsetB::keyPressed (const juce::KeyPress& key)
{
    // Test 7: left and right Attack, up and down Release, 1% of travel (0.2% with Shift).
    const int code = key.getKeyCode();
    const double step = key.getModifiers().isShiftDown() ? 0.002 : 0.01;
    ParamEdit* e = nullptr;
    double dir = 0.0;
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
    {
        e = &attack;
        dir = code == juce::KeyPress::rightKey ? 1.0 : -1.0;
    }
    else if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        e = &release;
        dir = code == juce::KeyPress::upKey ? 1.0 : -1.0;
    }
    if (e == nullptr)
        return false;
    e->begin();
    heldKey = true;
    e->setTravel (e->travel() + dir * step);
    repaint();
    return true;
}

bool InsetB::keyStateChanged (bool isKeyDown)
{
    if (! isKeyDown && std::exchange (heldKey, false) && ! drag)
    {
        attack.end();
        release.end();
    }
    return false;
}

void InsetB::focusLost (FocusChangeType)
{
    if (std::exchange (heldKey, false) && ! drag)
    {
        attack.end();
        release.end();
    }
    repaint();
}

// ---------------------------------------------------------------------------------------------------------------------

Bar::Bar (State& s, PluginProcessor& p) : state (s)
{
    const auto setUp = [this, &s] (juce::ComboBox& box, juce::StringArray items, int chosen, std::function<void (int)> apply,
                                   const juce::String& tip) {
        box.addItemList (items, 1);
        box.setSelectedItemIndex (chosen, juce::dontSendNotification);
        box.setTooltip (tip);
        box.onChange = [&box, apply, &s] {
            apply (box.getSelectedItemIndex());
            if (s.changed)
                s.changed();
        };
        addAndMakeVisible (box);
    };
    setUp (candidate, { "Proto: A freeze & grab", "Proto: B Timing inset", "Proto: off" }, static_cast<int> (s.candidate),
           [&s] (int i) { s.candidate = static_cast<Candidate> (i); }, "PROTOTYPE #215: which candidate");
    setUp (freeze, { "A freeze: on hover", "A freeze: on press", "A freeze: never" }, static_cast<int> (s.freeze),
           [&s] (int i) { s.freeze = static_cast<Freeze> (i); }, "Candidate A: when the graph freezes");
    setUp (dragScale, { "A drag: knob travel", "A drag: graph time" }, static_cast<int> (s.dragScale),
           [&s] (int i) { s.dragScale = static_cast<DragScale> (i); }, "Candidate A: drag sensitivity");
    setUp (autoRelease, { "B Auto Rel: band", "B Auto Rel: curve" }, static_cast<int> (s.autoReleaseLook),
           [&s] (int i) { s.autoReleaseLook = static_cast<AutoReleaseLook> (i); }, "Candidate B: Auto Release drawn as a band or one curve");
    setUp (place, { "B: beside inset", "B: below inset", "B: swap with inset" }, static_cast<int> (s.place),
           [&s] (int i) { s.place = static_cast<Place> (i); }, "Candidate B: where it sits");
    setUp (signal, { "Signal: input", "Signal: drums", "Signal: tone bursts", "Signal: drums + bass" }, p.protoTestSignal.load(),
           [&p] (int i) { p.protoTestSignal.store (i); }, "PROTOTYPE: test signal replacing the input");
}

void Bar::sync()
{
    candidate.setSelectedItemIndex (static_cast<int> (state.candidate), juce::dontSendNotification);
    freeze.setSelectedItemIndex (static_cast<int> (state.freeze), juce::dontSendNotification);
    dragScale.setSelectedItemIndex (static_cast<int> (state.dragScale), juce::dontSendNotification);
    autoRelease.setSelectedItemIndex (static_cast<int> (state.autoReleaseLook), juce::dontSendNotification);
    place.setSelectedItemIndex (static_cast<int> (state.place), juce::dontSendNotification);
}

void Bar::resized()
{
    auto area = getLocalBounds();
    for (auto* box : { &signal, &place, &autoRelease, &dragScale, &freeze, &candidate })
    {
        box->setBounds (area.removeFromRight (box == &candidate || box == &signal ? 168 : 150));
        area.removeFromRight (4);
    }
}

} // namespace comp1::proto215
