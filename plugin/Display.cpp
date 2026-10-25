#include "Display.h"

#include "Parameters.h"

#include <cstdlib>

namespace comp1
{

Display::Display (PluginProcessor& p, LevelHistory& history)
    : plugkit::ui::EditorTick::Client (static_cast<juce::Component&> (*this)), processor (p), level (history, p), inset (p, history), spectrum (p), protoA (level, p, protoState), protoB (p, protoState), protoBar (protoState, p)
{
    setName ("Display");
    level.setName ("Level");
    int order = 0;
    for (auto& [tab, chip, content] : tabs)
    {
        chip.setTitle (chip.getButtonText());
        chip.setTooltip ("Show the " + chip.getButtonText() + " display");
        chip.onClick = [this, tab] {
            processor.setDisplayTab (tab);
            show (tab);
        };
        // Left to right.
        chip.setExplicitFocusOrder (++order);
        addAndMakeVisible (chip);
        addChildComponent (content);
    }
    // PROTOTYPE #215: A's overlay over the graph, under the insets.
    addChildComponent (protoA);
    // After the tabs.
    inset.setExplicitFocusOrder (++order);
    addChildComponent (inset);
    protoB.setExplicitFocusOrder (++order);
    addChildComponent (protoB);
    addAndMakeVisible (protoBar);
    protoState.changed = [this] {
        protoBar.sync();
        show (shown);
        protoLayout();
    };
    show (processor.displayTab());

    // PROTOTYPE #215: with COMP1_PROTO_SNAPSHOTS=<folder>, render PNGs of A and B and quit.
    if (const char* folder = std::getenv ("COMP1_PROTO_SNAPSHOTS"))
    {
        const juce::File dir (folder);
        dir.createDirectory();
        juce::Component::SafePointer<Display> self (this);
        auto* params = &p.parameterState();
        const auto setParam = [params] (const juce::String& id, double plain) {
            auto& param = parameters::parameterOf (*params, id);
            param.setValueNotifyingHost (param.convertTo0to1 (static_cast<float> (plain)));
        };
        const auto shot = [self, dir] (const juce::String& name) {
            if (self == nullptr)
                return;
            auto* top = self->getTopLevelComponent();
            auto image = top->createComponentSnapshot (top->getLocalArea (self, self->getLocalBounds()), true, 1.5f);
            juce::FileOutputStream out (dir.getChildFile (name + ".png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
            dir.getChildFile ("notes.txt").appendText (name + (self->protoA.isVisible() ? ": " + self->protoA.infoText() : juce::String()) + "\n");
        };
        const auto choose = [self] (proto215::Candidate c, proto215::Place place = proto215::Place::beside) {
            if (self == nullptr)
                return;
            self->protoState.candidate = c;
            self->protoState.place = place;
            self->protoState.changed();
        };
        std::vector<std::pair<int, std::function<void()>>> steps;
        const auto add = [&steps] (int delay, std::function<void()> f) { steps.emplace_back (delay, std::move (f)); };
        add (14000, [=] { self->getTopLevelComponent()->setInterceptsMouseClicks (false, false); choose (proto215::Candidate::a); });
        // Test 1: Attack at 1, 10 and 50 ms on the drums.
        for (double ms : { 1.0, 10.0, 50.0 })
        {
            add (100, [=] { setParam (parameters::attackId, ms); });
            add (6000, [=] { self->protoA.forceFreeze = true; });
            add (700, [=] { shot ("T1-attack-" + juce::String (ms, 0) + "ms-frozen"); self->protoA.forceFreeze = false; });
        }
        add (100, [=] { setParam (parameters::attackId, 10.0); });
        add (6000, [=] { self->protoA.forceFreeze = true; });
        add (800, [=] { shot ("A1-frozen-handles-clean"); });
        add (100, [=] { self->protoA.debugDrag (false, 400.0); });
        add (600, [=] { shot ("A2-dragging-release-400ms-ghost"); self->protoA.debugLetGo(); setParam (parameters::releaseId, 100.0); });
        add (300, [=] { self->protoA.forceFreeze = false; });
        add (2500, [=] { self->protoA.forceFreeze = true; });
        add (600, [=] { self->protoA.debugDrag (true, 50.0); });
        add (600, [=] { shot ("A3-dragging-attack-50ms-ghost"); self->protoA.debugLetGo(); setParam (parameters::attackId, 10.0); self->protoA.forceFreeze = false; });
        add (300, [=] { choose (proto215::Candidate::b); });
        add (800, [=] { shot ("B1-beside-clean"); });
        add (100, [=] { setParam (parameters::autoReleaseId, 1.0); });
        add (800, [=] { shot ("B2-beside-clean-auto-release-band"); self->protoState.autoReleaseLook = proto215::AutoReleaseLook::curve; self->protoState.changed(); });
        add (800, [=] { shot ("B3-beside-clean-auto-release-curve"); self->protoState.autoReleaseLook = proto215::AutoReleaseLook::band; self->protoState.changed(); });
        // Test 5 on FET.
        add (100, [=] { self->processor.chooseStyle (Style::FET); choose (proto215::Candidate::a); });
        add (6000, [=] { self->protoA.forceFreeze = true; });
        add (800, [=] { shot ("A6-frozen-fet"); self->protoA.forceFreeze = false; choose (proto215::Candidate::b); });
        add (800, [=] { shot ("B7-beside-fet"); });
        add (100, [=] { self->processor.chooseStyle (Style::Opto); });
        add (300, [=] { setParam (parameters::autoReleaseId, 1.0); });
        add (1200, [=] { shot ("B4-beside-opto-auto-release"); choose (proto215::Candidate::b, proto215::Place::below); });
        add (800, [=] { shot ("B5-below-opto"); choose (proto215::Candidate::b, proto215::Place::swap); });
        add (800, [=] { shot ("B6-swap-opto"); choose (proto215::Candidate::a); });
        add (6000, [=] { self->protoA.forceFreeze = true; });
        add (800, [=] { shot ("A4-frozen-opto"); self->protoA.debugDrag (false, 600.0); });
        add (600, [=] { shot ("A5-dragging-release-600ms-opto"); self->protoA.debugLetGo(); });
        add (300, [] { juce::JUCEApplicationBase::quit(); });
        int at = 0;
        for (auto& [delay, step] : steps)
        {
            at += delay;
            juce::Timer::callAfterDelay (at, [self, step] {
                if (self != nullptr)
                    step();
            });
        }
    }
}

juce::Component& Display::tabContent (DisplayTab tab)
{
    return tabs[rowOf (tab)].content;
}

void Display::tick (double)
{
    if (processor.displayTab() != shown)
        show (processor.displayTab());
}

void Display::show (DisplayTab tab)
{
    shown = tab;
    for (auto& [rowTab, chip, content] : tabs)
    {
        chip.setToggleState (rowTab == tab, juce::dontSendNotification);
        content.setVisible (rowTab == tab);
    }
    using proto215::Candidate;
    const bool level = tab == DisplayTab::level;
    protoA.setVisible (level && protoState.candidate == Candidate::a);
    protoB.setVisible (level && protoState.candidate == Candidate::b);
    inset.setVisible (level && ! (protoState.candidate == Candidate::b && protoState.place == proto215::Place::swap));
}

void Display::protoLayout()
{
    const auto at = level.getPosition().translated (insetGap, insetGap);
    switch (protoState.place)
    {
        case proto215::Place::beside: protoB.setTopLeftPosition (at.translated (TransferCurveInset::side + insetGap, 0)); break;
        case proto215::Place::below: protoB.setTopLeftPosition (at.translated (0, TransferCurveInset::side + insetGap)); break;
        case proto215::Place::swap: protoB.setTopLeftPosition (at); break;
    }
}

void Display::resized()
{
    auto area = getLocalBounds();
    auto chips = area.removeFromTop (tabHeight);
    protoBar.setBounds (chips.withTrimmedLeft (200).reduced (0, 2));
    area.removeFromTop (contentGap);
    for (auto& [tab, chip, content] : tabs)
    {
        chip.setBounds (chips.removeFromLeft (chip.getIdealWidth()));
        chips.removeFromLeft (tabGap);
        content.setBounds (area);
    }
    inset.setTopLeftPosition (level.getPosition().translated (insetGap, insetGap));
    protoA.setBounds (level.getBounds());
    protoLayout();
}

} // namespace comp1
