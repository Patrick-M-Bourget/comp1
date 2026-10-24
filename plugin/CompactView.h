#pragma once

#include "CompFollowAttachment.h"
#include "Controls.h"
#include "GainReductionMeter.h"
#include "HeroComp.h"
#include "LevelHistory.h"
#include "PluginProcessor.h"
#include "comp1/Style.h"
#include "plugkit/ui/EditorTick.h"
#include "plugkit/ui/staple/Tokens.h"
#include "plugkit/ui/staple/controls/Knob.h"
#include "plugkit/ui/staple/controls/Tween.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>

namespace comp1
{

// One control of a Compact Slot's row: its host parameter's ID, and the text it shows, its label under a knob or a
// toggle's own text. A Companion is a toggle for an on/off parameter, else a small knob.
struct CompactSlotControl
{
    const juce::String* parameterId;
    const char* text;
};

// A Compact Slot's controls in one Style: its primary, then its Companion.
struct CompactSlotRow
{
    CompactSlotControl primary, companion;
};

// A Style's row in compactSlots: the Style, and its Compact Slots A and B.
struct CompactSlotsRow
{
    Style style;
    CompactSlotRow a, b;
};

// What the Compact Slots show in each Style, a row each in Style's order (ADR 0011). Compression and saturation
// settings only: never Threshold or Ratio, Dry or Wet. The High-pass as a whole slot is its Frequency with its on
// toggle as the Companion (its Slope not shown); as a Companion, its on toggle alone.
inline constexpr CompactSlotsRow compactSlots[] {
    { Style::Clean, { { &parameters::attackId, "Attack" }, { &parameters::kneeId, "Knee" } }, { { &parameters::releaseId, "Release" }, { &parameters::autoReleaseId, "Auto Release" } } },
    { Style::VCA, { { &parameters::attackId, "Attack" }, { &parameters::detectionHighPassOnId, "High-Pass" } }, { { &parameters::releaseId, "Release" }, { &parameters::autoReleaseId, "Auto Release" } } },
    { Style::FET, { { &parameters::attackId, "Attack" }, { &parameters::colourId, "Colour" } }, { { &parameters::releaseId, "Release" }, { &parameters::holdId, "Hold" } } },
    { Style::Opto, { { &parameters::colourId, "Colour" }, { &parameters::rangeId, "Range" } }, { { &parameters::detectionHighPassFrequencyId, "High-Pass" }, { &parameters::detectionHighPassOnId, "On" } } },
    { Style::Vocal, { { &parameters::gateThresholdId, "Gate Threshold" }, { &parameters::gateId, "Gate" } }, { { &parameters::releaseId, "Release" }, { &parameters::autoReleaseId, "Auto Release" } } },
    { Style::Instrument, { { &parameters::attackId, "Attack" }, { &parameters::kneeId, "Knee" } }, { { &parameters::releaseId, "Release" }, { &parameters::autoReleaseId, "Auto Release" } } },
    { Style::Bus, { { &parameters::attackId, "Attack" }, { &parameters::detectionHighPassOnId, "High-Pass" } }, { { &parameters::releaseId, "Release" }, { &parameters::autoReleaseId, "Auto Release" } } },
    { Style::Drums, { { &parameters::attackId, "Attack" }, { &parameters::holdId, "Hold" } }, { { &parameters::driveId, "Drive" }, { &parameters::saturationId, "Saturation" } } },
};

// A Style's row in compactSlots.
constexpr std::size_t compactSlotsRowOf (Style style) { return static_cast<std::size_t> (style); }

static_assert (
    [] {
        for (std::size_t row = 0; row < std::size (compactSlots); ++row)
            if (compactSlotsRowOf (compactSlots[row].style) != row)
                return false;
        return true;
    }(),
    "compactSlots lists every Style once, in Style's order");
static_assert (std::size (compactSlots) == styleCount, "compactSlots has a row for every Style");

// A Compact Slot: a primary knob at the kit's large size with its label under it, and its Companion under that, a
// toggle or a knob at the kit's small size with its label. It keeps its controls and rebinds them (show): a Style
// change attaches them to another row's parameters, titles them with those parameters' names and shows the
// Companion's kind, writing nothing. Its size is the same for every row (idealWidth, idealHeight), and so is where
// each control sits in it. Tab walks the primary, then the Companion.
//
// A rebinding can crossfade: the slot as it looked fades out over crossfadeMs, drawn over the rebound controls as they
// fade in. The rebound controls take edits and Tab from the start; only alpha changes, never a place.
class CompactSlot final : public juce::Component
{
public:
    // The crossfade's time: the design system's menus-and-cards duration, the family's pop-in fade.
    static constexpr int crossfadeMs = staple::tokens::motion::dur2Ms;

