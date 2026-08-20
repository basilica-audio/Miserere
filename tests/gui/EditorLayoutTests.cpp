#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "gui/BusPanel.h"
#include "gui/NeedleMeter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <vector>

// M3 vector-editor layout tests (issue #25). Unlike the photoreal siblings
// (whose EditorLayoutTests assert hand-measured pixel manifests against
// baked master renders), Miserere's geometry is COMPUTED from the layout
// constants + control tables in PluginEditor.cpp - so the manifest under
// test here is the real constructed component tree itself: containment, no
// overlap, and full parameter coverage. Any arithmetic slip in the layout
// constants shows up as a concrete clipped/colliding control here.
namespace
{
    template <typename ComponentType>
    void visitDescendants (juce::Component& parent, const std::function<void (ComponentType&)>& visit)
    {
        for (int i = 0; i < parent.getNumChildComponents(); ++i)
        {
            auto* child = parent.getChildComponent (i);

            if (auto* typed = dynamic_cast<ComponentType*> (child))
                visit (*typed);

            visitDescendants<ComponentType> (*child, visit);
        }
    }

    juce::Rectangle<int> boundsInEditor (const juce::Component& component, const juce::Component& editor)
    {
        auto bounds = component.getBounds();

        for (const auto* ancestor = component.getParentComponent();
             ancestor != nullptr && ancestor != &editor;
             ancestor = ancestor->getParentComponent())
        {
            bounds += ancestor->getPosition();
        }

        return bounds;
    }
}

TEST_CASE ("Every automatable parameter has exactly one attached control", "[gui][layout]")
{
    MiserereAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    MiserereAudioProcessorEditor editor (processor);

    int sliders = 0, toggles = 0;
    visitDescendants<juce::Slider> (editor, [&] (juce::Slider&) { ++sliders; });
    visitDescendants<juce::ToggleButton> (editor, [&] (juce::ToggleButton&) { ++toggles; });

    // The APVTS carries 45 float + 13 choice + 21 bool parameters = 79
    // (ParameterIds.h; the two #20 colour choices joined in v0.6.0, the
    // three #24 output-limiter parameters and the three #23 external-key
    // switches in v0.7.0). One knob per float/choice parameter, one toggle
    // per bool parameter - no parameter may be left off the M3 surface, and
    // no control may exist without a parameter.
    CHECK ((int) processor.getParameters().size() == 79);
    CHECK (sliders + toggles == (int) processor.getParameters().size());
    CHECK (sliders == 58);
    CHECK (toggles == 21);
}

TEST_CASE ("Moving a knob moves its parameter - one wiring spot check per bus", "[gui][layout]")
{
    MiserereAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    MiserereAudioProcessorEditor editor (processor);

    struct Expectation
    {
        const char* title;      // unique across the whole editor
        const char* parameterId;
        double sliderValue;     // a legal, non-default value
        float expectedRaw;      // denormalised parameter value afterwards
    };

    const Expectation expectations[] = {
        { "In Trim", "in_trim", 3.0, 3.0f },              // Global
        { "Sat Drive", "direct_sat_drive", 6.0, 6.0f },   // Direct
        { "Input", "crush_input", 24.0, 24.0f },          // Crush
        { "Peak Reduction", "sand_peakred", 80.0, 80.0f },// Sandwich
        { "Detune", "spread_detune", 9.0, 9.0f },         // Spread
        { "Wobble", "slap_wobble", 40.0, 40.0f },         // Slap
    };

    for (const auto& expectation : expectations)
    {
        juce::Slider* knob = nullptr;
        visitDescendants<juce::Slider> (editor, [&] (juce::Slider& s)
        {
            if (s.getTitle() == expectation.title)
                knob = &s;
        });

        REQUIRE (knob != nullptr);
        INFO ("knob \"" << expectation.title << "\" -> " << expectation.parameterId);

        auto* raw = processor.apvts.getRawParameterValue (expectation.parameterId);
        REQUIRE (raw != nullptr);

        knob->setValue (expectation.sliderValue, juce::sendNotificationSync);
        CHECK (raw->load() == Catch::Approx (expectation.expectedRaw).margin (1.0e-4));
    }
}

