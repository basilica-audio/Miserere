#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Suite-reusable vertical fader for the wave-3 "composited plate" GUI
// generation (DECISIONS.md D3: dark recessed pinstriped slot, knurled
// brass cap with an engraved pointer line).
//
// SPRITE-GAP WORKAROUND (documented in the sprite library's
// provenance.md): D3's intended master variant pair (caps at 0 dB vs caps
// at the bottom of the travel) was never rendered - wave 1 produced no
// accepted miserere base - so no CLEAN empty track exists. The accepted
// nearest-sprite workaround is cap-over-track compositing from the two
// existing crops:
//
//   1. the track sprite still carries its baked cap at ~50% travel. A
//      clean track is reconstructed at construction by cloning the
//      slot band directly below the baked-cap region over it - a pure
//      pixel copy from the SAME approved sprite (the slot's appearance is
//      vertically uniform), the same never-repaint/never-invent rule the
//      SpriteToggle mirror and SpriteKnob inner-disc crops follow.
//   2. the extracted cap sprite is then composited over the clean track
//      at the live parameter position.
//
// When the D3 variant-pair render lands, the clean-track reconstruction
// simply becomes unnecessary (constructor takes the clean asset, nothing
// else changes).
//
// All geometry (baked-cap patch rect, cap-centre travel limits, slot
// centre x) is measured per sprite and carried by the plugin's layout
// manifest, exactly like SpriteKnob's centre/radius geometry.
namespace basilica::gui
{
    class SpriteFader : public juce::Slider
    {
    public:
        struct TrackGeometry
        {
            juce::Rectangle<int> bakedCapRect; // baked cap region to patch, in track px
            float travelTopY = 0;              // cap-centre y at maximum value, in track px
            float travelBottomY = 0;           // cap-centre y at minimum value, in track px
            float slotCx = 0;                  // slot centre x, in track px
        };

        SpriteFader (const juce::Image& trackSprite, const juce::Image& capSprite,
                     const TrackGeometry& geometry);
        ~SpriteFader() override;

        void paint (juce::Graphics& g) override;

        // WCAG 2.1.1 Keyboard: WAI-ARIA-style stepping via KeyboardSteps.h
        // (juce::Slider's own keyPressed steps by the raw interval and
        // swallows Shift - see SpriteKnob for the family rationale).
        bool keyPressed (const juce::KeyPress& key) override;

        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;

        // Cap-centre y in track px for a normalised value - the mapping
        // under test in tests/gui/SpriteFaderTests.cpp. Value 1 sits at
        // travelTopY (fader up = maximum, hardware convention).
        static float capCentreYForProportion (double normalisedValue, float travelTopY,
                                              float travelBottomY) noexcept;

        // Clean-track reconstruction (see class comment), exposed as an
        // independently testable static.
        static juce::Image buildCleanTrack (const juce::Image& trackSprite,
                                            juce::Rectangle<int> bakedCapRect);

    private:
        juce::Image cleanTrack;
        juce::Image capImage;
        TrackGeometry geometry;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpriteFader)
    };
}
