#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

#include <BinaryData.h>

#include <set>

// The wave-3 layout manifest (resources/gui/layout-manifest.json) is the
// single source of the composited editor's control surface - these tests
// pin it against BOTH sides it has to agree with: the rendered control
// inventory (rollout-2026-07/miserere/control-inventory.md: 39 knobs + 11
// selector knobs + 17 toggles + 4 D3 bus faders + 2 GR dials; see
// src/PluginEditor.h for the two documented post-inventory adjustments)
// and the live APVTS parameter set.
namespace
{
    MiserereAudioProcessorEditor::Manifest parsedManifest()
    {
        return MiserereAudioProcessorEditor::parseLayoutManifest();
    }

    int countOfType (const MiserereAudioProcessorEditor::Manifest& m, const juce::String& type)
    {
        int count = 0;

        for (const auto& control : m.controls)
            if (control.type == type)
                ++count;

        return count;
    }
}

TEST_CASE ("Manifest control counts match the rendered control inventory", "[gui][layout]")
{
    const auto manifest = parsedManifest();

    CHECK (countOfType (manifest, "knob") == 39);
    CHECK (countOfType (manifest, "selector") == 11);
    CHECK (countOfType (manifest, "toggle") == 17);
    CHECK (countOfType (manifest, "fader") == 4);
    CHECK (countOfType (manifest, "gr") == 2);
    CHECK ((int) manifest.controls.size() == 73);
}

TEST_CASE ("Every manifest control id resolves to an APVTS parameter of the matching kind", "[gui][layout]")
{
    MiserereAudioProcessor processor;
    const auto manifest = parsedManifest();

    for (const auto& control : manifest.controls)
    {
        if (control.type == "gr")
        {
            CHECK ((control.tap == "directFetGr" || control.tap == "busGr"));
            continue;
        }

        auto* parameter = processor.apvts.getParameter (control.id);

        INFO ("manifest id: " << control.id);
        REQUIRE (parameter != nullptr);

        if (control.type == "toggle")
        {
            CHECK (dynamic_cast<juce::AudioParameterBool*> (parameter) != nullptr);
        }
        else if (control.type == "selector")
        {
            CHECK (dynamic_cast<juce::AudioParameterChoice*> (parameter) != nullptr);
        }
        else
        {
            CHECK (dynamic_cast<juce::AudioParameterBool*> (parameter) == nullptr);
            CHECK (dynamic_cast<juce::AudioParameterChoice*> (parameter) == nullptr);
        }
    }
}

TEST_CASE ("Manifest ids are unique and every control sits inside the plate", "[gui][layout]")
{
    const auto manifest = parsedManifest();

    REQUIRE (manifest.plateWidth1x > 0);
    REQUIRE (manifest.plateHeight1x > 0);

    std::set<juce::String> seen;

    for (const auto& control : manifest.controls)
    {
        INFO ("manifest id: " << control.id);
        CHECK (seen.insert (control.id).second);
        CHECK (control.size > 0.0f);

        const auto half = control.size * 0.5f;
        CHECK (control.cx - half >= 0.0f);
        CHECK (control.cx + half <= (float) manifest.plateWidth1x);
        CHECK (control.cy - half >= 0.0f);
        CHECK (control.cy + half <= (float) manifest.plateHeight1x);
    }
}

TEST_CASE ("Manifest references only binary resources that exist, and interactive controls carry labels", "[gui][layout]")
{
    const auto manifest = parsedManifest();

    const auto resourceExists = [] (const juce::String& name)
    {
        int size = 0;
        return BinaryData::getNamedResource (name.toRawUTF8(), size) != nullptr && size > 0;
    };

    CHECK (resourceExists (manifest.plateBinary));

    REQUIRE (! manifest.sprites.empty());

    for (const auto& [name, sprite] : manifest.sprites)
    {
        INFO ("sprite: " << name);
        CHECK (resourceExists (sprite.binary));
        CHECK (sprite.width > 0.0f);
        CHECK (sprite.height > 0.0f);
    }

    // The D2 mini GR dials deliberately carry no engraved label of their
    // own (DECISIONS.md D2: the face stays clean, the band lettering is
    // the section header above the column) - every interactive control
    // must be labelled.
    for (const auto& control : manifest.controls)
    {
        INFO ("manifest id: " << control.id);

        if (control.type != "gr" && control.type != "vu")
            CHECK (control.label.isNotEmpty());
    }

    // GR tick table: rest (0 dB) sits at the table's high end, per the
    // suite-wide GR numeral convention.
    REQUIRE (manifest.grTicks.size() >= 2);
    CHECK (manifest.grTicks.front().db < manifest.grTicks.back().db);
    CHECK (manifest.grTicks.back().db == 0.0f);
}

TEST_CASE ("The editor instantiates exactly one live control per manifest entry", "[gui][layout]")
{
    MiserereAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    MiserereAudioProcessorEditor editor (processor);
    const auto manifest = parsedManifest();

    int liveControls = 0;

    for (int i = 0; i < editor.getNumChildComponents(); ++i)
    {
        auto* child = editor.getChildComponent (i);

        if (dynamic_cast<basilica::gui::SpriteKnob*> (child) != nullptr
            || dynamic_cast<basilica::gui::SpriteToggle*> (child) != nullptr
            || dynamic_cast<basilica::gui::SpriteFader*> (child) != nullptr
            || dynamic_cast<basilica::gui::NeedleDial*> (child) != nullptr)
            ++liveControls;
    }

    CHECK (liveControls == (int) manifest.controls.size());
}