TEST_CASE ("All controls, labels and meters stay inside their panel; panels stay inside the editor", "[gui][layout]")
{
    MiserereAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    MiserereAudioProcessorEditor editor (processor);

    const auto editorBounds = editor.getLocalBounds();
    CHECK (editorBounds.getWidth() > 0);
    CHECK (editorBounds.getHeight() > 0);

    int panelsSeen = 0;

    visitDescendants<basilica::gui::BusPanel> (editor, [&] (basilica::gui::BusPanel& panel)
    {
        ++panelsSeen;
        INFO ("panel \"" << panel.getTitle().toStdString() << "\" bounds "
              << panel.getBounds().toString().toStdString());
        CHECK (editorBounds.contains (panel.getBounds()));

        // Every direct child (knobs incl. their value boxes, attached
        // labels, toggles, the meter) must be fully inside the panel.
        for (int i = 0; i < panel.getNumChildComponents(); ++i)
        {
            const auto* child = panel.getChildComponent (i);
            INFO ("child \"" << child->getName().toStdString() << "\" bounds "
                  << child->getBounds().toString().toStdString()
                  << " in panel \"" << panel.getTitle().toStdString() << "\" "
                  << panel.getLocalBounds().toString().toStdString());
            CHECK (panel.getLocalBounds().contains (child->getBounds()));
        }
    });

    CHECK (panelsSeen == 6);
}

TEST_CASE ("No two interactive controls or meters overlap", "[gui][layout]")
{
    MiserereAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    MiserereAudioProcessorEditor editor (processor);

    struct Entry
    {
        juce::String name;
        juce::Rectangle<int> bounds;
    };

    std::vector<Entry> entries;

    const auto collect = [&] (juce::Component& component)
    {
        entries.push_back ({ component.getTitle(), boundsInEditor (component, editor) });
    };

    visitDescendants<juce::Slider> (editor, [&] (juce::Slider& s) { collect (s); });
    visitDescendants<juce::ToggleButton> (editor, [&] (juce::ToggleButton& t) { collect (t); });
    visitDescendants<basilica::gui::NeedleMeter> (editor, [&] (basilica::gui::NeedleMeter& m) { collect (m); });

    // 58 knobs + 21 toggles + 4 meters - the pairwise scan below must not
    // pass vacuously on an empty collection.
    REQUIRE (entries.size() == 83);

    for (size_t i = 0; i < entries.size(); ++i)
    {
        for (size_t j = i + 1; j < entries.size(); ++j)
        {
            INFO ("\"" << entries[i].name.toStdString() << "\" " << entries[i].bounds.toString().toStdString()
                  << " vs \"" << entries[j].name.toStdString() << "\" " << entries[j].bounds.toString().toStdString());
            CHECK_FALSE (entries[i].bounds.intersects (entries[j].bounds));
        }
    }
}

TEST_CASE ("Panels do not overlap each other or the preset bar", "[gui][layout]")
{
    MiserereAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    MiserereAudioProcessorEditor editor (processor);

    std::vector<juce::Rectangle<int>> panelBounds;

    visitDescendants<basilica::gui::BusPanel> (editor, [&] (basilica::gui::BusPanel& panel)
    {
        panelBounds.push_back (panel.getBounds());
    });

    REQUIRE (panelBounds.size() == 6);

    for (size_t i = 0; i < panelBounds.size(); ++i)
        for (size_t j = i + 1; j < panelBounds.size(); ++j)
            CHECK_FALSE (panelBounds[i].intersects (panelBounds[j]));

    // The preset bar band sits above all panels.
    juce::Component* presetBar = nullptr;

    for (int i = 0; i < editor.getNumChildComponents(); ++i)
        if (dynamic_cast<basilica::presets::PresetBar*> (editor.getChildComponent (i)) != nullptr)
            presetBar = editor.getChildComponent (i);

    REQUIRE (presetBar != nullptr);

    for (const auto& bounds : panelBounds)
        CHECK_FALSE (presetBar->getBounds().intersects (bounds));
}

TEST_CASE ("Every knob's visible label text matches its accessible title (label-in-name)", "[gui][layout][a11y]")
{
    MiserereAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    MiserereAudioProcessorEditor editor (processor);

    // WCAG 2.5.3 Label in Name: the label painted next to a knob must be
    // the same string AT users hear as the control's name - a mismatch
    // breaks voice-control users ("click In Trim" targeting a control
    // whose accessible name is something else).
    int labelledKnobs = 0;

    visitDescendants<juce::Label> (editor, [&] (juce::Label& label)
    {
        if (auto* attached = label.getAttachedComponent())
        {
            if (auto* slider = dynamic_cast<juce::Slider*> (attached))
            {
                ++labelledKnobs;
                INFO ("label \"" << label.getText().toStdString() << "\" for knob \""
                      << slider->getTitle().toStdString() << "\"");
                CHECK (label.getText() == slider->getTitle());
            }
        }
    });

    // Every one of the 58 knobs carries an attached, matching label.
    CHECK (labelledKnobs == 58);
}
