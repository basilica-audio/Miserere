#include "NeedleMeter.h"

#include "BasilicaLookAndFeel.h"

#include <array>
#include <cmath>

namespace
{
    struct Tick
    {
        float gainReductionDb;
        float deg; // clockwise from straight-up
    };

    // Engraved GR scale: 0 dB at the right-hand rest position, deepening
    // reduction sweeping left. The spacing compresses towards the deep end
    // (classic GR-meter feel: the first few dB get the most arc), which is
    // why this is a measured-feel piecewise table rather than one linear
    // ramp - the same table drives BOTH the painted tick marks and the
    // needle angle, so face and needle can never drift apart.
    constexpr std::array<Tick, 5> ticks {
        Tick { 0.0f, 50.0f },
        Tick { 3.0f, 20.0f },
        Tick { 6.0f, -5.0f },
        Tick { 10.0f, -28.0f },
        Tick { 20.0f, -50.0f },
    };

    // Geometry shared by paint(): the needle hub sits below the visible
    // face bottom so the arc reads as a window onto a larger dial.
    constexpr float hubYFraction = 1.25f;      // hub centre, fraction of component height
    constexpr float needleLengthFraction = 1.08f; // needle length, fraction of component height
}

namespace basilica::gui
{
    NeedleMeter::NeedleMeter (juce::String accessibleTitle, juce::String faceLegend)
        : title (std::move (accessibleTitle)), legend (std::move (faceLegend))
    {
        setTitle (title);
        setDescription (title);

        // Pure display: never focusable, never steals mouse events from
        // controls near it.
        setWantsKeyboardFocus (false);
        setInterceptsMouseClicks (false, false);
    }

    NeedleMeter::~NeedleMeter() = default;

    float NeedleMeter::angleDegreesForGainReductionDb (float gainReductionDb) noexcept
    {
        if (! std::isfinite (gainReductionDb) || gainReductionDb <= ticks.front().gainReductionDb)
            return ticks.front().deg;

        if (gainReductionDb >= ticks.back().gainReductionDb)
            return ticks.back().deg;

        for (size_t i = 1; i < ticks.size(); ++i)
        {
            if (gainReductionDb <= ticks[i].gainReductionDb)
            {
                const auto& lo = ticks[i - 1];
                const auto& hi = ticks[i];
                const auto span = hi.gainReductionDb - lo.gainReductionDb;
                const auto t = span > 0.0f ? (gainReductionDb - lo.gainReductionDb) / span : 0.0f;
                return lo.deg + t * (hi.deg - lo.deg);
            }
        }

        return ticks.back().deg;
    }

    float NeedleMeter::stepBallistics (float currentSmoothed, float target,
                                       float dtSeconds, float tauSeconds) noexcept
    {
        // Sanitise: a NaN/inf reading (defensive - should never happen, the
        // engine clamps its own metering) must not poison the smoothed
        // state, and a poisoned current state must be recoverable.
        if (! std::isfinite (target))
            target = 0.0f;

        if (! std::isfinite (currentSmoothed))
            return target;

        if (tauSeconds <= 0.0f || dtSeconds <= 0.0f)
            return target;

        const auto alpha = 1.0f - std::exp (-dtSeconds / tauSeconds);
        return currentSmoothed + (target - currentSmoothed) * alpha;
    }

    void NeedleMeter::tick (float dtSeconds) noexcept
    {
        const auto target = targetDb.load (std::memory_order_relaxed);
        const auto next = stepBallistics (smoothedDb, target, dtSeconds, ballisticsTauSeconds);

        if (! juce::approximatelyEqual (next, smoothedDb))
        {
            smoothedDb = next;
            repaint();
        }
    }

    void NeedleMeter::setImmediateDbForPreview (float db) noexcept
    {
        targetDb.store (db, std::memory_order_relaxed);
        smoothedDb = std::isfinite (db) ? db : 0.0f;
        repaint();
    }

    void NeedleMeter::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat();

        // --- Recessed face ---------------------------------------------
        g.setColour (BasilicaLookAndFeel::getMeterFaceColour());
        g.fillRoundedRectangle (bounds, 6.0f);

        g.setColour (BasilicaLookAndFeel::getMeterMarkingColour().withAlpha (0.6f));
        g.drawRoundedRectangle (bounds.reduced (0.75f), 6.0f, 1.5f);

