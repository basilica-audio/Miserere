#include "gui/SpriteFader.h"

#include <BinaryData.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// D3 bus fader statics (src/gui/SpriteFader.h): the value -> cap-position
// mapping and the documented clean-track reconstruction (the sprite
// library has no cap-free track yet - the baked cap is patched out by
// cloning the slot band below it).
namespace
{
    juce::Image trackSprite()
    {
        return juce::ImageCache::getFromMemory (BinaryData::sprite_fader_track_png,
                                                BinaryData::sprite_fader_track_pngSize);
    }

    // The baked-cap geometry the layout manifest carries for this sprite.
    const juce::Rectangle<int> bakedCapRect { 30, 102, 46, 56 };

    float meanBrightness (const juce::Image& image, juce::Rectangle<int> area)
    {
        float sum = 0;
        int count = 0;

        for (int y = area.getY(); y < area.getBottom(); ++y)
        {
            for (int x = area.getX(); x < area.getRight(); ++x)
            {
                const auto c = image.getPixelAt (x, y);

                if (c.getAlpha() > 100)
                {
                    sum += c.getFloatRed() + c.getFloatGreen() + c.getFloatBlue();
                    ++count;
                }
            }
        }

        return count > 0 ? sum / (float) count : 0.0f;
    }
}

TEST_CASE ("Cap centre travels linearly from bottom (0) to top (1)", "[gui][fader]")
{
    using basilica::gui::SpriteFader;

    constexpr float top = 68.0f, bottom = 210.0f;

    CHECK (SpriteFader::capCentreYForProportion (0.0, top, bottom) == Catch::Approx (bottom));
    CHECK (SpriteFader::capCentreYForProportion (1.0, top, bottom) == Catch::Approx (top));
    CHECK (SpriteFader::capCentreYForProportion (0.5, top, bottom) == Catch::Approx ((top + bottom) * 0.5f));

    // Out-of-range values clamp instead of overshooting the slot.
    CHECK (SpriteFader::capCentreYForProportion (-1.0, top, bottom) == Catch::Approx (bottom));
    CHECK (SpriteFader::capCentreYForProportion (2.0, top, bottom) == Catch::Approx (top));
}

TEST_CASE ("Clean-track reconstruction removes the baked brass cap", "[gui][fader]")
{
    const auto track = trackSprite();
    REQUIRE (track.isValid());

    // The shipped track sprite really does carry a baked cap: its region
    // is clearly brighter than the empty slot band below it.
    const auto below = bakedCapRect.translated (0, bakedCapRect.getHeight());
    REQUIRE (meanBrightness (track, bakedCapRect) > meanBrightness (track, below) + 0.15f);

    const auto clean = basilica::gui::SpriteFader::buildCleanTrack (track, bakedCapRect);
    REQUIRE (clean.isValid());
    CHECK (clean.getWidth() == track.getWidth());
    CHECK (clean.getHeight() == track.getHeight());

    // After patching, the former cap region reads like empty slot.
    CHECK (meanBrightness (clean, bakedCapRect)
           == Catch::Approx (meanBrightness (clean, below)).margin (0.06));

    // And pixels outside the patch are untouched.
    const auto above = juce::Rectangle<int> (bakedCapRect.getX(), 40, bakedCapRect.getWidth(), 40);
    CHECK (meanBrightness (clean, above) == Catch::Approx (meanBrightness (track, above)).margin (1.0e-4));
}
