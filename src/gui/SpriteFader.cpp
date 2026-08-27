#include "SpriteFader.h"

#include "KeyboardSteps.h"

namespace basilica::gui
{
    namespace
    {
        constexpr int normalDragSensitivity = 250;
        constexpr int fineDragSensitivity = normalDragSensitivity * 8;
    }

    SpriteFader::SpriteFader (const juce::Image& trackSprite, const juce::Image& capSprite,
                              const TrackGeometry& geometryIn)
        : juce::Slider (juce::Slider::LinearVertical, juce::Slider::NoTextBox),
          cleanTrack (buildCleanTrack (trackSprite, geometryIn.bakedCapRect)),
          capImage (capSprite),
          geometry (geometryIn)
    {
        setMouseDragSensitivity (normalDragSensitivity);
        setScrollWheelEnabled (true);
        setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);

        // Keyboard navigation (WCAG 2.1.1): juce::Slider ships with
        // setWantsKeyboardFocus(false) - see SpriteKnob.
        setWantsKeyboardFocus (true);
    }

    SpriteFader::~SpriteFader() = default;

    float SpriteFader::capCentreYForProportion (double normalisedValue, float travelTopY,
                                                float travelBottomY) noexcept
    {
        const auto clamped = (float) juce::jlimit (0.0, 1.0, normalisedValue);
        return travelBottomY + clamped * (travelTopY - travelBottomY);
    }

    juce::Image SpriteFader::buildCleanTrack (const juce::Image& trackSprite,
                                              juce::Rectangle<int> bakedCapRect)
    {
        if (! trackSprite.isValid())
            return {};

        auto clean = trackSprite.createCopy();

        if (bakedCapRect.isEmpty())
            return clean;

        juce::Image::BitmapData src (trackSprite, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData dst (clean, juce::Image::BitmapData::writeOnly);

        // Clone the slot band directly below the baked cap over the cap
        // region (the slot is vertically uniform). Rows past the sprite's
        // bottom fall back to mirroring the band above instead.
        const auto capHeight = bakedCapRect.getHeight();

        for (int y = bakedCapRect.getY(); y < bakedCapRect.getBottom(); ++y)
        {
            auto sourceY = y + capHeight;

            if (sourceY >= trackSprite.getHeight())
                sourceY = bakedCapRect.getY() - (y - bakedCapRect.getY()) - 1;

            sourceY = juce::jlimit (0, trackSprite.getHeight() - 1, sourceY);

            for (int x = bakedCapRect.getX(); x < bakedCapRect.getRight(); ++x)
            {
                const auto clampedX = juce::jlimit (0, trackSprite.getWidth() - 1, x);
                dst.setPixelColour (clampedX, y, src.getPixelColour (clampedX, sourceY));
            }
        }

        return clean;
    }

    void SpriteFader::paint (juce::Graphics& g)
    {
        if (! cleanTrack.isValid())
            return;

        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

        const auto bounds = getLocalBounds().toFloat();
        const auto scale = bounds.getHeight() / (float) cleanTrack.getHeight();

        // 1. The reconstructed empty track at its baked lighting.
        g.drawImageTransformed (cleanTrack, juce::AffineTransform::scale (scale));

        // 2. The cap at the live position, centred on the slot.
        if (capImage.isValid())
        {
            const auto proportion = valueToProportionOfLength (getValue());
            const auto capCy = capCentreYForProportion (proportion, geometry.travelTopY,
                                                        geometry.travelBottomY);

            const auto transform =
                juce::AffineTransform::translation (-(float) capImage.getWidth() * 0.5f,
                                                    -(float) capImage.getHeight() * 0.5f)
                    .scaled (scale)
                    .translated (geometry.slotCx * scale, capCy * scale);

            g.drawImageTransformed (capImage, transform);
        }

        // WCAG 2.4.7 Focus Visible: paint() fully replaces the LookAndFeel
        // path, so the focus ring is drawn here.
        if (hasKeyboardFocus (true))
        {
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.drawRoundedRectangle (bounds.reduced (1.0f), 4.0f, 1.5f);
        }
    }

    bool SpriteFader::keyPressed (const juce::KeyPress& key)
    {
        return handleSliderKeyPress (*this, key) || juce::Slider::keyPressed (key);
    }

    void SpriteFader::mouseDown (const juce::MouseEvent& e)
    {
        setMouseDragSensitivity (e.mods.isShiftDown() ? fineDragSensitivity : normalDragSensitivity);
        Slider::mouseDown (e);
    }

    void SpriteFader::mouseDrag (const juce::MouseEvent& e)
    {
        setMouseDragSensitivity (e.mods.isShiftDown() ? fineDragSensitivity : normalDragSensitivity);
        Slider::mouseDrag (e);
    }
}