        // Clip everything dial-related to the face so the below-face hub
        // geometry never paints outside the meter.
        g.saveState();
        g.reduceClipRegion (bounds.reduced (2.0f).toNearestInt());

        const auto hubX = bounds.getCentreX();
        const auto hubY = bounds.getHeight() * hubYFraction;
        const auto arcOuter = bounds.getHeight() * (needleLengthFraction + 0.06f);
        const auto arcInner = arcOuter - juce::jmin (8.0f, bounds.getHeight() * 0.09f);

        // --- Engraved arc + ticks + numerals ---------------------------
        g.setColour (BasilicaLookAndFeel::getMeterMarkingColour());

        juce::Path arc;
        arc.addCentredArc (hubX, hubY, arcInner, arcInner,
                           0.0f,
                           juce::degreesToRadians (ticks.back().deg),
                           juce::degreesToRadians (ticks.front().deg),
                           true);
        g.strokePath (arc, juce::PathStrokeType (1.2f));

        const auto numeralFont = BasilicaLookAndFeel::getSerifFont (juce::jmax (10.0f, bounds.getHeight() * 0.12f));
        g.setFont (numeralFont);

        for (const auto& tick : ticks)
        {
            const auto angle = juce::degreesToRadians (tick.deg);
            const auto sinA = std::sin (angle);
            const auto cosA = std::cos (angle);

            juce::Path tickPath;
            tickPath.startNewSubPath (hubX + arcInner * sinA, hubY - arcInner * cosA);
            tickPath.lineTo (hubX + arcOuter * sinA, hubY - arcOuter * cosA);
            g.strokePath (tickPath, juce::PathStrokeType (1.4f));

            // Numeral just inside the arc.
            const auto numeralRadius = arcInner - numeralFont.getHeight() * 0.75f;
            const auto numeralCentreX = hubX + numeralRadius * sinA;
            const auto numeralCentreY = hubY - numeralRadius * cosA;
            g.drawSingleLineText (juce::String ((int) tick.gainReductionDb),
                                  (int) std::lround (numeralCentreX),
                                  (int) std::lround (numeralCentreY),
                                  juce::Justification::horizontallyCentred);
        }

        // Legend (bus name + unit) engraved below the arc's centre.
        g.setFont (BasilicaLookAndFeel::getSerifFont (juce::jmax (10.0f, bounds.getHeight() * 0.13f), true));
        g.drawText (legend + "  GR dB",
                    bounds.withTrimmedBottom (bounds.getHeight() * 0.06f),
                    juce::Justification::centredBottom);

        // --- Needle -----------------------------------------------------
        const auto needleAngle = juce::degreesToRadians (angleDegreesForGainReductionDb (smoothedDb));
        const auto needleLength = bounds.getHeight() * needleLengthFraction;

        juce::Path needle;
        needle.startNewSubPath (hubX, hubY);
        needle.lineTo (hubX + needleLength * std::sin (needleAngle),
                       hubY - needleLength * std::cos (needleAngle));

        g.setColour (BasilicaLookAndFeel::getMeterNeedleColour());
        g.strokePath (needle, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));

        g.restoreState();
    }

    // Read-only text value interface exposing the current ballistic-smoothed
    // reading - the suite's AnalogMeter/HubNeedle A-07 pattern (JUCE 8.0.14
    // juce::AccessibilityTextValueInterface).
    class NeedleMeter::ValueInterface final : public juce::AccessibilityTextValueInterface
    {
    public:
        explicit ValueInterface (const NeedleMeter& ownerIn) noexcept : owner (ownerIn) {}

        bool isReadOnly() const override { return true; }

        juce::String getCurrentValueAsString() const override
        {
            return juce::String (owner.smoothedDb, 1) + " dB";
        }

        void setValueAsString (const juce::String&) override {}

    private:
        const NeedleMeter& owner;
    };

    std::unique_ptr<juce::AccessibilityHandler> NeedleMeter::createAccessibilityHandler()
    {
        return std::make_unique<juce::AccessibilityHandler> (
            *this,
            juce::AccessibilityRole::label,
            juce::AccessibilityActions {},
            juce::AccessibilityHandler::Interfaces { std::make_unique<ValueInterface> (*this) });
    }
}
