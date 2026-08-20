#include "gui/NeedleMeter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

// The M3 vector needle meter (issue #25): pure-function coverage of the
// ballistics and the dB->angle mapping (both static so no timer/message
// loop is needed), plus the display-only component contract.

TEST_CASE ("Ballistics step converges monotonically towards the target without overshoot", "[gui][meter]")
{
    using basilica::gui::NeedleMeter;

    float smoothed = 0.0f;
    constexpr float target = 10.0f;
    constexpr float dt = 1.0f / 30.0f;

    float previous = smoothed;

    // Strict monotonicity only over the first ~1.3 s of the ramp - once the
    // gap shrinks towards float epsilon the per-tick increment legitimately
    // underflows to zero, so asserting strict `>` all the way to
    // convergence would test float rounding, not the ballistics.
    for (int i = 0; i < 40; ++i)
    {
        smoothed = NeedleMeter::stepBallistics (previous, target, dt, NeedleMeter::ballisticsTauSeconds);

        CHECK (smoothed > previous);  // strictly rising towards the target...
        CHECK (smoothed <= target);   // ...never past it
        previous = smoothed;
    }

    for (int i = 0; i < 400; ++i)
        smoothed = NeedleMeter::stepBallistics (smoothed, target, dt, NeedleMeter::ballisticsTauSeconds);

    // ~14.7 s total at 30 Hz >> tau (0.18 s): must have converged.
    CHECK (smoothed == Catch::Approx (target).margin (1.0e-3));

    // Falling back to 0 is symmetric.
    for (int i = 0; i < 400; ++i)
        smoothed = NeedleMeter::stepBallistics (smoothed, 0.0f, dt, NeedleMeter::ballisticsTauSeconds);

    CHECK (smoothed == Catch::Approx (0.0f).margin (1.0e-3));
}

TEST_CASE ("Ballistics edge cases: zero dt/tau jump straight to the target", "[gui][meter]")
{
    using basilica::gui::NeedleMeter;

    CHECK (NeedleMeter::stepBallistics (2.0f, 8.0f, 0.0f, 0.18f) == Catch::Approx (8.0f));
    CHECK (NeedleMeter::stepBallistics (2.0f, 8.0f, -1.0f, 0.18f) == Catch::Approx (8.0f));
    CHECK (NeedleMeter::stepBallistics (2.0f, 8.0f, 0.033f, 0.0f) == Catch::Approx (8.0f));
}

TEST_CASE ("Ballistics sanitise non-finite inputs instead of propagating them", "[gui][meter]")
{
    using basilica::gui::NeedleMeter;
    constexpr auto nan = std::numeric_limits<float>::quiet_NaN();
    constexpr auto inf = std::numeric_limits<float>::infinity();

    // A NaN/inf TARGET resolves to 0 dB (needle at rest) - the step output
    // must stay finite whatever the engine hands over.
    CHECK (std::isfinite (NeedleMeter::stepBallistics (5.0f, nan, 0.033f, 0.18f)));
    CHECK (std::isfinite (NeedleMeter::stepBallistics (5.0f, inf, 0.033f, 0.18f)));
    CHECK (std::isfinite (NeedleMeter::stepBallistics (5.0f, -inf, 0.033f, 0.18f)));

    // A poisoned CURRENT state recovers to the (sanitised) target instead
    // of sticking at NaN forever.
    CHECK (NeedleMeter::stepBallistics (nan, 4.0f, 0.033f, 0.18f) == Catch::Approx (4.0f));
    CHECK (NeedleMeter::stepBallistics (nan, nan, 0.033f, 0.18f) == Catch::Approx (0.0f));
}

TEST_CASE ("dB->angle mapping hits the engraved ticks exactly and clamps beyond both ends", "[gui][meter]")
{
    using basilica::gui::NeedleMeter;

    // The tick table in NeedleMeter.cpp: 0/3/6/10/20 dB GR at
    // +50/+20/-5/-28/-50 degrees (needle rests right, sweeps left).
    CHECK (NeedleMeter::angleDegreesForGainReductionDb (0.0f) == Catch::Approx (50.0f));
    CHECK (NeedleMeter::angleDegreesForGainReductionDb (3.0f) == Catch::Approx (20.0f));
    CHECK (NeedleMeter::angleDegreesForGainReductionDb (6.0f) == Catch::Approx (-5.0f));
    CHECK (NeedleMeter::angleDegreesForGainReductionDb (10.0f) == Catch::Approx (-28.0f));
    CHECK (NeedleMeter::angleDegreesForGainReductionDb (20.0f) == Catch::Approx (-50.0f));

    // Clamped beyond the scale (and for nonsense negative "reduction").
    CHECK (NeedleMeter::angleDegreesForGainReductionDb (-5.0f) == Catch::Approx (50.0f));
    CHECK (NeedleMeter::angleDegreesForGainReductionDb (100.0f) == Catch::Approx (-50.0f));

    // Non-finite input rests the needle instead of producing a NaN angle.
    CHECK (NeedleMeter::angleDegreesForGainReductionDb (std::numeric_limits<float>::quiet_NaN())
           == Catch::Approx (50.0f));

    // Strictly monotonically decreasing across the live span - deeper
    // reduction always swings further left, no plateau or reversal.
    auto previous = NeedleMeter::angleDegreesForGainReductionDb (0.0f);

    for (float db = 0.25f; db <= 20.0f; db += 0.25f)
    {
        const auto angle = NeedleMeter::angleDegreesForGainReductionDb (db);
        CHECK (angle < previous);
        previous = angle;
    }
}

TEST_CASE ("tick() follows the atomic target and never lets NaN reach the smoothed reading", "[gui][meter]")
{
    basilica::gui::NeedleMeter meter ("Test gain reduction meter", "TEST");

    meter.setTargetDb (12.0f);

    for (int i = 0; i < 300; ++i)
        meter.tick (1.0f / 30.0f);

    CHECK (meter.getSmoothedDb() == Catch::Approx (12.0f).margin (1.0e-3));

    meter.setTargetDb (std::numeric_limits<float>::quiet_NaN());

    for (int i = 0; i < 300; ++i)
        meter.tick (1.0f / 30.0f);

    CHECK (std::isfinite (meter.getSmoothedDb()));
    CHECK (meter.getSmoothedDb() == Catch::Approx (0.0f).margin (1.0e-3));
}

TEST_CASE ("NeedleMeter is a display-only component with a titled, read-only accessible value", "[gui][meter][a11y]")
{
    basilica::gui::NeedleMeter meter ("Crush gain reduction meter", "CRUSH");

    CHECK (meter.getTitle() == "Crush gain reduction meter");
    CHECK_FALSE (meter.getWantsKeyboardFocus());

    bool clicksSelf = true, clicksChildren = true;
    meter.getInterceptsMouseClicks (clicksSelf, clicksChildren);
    CHECK_FALSE (clicksSelf);
    CHECK_FALSE (clicksChildren);

    // createAccessibilityHandler() (not getAccessibilityHandler()) - the
    // latter needs a live native peer (JUCE 8.0.14 juce_Component.cpp:
    // 3323-3326), which this headless binary never has.
    const auto handler = meter.createAccessibilityHandler();
    REQUIRE (handler != nullptr);

    auto* valueInterface = handler->getValueInterface();
    REQUIRE (valueInterface != nullptr);
    CHECK (valueInterface->isReadOnly());
    CHECK (valueInterface->getCurrentValueAsString() == "0.0 dB");

    meter.setImmediateDbForPreview (3.5f);
    CHECK (valueInterface->getCurrentValueAsString() == "3.5 dB");
}
