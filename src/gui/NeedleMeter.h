#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>

// Per-bus gain-reduction needle meter for the M3 vector editor (issue #25).
//
// Fully vector-drawn (face, engraved arc, tick marks, legend, needle - all
// juce::Path/Graphics at runtime): the design descends from the suite's
// HubNeedle/AnalogMeter family (aureate -> requiem -> silentium), but where
// those rotate a Blender-rendered needle sprite over a baked dial face,
// Miserere has no photoreal assets at all, so both the face and the needle
// are drawn here.
//
// Threading model (same as HubNeedle): setTargetDb() is a plain relaxed
// atomic store, safe from any thread; ballistic smoothing runs on the GUI
// thread via tick(), driven by the editor's own timer (never a Timer owned
// here), so headless tests can advance it deterministically without a
// running message loop.
//
// Value convention: POSITIVE dB of gain reduction (0 = none), matching
// MiserereAudioProcessor::get{DirectFet,Crush,Sand}GainReductionDb(). The
// needle rests at the right-hand extreme at 0 dB GR and sweeps left as
// reduction deepens - classic GR-meter behaviour.
//
// Accessibility (A-07 pattern, WCAG 4.1.2): exposes a read-only
// AccessibilityTextValueInterface with the current smoothed reading as a
// dB string, queryable on demand - deliberately NOT announced on every
// repaint (see silentium's AnalogMeter docs for why auto-announce is the
// wrong behaviour for a meter). The component is display-only: it never
// takes keyboard focus and never intercepts mouse events.
namespace basilica::gui
{
    class NeedleMeter : public juce::Component
    {
    public:
        // accessibleTitle: e.g. "Crush gain reduction meter".
        // faceLegend: the short engraved legend on the face, e.g. "CRUSH".
        NeedleMeter (juce::String accessibleTitle, juce::String faceLegend);
        ~NeedleMeter() override;

        // Thread-safe (plain relaxed atomic store): the instantaneous
        // gain-reduction reading in dB (positive = reduction). Non-finite
        // values are stored as-is and sanitised at the ballistics step.
        void setTargetDb (float newTargetDb) noexcept
        {
            targetDb.store (newTargetDb, std::memory_order_relaxed);
        }

        // Advances the ballistic smoothing by dtSeconds and repaints if the
        // smoothed value changed meaningfully - called from the editor's
        // timer (see PluginEditor.cpp).
        void tick (float dtSeconds) noexcept;

        // Test/preview-only: seeds both the raw target and the smoothed
        // reading immediately, bypassing the ramp (headless test binaries
        // have no message loop to pump real ticks through).
        void setImmediateDbForPreview (float db) noexcept;

        float getSmoothedDb() const noexcept { return smoothedDb; }

        void paint (juce::Graphics& g) override;
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

        // One-pole ballistic integration step, pure/static so it is
        // directly unit-testable without a running timer. Non-finite
        // targets resolve to 0 dB (needle at rest) instead of poisoning
        // the smoothed state with NaN/inf.
        static float stepBallistics (float currentSmoothed, float target,
                                     float dtSeconds, float tauSeconds) noexcept;

        // dB of gain reduction -> needle angle in degrees, clockwise from
        // straight-up (12 o'clock). Piecewise-linear over the engraved tick
        // table (0/3/6/10/20 dB), clamped beyond both ends; monotonically
        // DECREASING (more reduction swings the needle further left).
        static float angleDegreesForGainReductionDb (float gainReductionDb) noexcept;

        static constexpr float ballisticsTauSeconds = 0.18f;

    private:
        class ValueInterface;

        const juce::String title;
        const juce::String legend;

        std::atomic<float> targetDb { 0.0f };
        float smoothedDb = 0.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeedleMeter)
    };
}
