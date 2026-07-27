#pragma once

#include <atomic>

// Per-band gain-reduction telemetry (v0.5.0, .scaffold brief section 3.7):
// the multiplicative gain each band's compressor and gate actually applied,
// in dB, published once per block for the editor's GR bars.
//
// Deliberately the smallest thing that works: six relaxed atomics (three
// bands x {compressor, gate}), written from the audio thread and read from
// the editor's 30 Hz timer. No FIFO, no history ring - a scrolling GR graph
// is M3 GUI scope. Relaxed ordering is correct here because each value is
// independently meaningful and no other state is published alongside it; a
// reader that catches a value one block late simply draws one frame of stale
// metering.
//
// Sign convention: values are <= 0 dB (0 = no reduction). For a band running
// Mid/Side, the two slots' reductions are folded together as the maximum
// reduction of the pair, so one bar per band stays meaningful.
namespace trpt
{
    struct BandGainReduction
    {
        std::atomic<float> compressorDb { 0.0f };
        std::atomic<float> gateDb { 0.0f };

        void store (float newCompressorDb, float newGateDb) noexcept
        {
            compressorDb.store (newCompressorDb, std::memory_order_relaxed);
            gateDb.store (newGateDb, std::memory_order_relaxed);
        }

        float loadCompressorDb() const noexcept { return compressorDb.load (std::memory_order_relaxed); }
        float loadGateDb() const noexcept { return gateDb.load (std::memory_order_relaxed); }

        void clear() noexcept { store (0.0f, 0.0f); }
    };

    struct GainReductionMeter
    {
        BandGainReduction low;
        BandGainReduction mid;
        BandGainReduction high;

        void clear() noexcept
        {
            low.clear();
            mid.clear();
            high.clear();
        }
    };
}
