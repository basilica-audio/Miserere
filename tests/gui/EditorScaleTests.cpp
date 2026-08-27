#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

// Stepped window scaling (75/100/150/200%) - the photoreal-family
// convention (no free resize with prerendered assets). Persistence rides
// on the family's shared APVTS root property ("editorScale", a double), so stored sessions keep round-tripping and
// arbitrary stored values snap to the nearest step.

TEST_CASE ("A fresh instance opens at 100% and its own design size", "[gui][scale]")
{
    MiserereAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    CHECK_FALSE (processor.apvts.state.hasProperty (MiserereAudioProcessorEditor::getScaleStatePropertyId()));
    CHECK (MiserereAudioProcessorEditor::readPersistedScaleStepIndex (processor.apvts.state)
           == MiserereAudioProcessorEditor::defaultScaleStepIndex);

    MiserereAudioProcessorEditor editor (processor);

    CHECK (editor.getEditorScale() == Catch::Approx (1.0f));
    CHECK (editor.getWidth() == editor.getDesignWidth());
    CHECK (editor.getHeight() == editor.getDesignHeight());
}

TEST_CASE ("Applying a scale step resizes the window by exactly that factor", "[gui][scale]")
{
    MiserereAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    MiserereAudioProcessorEditor editor (processor);

    for (size_t step = 0; step < MiserereAudioProcessorEditor::scaleSteps.size(); ++step)
    {
        editor.applyScaleStep ((int) step);

        const auto scale = MiserereAudioProcessorEditor::scaleSteps[step];
        CHECK (editor.getWidth() == (int) std::lround ((float) editor.getDesignWidth() * scale));
        CHECK (editor.getHeight() == (int) std::lround ((float) editor.getDesignHeight() * scale));
    }
}

TEST_CASE ("Out-of-range step indices clamp instead of crashing", "[gui][scale]")
{
    MiserereAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    MiserereAudioProcessorEditor editor (processor);

    editor.applyScaleStep (-3);
    CHECK (editor.getScaleStepIndex() == 0);

    editor.applyScaleStep (99);
    CHECK (editor.getScaleStepIndex() == (int) MiserereAudioProcessorEditor::scaleSteps.size() - 1);
}

TEST_CASE ("Continuous legacy scale values snap to the nearest step", "[gui][scale][state]")
{
    MiserereAudioProcessor processor;

    struct Expectation
    {
        double stored;
        int expectedStep;
    };

    // The shared family property may carry any continuous value.
    const std::vector<Expectation> expectations = {
        { 0.6, 0 }, { 0.8, 0 }, { 0.95, 1 }, { 1.0, 1 }, { 1.2, 1 },
        { 1.4, 2 }, { 1.5, 2 }, { 1.72, 2 }, { 1.9, 3 }, { 2.0, 3 }, { 5.0, 3 },
    };

    for (const auto& expectation : expectations)
    {
        processor.apvts.state.setProperty (MiserereAudioProcessorEditor::getScaleStatePropertyId(),
                                           expectation.stored, nullptr);

        INFO ("stored scale: " << expectation.stored);
        CHECK (MiserereAudioProcessorEditor::readPersistedScaleStepIndex (processor.apvts.state)
               == expectation.expectedStep);
    }
}

TEST_CASE ("The chosen scale survives a full plugin-state round trip", "[gui][scale][state]")
{
    juce::MemoryBlock savedState;

    {
        MiserereAudioProcessor processor;
        processor.prepareToPlay (48000.0, 512);

        MiserereAudioProcessorEditor editor (processor);
        editor.applyScaleStep (2); // 150%

        processor.getStateInformation (savedState);
    }

    MiserereAudioProcessor restored;
    restored.setStateInformation (savedState.getData(), (int) savedState.getSize());

    CHECK (MiserereAudioProcessorEditor::readPersistedScaleStepIndex (restored.apvts.state) == 2);

    MiserereAudioProcessorEditor editor (restored);
    CHECK (editor.getEditorScale() == Catch::Approx (1.5f));
    CHECK (editor.getWidth() == (int) std::lround ((float) editor.getDesignWidth() * 1.5f));
}

TEST_CASE ("Persisting the scale does not disturb parameter state", "[gui][scale][state]")
{
    MiserereAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    auto* inTrim = processor.apvts.getParameter ("in_trim");
    REQUIRE (inTrim != nullptr);

    inTrim->setValueNotifyingHost (0.73f);

    MiserereAudioProcessorEditor editor (processor);
    editor.applyScaleStep (3);

    CHECK (inTrim->getValue() == Catch::Approx (0.73f).margin (1.0e-4));
}