    CompactSlot (const juce::String& name, PluginProcessor& processor);
    ~CompactSlot() override;

    // Attaches the controls to row's parameters, crossfading from how the slot looks if crossfading and the slot is
    // showing, else at once. A rebinding during a crossfade fades out from how the slot looks then.
    void show (const CompactSlotRow& row, bool crossfading = false);
    // How far the crossfade has gone, 0 to 1: the rebound controls' alpha, and 1 when none is under way.
    float crossfade() const { return fade.value(); }
    // Whether the slot as it looked before the rebinding is still drawn, fading out.
    bool showsOutgoing() const { return outgoing.isValid(); }

    staple::Knob& primary() { return primaryKnob; }
    // The Companion shown: the toggle or the small knob.
    juce::Component& companion();

    // The widest any row needs, and the height of a primary over a small knob's Companion.
    static int idealWidth();
    static int idealHeight();

    void resized() override;
    void paintOverChildren (juce::Graphics& g) override;

private:
    juce::RangedAudioParameter& parameter (const CompactSlotControl& control);
    // Sets the controls' alpha to the crossfade's progress, dropping the outgoing image at its end.
    void applyCrossfade (float progress);

    PluginProcessor& processor;
    staple::Knob primaryKnob, companionKnob;
    juce::Label primaryLabel, companionLabel;
    juce::ToggleButton companionToggle;
    std::unique_ptr<juce::SliderParameterAttachment> primaryAttachment, companionKnobAttachment;
    std::unique_ptr<juce::ButtonParameterAttachment> companionToggleAttachment;
    // The slot as it looked before the rebinding, drawn at 1 - the crossfade's progress until it ends.
    juce::Image outgoing;
    staple::Tween fade { crossfadeMs, 1.0f };
};

// The Compact View's body, under the editor's shared header and footer, named "Compact View": Compact Slot A, Comp,
// Compact Slot B and the compressor's Gain Reduction meter, left to right in one row; Tab walks A's primary and
// Companion, Comp, then B's (ADR 0011). Style is the footer's chip, and Output Gain is in the output popover the
// footer's Output readout opens, as in the Expanded View.
//
// - Compact Slots A and B: what each shows is the Style's row in compactSlots, followed on the editor's tick, so
//   only the Style parameter changes it, from the chip, a Preset, A/B Compare or undo alike. Following it writes
//   nothing: a Style change writes only what chooseStyle() writes.
// - Comp: the Comp knob alone, at the kit's hero size, attached as the Expanded View's (addCompKnob): grabbing it
//   while Comp Follow is off follows, with the edit that follows as one undo step. There is no Comp Follow toggle,
//   Threshold or Ratio. Its knob has the Detection Level ring and the Comp Follow-off cue (HeroComp).
// - The Gain Reduction meter: the compressor's, on the editor's level history, as tall as the body, and no stop for Tab.
//
// The meter is at the body's right edge, and the room left of it is three equal columns, slot A, Comp and slot B each
// centred in its own and centred down the body. Nothing's place depends on the Style, so a Style change moves nothing;
// a larger window spreads the row out and makes the meter taller. Both views' controls attach to the same host
// parameters, so an edit in one shows in the other.
class CompactView final : public juce::Component
{
public:
    CompactView (PluginProcessor& processor, LevelHistory& history);
    ~CompactView() override;

    GainReductionMeter& gainReductionMeter() { return gainReduction; }
    // Compact Slot A (0) or B (1).
    CompactSlot& compactSlot (std::size_t index) { return *slots[index]; }
    HeroComp& heroComp() { return *hero; }

    // Whether a Style change swaps the Compact Slots' controls at once, without the crossfade, as the OS's reduced
    // motion setting asks. Nothing in plugkit reads that setting yet, so it is off unless set here.
    void setReducedMotion (bool on) { reducedMotion = on; }

    void resized() override;

private:
    // Shows the Style's row in the slots, if it isn't shown.
    void followStyle();

    PluginProcessor& processor;
    std::array<std::unique_ptr<CompactSlot>, 2> slots;
    ControlSection compressor;
    GainReductionMeter gainReduction;
    std::optional<CompFollowAttachment> compAttachment;
    std::optional<HeroComp> hero;
    std::optional<Style> shownStyle;
    bool reducedMotion = false;
    // Follows the Style on the editor's tick.
    struct StyleFollower final : plugkit::ui::EditorTick::Client
    {
        explicit StyleFollower (CompactView& v) : Client (v), view (v) {}
        void tick (double) override { view.followStyle(); }
        CompactView& view;
    } styleFollower { *this };
};

} // namespace comp1
