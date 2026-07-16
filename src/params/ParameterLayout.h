#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

// Builds the complete v0.2 (v2 topology) AudioProcessorValueTreeState
// parameter layout. Extracted from the processor into its own translation
// unit so it can be unit-tested in isolation (SharedCode target) without
// instantiating the full AudioProcessor. See ParameterIds.h for the frozen-
// ID contract this function must honour: IDs never change again without a
// further breaking-version bump, ranges/defaults/skew may still be tuned.
namespace msrr
{
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Choice-index -> concrete-value tables shared between
    // ParameterLayout.cpp (which builds the AudioParameterChoice objects)
    // and PluginProcessor.cpp (which maps the chosen index back to a
    // concrete Hz/enum value for the DSP engine) - kept in one place so the
    // layout strings and the DSP mapping can never drift apart.
    extern const juce::StringArray crushRatioChoices;
    extern const juce::StringArray crushStyleChoices;

    extern const juce::StringArray eqHpfFreqChoices;
    extern const std::array<float, 4> eqHpfFreqHz;
    extern const juce::StringArray eqLowFreqChoices;
    extern const std::array<float, 4> eqLowFreqHz;
    extern const juce::StringArray eqMidFreqChoices;
    extern const std::array<float, 6> eqMidFreqHz;

    extern const juce::StringArray sandLfFreqChoices;
    extern const std::array<float, 4> sandLfFreqHz;
    extern const juce::StringArray sandHfBellFreqChoices;
    extern const std::array<float, 7> sandHfBellFreqHz;
    extern const juce::StringArray sandHfShelfFreqChoices;
    extern const std::array<float, 3> sandHfShelfFreqHz;
}
