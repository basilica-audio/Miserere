#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

#include "gui/BasilicaLookAndFeel.h"
#include "gui/BusPanel.h"
#include "gui/NeedleMeter.h"
#include "gui/PointerKnob.h"
#include "presets/PresetBar.h"

class MiserereAudioProcessor;

// M3 custom vector editor (issue #25) + accessible parameter surface
// (issue #26).
//
// Everything is drawn at runtime by BasilicaLookAndFeel / the src/gui
// components - no photoreal PNG assets exist in this plugin (unlike the
// filmstrip/faceplate siblings): pointer knobs with engraved scale rings,
// lamp toggles, and one vector needle meter per gain-reducing bus (Direct
// FET, Crush, Sandwich - the three GR sources MiserereAudioProcessor
// exposes), grouped into one BusPanel per bus in signal-flow order
// (Global / Direct / Crush / Sandwich / Spread / Slap).
//
// FOCUS ORDER CONTRACT (WCAG 2.4.3, suite-wide convention): JUCE's default
// traverser walks children in z-order, which equals CREATION order - the
// constructor therefore creates every control in signal-flow/reading order
// (preset bar first, then panel by panel, left-to-right within each row),
// and nothing may reorder children afterwards. Each BusPanel is an
// accessibility focus container (NOT a keyboard focus container - see
// BusPanel.h), so screen readers hear "Crush Bus, Ratio" while Tab still
// walks the whole editor.
//
// Controls are built data-driven from ID/label tables (see the .cpp) - all
// float AND choice parameters are PointerKnobs (choice knobs snap to their
// integer detents and announce the choice NAME), bool parameters are real
// juce::ToggleButtons (focusable and Space/Enter-operable out of the box,
// reported as toggle buttons by AT).
class MiserereAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    explicit MiserereAudioProcessorEditor (MiserereAudioProcessor& processorToEdit);
    ~MiserereAudioProcessorEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        basilica::gui::PointerKnob slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    struct Toggle
    {
        juce::ToggleButton button;
        std::unique_ptr<ButtonAttachment> attachment;
    };

    // One bus faceplate: the BusPanel component plus its control rows
    // (each row a left-to-right list of the controls laid out in it) and
    // an optional gain-reduction needle meter in the panel's right bay.
    struct Panel
    {
        std::unique_ptr<basilica::gui::BusPanel> component;
        std::vector<std::vector<juce::Component*>> rows;
        basilica::gui::NeedleMeter* meter = nullptr; // owned via `meters`
    };

    Panel& addPanel (const juce::String& busTitle);
    void addRow (Panel& panel);
    Knob& addKnob (Panel& panel, const char* parameterId, const juce::String& labelText);
    Toggle& addToggle (Panel& panel, const char* parameterId, const juce::String& labelText);
    basilica::gui::NeedleMeter& addMeter (Panel& panel, const juce::String& accessibleTitle,
                                          const juce::String& faceLegend);

    void timerCallback() override;

    static int slotWidthFor (const juce::Component& control) noexcept;
    static int rowWidth (const std::vector<juce::Component*>& row) noexcept;
    int panelRequiredWidth (const Panel& panel) const noexcept;
    int panelRequiredHeight (const Panel& panel) const noexcept;

    MiserereAudioProcessor& audioProcessor;

    // Must be constructed before any child that paints with it and
    // installed on `this` so it propagates to every child (including the
    // preset bar's stock buttons/menus/dialogs).
    basilica::gui::BasilicaLookAndFeel lookAndFeel;

    // M2 preset system - constructed after the localisation frame is
    // installed (see the constructor) so its TRANS()'d strings pick up the
    // right language from the very first paint.
    basilica::presets::PresetBar presetBar;

    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Toggle>> toggles;
    std::vector<std::unique_ptr<basilica::gui::NeedleMeter>> meters;
    std::vector<std::unique_ptr<Panel>> panels;

    // Signal-flow panels, kept as raw pointers into `panels` for layout:
    // the first five stack as full-width bands; spread+slap share the
    // bottom band.
    Panel* globalPanel = nullptr;
    Panel* directPanel = nullptr;
    Panel* crushPanel = nullptr;
    Panel* sandwichPanel = nullptr;
    Panel* spreadPanel = nullptr;
    Panel* slapPanel = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MiserereAudioProcessorEditor)
};
