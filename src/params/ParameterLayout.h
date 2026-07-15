#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

// Builds the complete v0.1 AudioProcessorValueTreeState parameter layout.
// Extracted from the processor into its own translation unit so it can be
// unit-tested in isolation (SharedCode target) without instantiating the
// full AudioProcessor. See ParameterIds.h for the frozen-ID contract this
// function must honour: IDs never change, ranges/defaults may be tuned.
namespace msrr
{
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Frequency choice lists shared between ParameterLayout.cpp (which builds
    // the AudioParameterChoice objects) and PluginProcessor.cpp (which maps
    // the chosen index back to a concrete Hz value for the DSP engine) - kept
    // in one place so the two can never drift apart.
    extern const juce::StringArray busBLowBoostFreqChoices;
    extern const juce::StringArray busBHighBoostFreqChoices;

    extern const std::array<float, 2> busBLowBoostFreqHz;
    extern const std::array<float, 4> busBHighBoostFreqHz;

    extern const juce::StringArray compRatioChoices;
    extern const std::array<float, 2> compRatioValues;
}
