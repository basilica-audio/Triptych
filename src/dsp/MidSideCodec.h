#pragma once

// Equal-power Mid/Side encode/decode for BandCompressor's per-band M/S
// processing (GitHub issue #24). M = (L + R) * k, S = (L - R) * k, with
// k = 1/sqrt(2) applied on *both* encode and decode - an exactly invertible
// transform (decodeMidSide(encodeMidSide(l, r)) == (l, r), up to
// floating-point rounding), unlike the "raw" M = L+R, S = L-R convention
// that only puts a 0.5 factor on decode. Kept as pure, allocation-free free
// functions (no DSP state) so the transform itself is directly unit-testable
// (tests/MidSideCodecTests.cpp) independent of BandCompressor's envelope/
// gain-computer behaviour - the same "pure math in its own header"
// convention KneeGainComputer.h/GateGainComputer.h use.
//
// Mono-compatibility guarantee: L + R == 2 * k * M identically, regardless
// of whatever processing is applied to S in between encode and decode -
// decode's L = (M+S)*k, R = (M-S)*k always cancel S out of the L+R sum
// algebraically. Attenuating/boosting the Side channel therefore can never
// introduce a phase-cancellation artifact into a mono downmix; only
// processing applied to Mid changes the mono sum, which is the intended,
// audible effect of compressing/expanding the centre content - see
// tests/MidSideCodecTests.cpp's "mono sum is independent of Side" test and
// docs/architecture.md's write-up of this feature.
namespace trpt
{
    inline constexpr float midSideEqualPowerGain = 0.70710678118654752f; // 1 / sqrt(2)

    inline void encodeMidSide (float left, float right, float& mid, float& side) noexcept
    {
        mid = (left + right) * midSideEqualPowerGain;
        side = (left - right) * midSideEqualPowerGain;
    }

    inline void decodeMidSide (float mid, float side, float& left, float& right) noexcept
    {
        left = (mid + side) * midSideEqualPowerGain;
        right = (mid - side) * midSideEqualPowerGain;
    }
}
