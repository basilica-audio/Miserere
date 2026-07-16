#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/MiserereEngine.h"

// Miserere v2: the parallel vocal template - a Direct (serial, all-off-by-
// default) path plus four parallel return busses (Crush, Sandwich, Spread,
// Slap), each with its own fader. The whole signal path lives in
// MiserereEngine (src/dsp) so it stays unit-testable independent of this
// AudioProcessor; this class is APVTS + host plumbing (parameter fan-out,
// oversized-block chunking, audition exclusivity) around it. See
// docs/architecture.md for the signal-flow diagram and docs/design-brief.md
// for the binding v2 spec.
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

    // GR metering values for a future GUI and for tests.
    float getDirectFetGainReductionDb() const noexcept { return engine.getDirectFetGainReductionDb(); }
    float getCrushGainReductionDb() const noexcept { return engine.getCrushGainReductionDb(); }
    float getSandGainReductionDb() const noexcept { return engine.getSandGainReductionDb(); }

private:
    //==============================================================================
    // Audition exclusivity (brief: "exclusive Audition"): engaging one
    // bus's audition releases the other three, enforced here at the
    // parameter level via this APVTS listener (the engine itself resolves
    // whatever flag combination it is given - see MiserereEngine.h). The
    // reentrancy guard stops the cascade of setValueNotifyingHost() calls
    // from re-triggering this handler for the auditions it is itself
    // clearing.
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

    std::atomic<bool> auditionExclusivityGuard { false };

    // Raw atomic pointers into the APVTS-managed parameter values, resolved
    // once at construction time so processBlock() never has to search for
    // them (no allocation/locks on the audio thread).
    std::atomic<float>* inTrimDb = nullptr;
    std::atomic<float>* outTrimDb = nullptr;
    std::atomic<float>* bypassFlag = nullptr;
    std::atomic<float>* linkFlag = nullptr;
    std::atomic<float>* parallelTrimDb = nullptr;

    std::atomic<float>* directDeessPreEnabled = nullptr;
    std::atomic<float>* directDeessPreFreq = nullptr;
    std::atomic<float>* directDeessPreThreshold = nullptr;
    std::atomic<float>* directFetEnabled = nullptr;
    std::atomic<float>* directFetThreshold = nullptr;
    std::atomic<float>* directFetAttack = nullptr;
    std::atomic<float>* directFetRelease = nullptr;
    std::atomic<float>* directFetMakeup = nullptr;
    std::atomic<float>* directEqHpfEnabled = nullptr;
    std::atomic<float>* directEqHpfFreq = nullptr;
    std::atomic<float>* directEqLowFreq = nullptr;
    std::atomic<float>* directEqLowGain = nullptr;
    std::atomic<float>* directEqMidFreq = nullptr;
    std::atomic<float>* directEqMidGain = nullptr;
    std::atomic<float>* directEqHighGain = nullptr;
    std::atomic<float>* directEqDrive = nullptr;
    std::atomic<float>* directSatDrive = nullptr;
    std::atomic<float>* directDeessPostEnabled = nullptr;
    std::atomic<float>* directDeessPostFreq = nullptr;
    std::atomic<float>* directDeessPostThreshold = nullptr;

    std::atomic<float>* crushInput = nullptr;
    std::atomic<float>* crushRatio = nullptr;
    std::atomic<float>* crushStyle = nullptr;
    std::atomic<float>* crushAttack = nullptr;
    std::atomic<float>* crushRelease = nullptr;
    std::atomic<float>* crushOutput = nullptr;

    std::atomic<float>* sandPreLfFreq = nullptr;
    std::atomic<float>* sandPreLfBoost = nullptr;
    std::atomic<float>* sandPreLfCut = nullptr;
    std::atomic<float>* sandPreHfBellFreq = nullptr;
    std::atomic<float>* sandPreHfBellBoost = nullptr;
    std::atomic<float>* sandPreHfBellBandwidth = nullptr;
    std::atomic<float>* sandPreHfShelfFreq = nullptr;
    std::atomic<float>* sandPreHfShelfAtten = nullptr;
    std::atomic<float>* sandPeakRed = nullptr;
    std::atomic<float>* sandLimit = nullptr;
    std::atomic<float>* sandEmphasis = nullptr;
    std::atomic<float>* sandResidual = nullptr;
    std::atomic<float>* sandPostLfFreq = nullptr;
    std::atomic<float>* sandPostLfBoost = nullptr;
    std::atomic<float>* sandPostLfCut = nullptr;
    std::atomic<float>* sandPostHfBellFreq = nullptr;
    std::atomic<float>* sandPostHfBellBoost = nullptr;
    std::atomic<float>* sandPostHfBellBandwidth = nullptr;
    std::atomic<float>* sandPostHfShelfFreq = nullptr;
    std::atomic<float>* sandPostHfShelfAtten = nullptr;

    std::atomic<float>* spreadDetune = nullptr;
    std::atomic<float>* spreadTime = nullptr;
    std::atomic<float>* spreadWidth = nullptr;

    std::atomic<float>* slapTime = nullptr;
    std::atomic<float>* slapStereo = nullptr;
    std::atomic<float>* slapTone = nullptr;

    std::array<std::atomic<float>*, MiserereEngine::numBusses> busLevelDb {};
    std::array<std::atomic<float>*, MiserereEngine::numBusses> busMuteFlag {};
    std::array<std::atomic<float>*, MiserereEngine::numBusses> busAuditionFlag {};

    juce::RangedAudioParameter* bypassParameter = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MiserereAudioProcessor)
};
