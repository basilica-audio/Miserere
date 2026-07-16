#include "params/ParameterIds.h"
#include "presets/Localisation.h"

#include <juce_core/juce_core.h>

#include <BinaryData.h>

#include <catch2/catch_test_macros.hpp>

// M2 i18n frame tests (.scaffold/specs/preset-system-m2.md's "I18N" section:
// "the de mapping parses; every TRANS key present in de.txt (script or test
// iterates keys); parameter names verifiably NOT in the mapping").
//
// These tests parse resources/i18n/de.txt (embedded via BinaryData, see
// CMakeLists.txt) directly through juce::LocalisedStrings rather than going
// through basilica::presets::installLocalisation()'s
// juce::SystemStats::getUserLanguage() branch - that branch depends on the
// locale of the machine running the tests, which CI runners don't control,
// so it isn't a deterministic thing to assert on. What IS deterministic and
// worth asserting: the shipped translation file itself parses correctly and
// carries exactly the PresetBar frame vocabulary, never any DSP/parameter
// terminology (see ParameterIds.h and design-brief.md's module names).
namespace
{
    juce::LocalisedStrings parseDeTranslations()
    {
        const auto text = juce::String::fromUTF8 (BinaryData::de_txt, BinaryData::de_txtSize);
        return { text, true };
    }

    // Every TRANS()'d frame string PresetBar.cpp/PresetManager.cpp use (see
    // src/presets/PresetBar.cpp and src/presets/PresetManager.cpp) - copied
    // verbatim from nave's pilot implementation, so this list is exactly
    // what resources/i18n/de.txt was copied verbatim to satisfy.
    const juce::StringArray expectedFrameKeys {
        "Init",
        "Factory",
        "User",
        "Set current as default",
        "Save",
        "Save As...",
        "Delete",
        "Import...",
        "Export...",
        "Enter a name for the new preset:",
        "Preset name",
        "Cancel",
        "Import a preset or preset bank...",
        "Import failed",
        "Export preset...",
        "This file is not a valid preset.",
        "This preset was saved by an incompatible version of the preset format.",
        "This preset file belongs to a different plugin.",
    };

    // A representative sample of Miserere's own DSP/parameter terminology
    // (module names, parameter labels and units from
    // src/params/ParameterLayout.cpp) - per the binding spec, NONE of these
    // may ever appear as a translated key.
    const juce::StringArray dspTermsThatMustNeverBeTranslated {
        "In Trim", "Out Trim", "Bypass", "Link", "Parallel",
        "Crush Input", "Crush Ratio", "Crush Style", "Crush Attack", "Crush Release", "Crush Output",
        "Sand Peak Reduction", "Sand Limit", "Sand Emphasis", "Sand Residual",
        "Spread Detune", "Spread Time", "Spread Width",
        "Slap Time", "Slap Stereo", "Slap Tone",
        "Direct FET Threshold", "Direct EQ Drive", "Direct Sat Drive",
        "Attack", "Release", "Threshold", "Ratio", "Mix", "Level", "Drive", "Hz", "dB", "%",
    };
}

TEST_CASE ("Localisation: resources/i18n/de.txt parses as a well-formed German LocalisedStrings mapping", "[i18n]")
{
    const auto translations = parseDeTranslations();

    CHECK (translations.getLanguageName().equalsIgnoreCase ("German"));
    CHECK (translations.getCountryCodes().contains ("de"));
    CHECK (translations.getMappings().size() > 0);
}

TEST_CASE ("Localisation: every PresetBar/PresetManager frame key has a German translation in de.txt", "[i18n]")
{
    const auto translations = parseDeTranslations();

    for (const auto& key : expectedFrameKeys)
    {
        CAPTURE (key);
        const auto translated = translations.translate (key, juce::String());
        CHECK (translated.isNotEmpty());
    }

    // The mapping shouldn't carry stray keys beyond what PresetBar/
    // PresetManager actually use - a mismatch here would mean either a
    // missing frame string (caught above) or a leftover/typo'd key.
    CHECK (translations.getMappings().size() == expectedFrameKeys.size());
}

TEST_CASE ("Localisation: DSP/parameter terminology is verifiably NOT present in de.txt's mapping", "[i18n]")
{
    const auto translations = parseDeTranslations();
    const auto& keys = translations.getMappings().getAllKeys();

    for (const auto& term : dspTermsThatMustNeverBeTranslated)
    {
        CAPTURE (term);
        CHECK_FALSE (keys.contains (term));
    }
}
