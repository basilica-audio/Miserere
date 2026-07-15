#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/MiserereEngine.h"

// Miserere: a parallel vocal chain in a single unit - a Direct chain plus
// three parallel busses (Opto sandwich, FET Smash, Slap delay), each with
// its own fader. The whole signal path lives in MiserereEngine (src/dsp) so
// it stays unit-testable independent of this AudioProcessor; this class is
// APVTS + host plumbing (parameter fan-out, oversized-block chunking, solo
// exclusivity) around it. See docs/architecture.md for the signal-flow
// diagram and docs/design-brief.md for the binding M1 spec.
class MiserereAudioProcessor final : public juce::AudioProcessor,
                                      private juce::AudioProcessorValueTreeState::Listener
{
public:
    MiserereAudioProcessor();
    ~MiserereAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorParameter* getBypassParameter() const override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    // GR metering values for a future GUI (M3) and for tests.
    float getDirectCompGainReductionDb() const noexcept { return engine.getDirectCompGainReductionDb(); }
    float getOptoGainReductionDb() const noexcept { return engine.getOptoGainReductionDb(); }
    float getSmashGainReductionDb() const noexcept { return engine.getSmashGainReductionDb(); }

private:
    //==============================================================================
    // Solo exclusivity (brief: "solo is exclusive-OR"): engaging one bus's
    // solo releases the other three, enforced here at the parameter level
    // via this APVTS listener (the engine itself resolves whatever flag
    // combination it is given - see MiserereEngine.h). The reentrancy guard
    // stops the cascade of setValueNotifyingHost() calls from re-triggering
    // this handler for the solos it is itself clearing.
    void parameterChanged (const juce::String& parameterId, float newValue) override;

    // Pushes every APVTS value into the engine. Called from prepareToPlay()
    // (so the first block after a session load already reflects the real
    // parameter state) and once per processBlock().
    void updateEngineParameters() noexcept;

    //==============================================================================
    MiserereEngine engine;

    // Oversized-block guard: hosts are expected never to exceed the block
    // size promised to prepareToPlay(), but if one does, processBlock()
    // splits the buffer into chunks of at most this size - a REAL clamp
    // that still processes all audio, not a jassert that compiles out of
    // Release builds.
    int preparedBlockSize = 0;

    std::atomic<bool> soloExclusivityGuard { false };

    // Raw atomic pointers into the APVTS-managed parameter values, resolved
    // once at construction time so processBlock() never has to search for
    // them (no allocation/locks on the audio thread).
    std::atomic<float>* inTrimDb = nullptr;
    std::atomic<float>* outTrimDb = nullptr;
    std::atomic<float>* bypassFlag = nullptr;

    std::atomic<float>* busAHpfEnabled = nullptr;
    std::atomic<float>* busAHpfFreq = nullptr;
    std::atomic<float>* busAEqLowGain = nullptr;
    std::atomic<float>* busAEqMidFreq = nullptr;
    std::atomic<float>* busAEqMidGain = nullptr;
    std::atomic<float>* busAEqMidQ = nullptr;
    std::atomic<float>* busAEqHighGain = nullptr;
    std::atomic<float>* busACompRatio = nullptr;
    std::atomic<float>* busACompThreshold = nullptr;
    std::atomic<float>* busACompAttack = nullptr;
    std::atomic<float>* busACompRelease = nullptr;
    std::atomic<float>* busACompMakeup = nullptr;
    std::atomic<float>* busADeessEnabled = nullptr;
    std::atomic<float>* busADeessFreq = nullptr;
    std::atomic<float>* busADeessThreshold = nullptr;
    std::atomic<float>* busASatDrive = nullptr;

    std::atomic<float>* busBLowBoostFreq = nullptr;
    std::atomic<float>* busBLowBoostGain = nullptr;
    std::atomic<float>* busBHighBoostFreq = nullptr;
    std::atomic<float>* busBHighBoostGain = nullptr;
    std::atomic<float>* busBOptoReduction = nullptr;
    std::atomic<float>* busBOptoMakeup = nullptr;
    std::atomic<float>* busBAirGain = nullptr;

    std::atomic<float>* busCAttack = nullptr;
    std::atomic<float>* busCRelease = nullptr;
    std::atomic<float>* busCDrive = nullptr;
    std::atomic<float>* busCOutputTrim = nullptr;

    std::atomic<float>* busDDelayMs = nullptr;
    std::atomic<float>* busDFeedback = nullptr;
    std::atomic<float>* busDHpFreq = nullptr;
    std::atomic<float>* busDLpFreq = nullptr;
    std::atomic<float>* busDMono = nullptr;

    std::array<std::atomic<float>*, MiserereEngine::numBusses> busLevelDb {};
    std::array<std::atomic<float>*, MiserereEngine::numBusses> busMuteFlag {};
    std::array<std::atomic<float>*, MiserereEngine::numBusses> busSoloFlag {};

    juce::RangedAudioParameter* bypassParameter = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MiserereAudioProcessor)
};
