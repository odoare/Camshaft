/*
  ------------------------------------------------------------------------------
    DemoSource.h

    The audio Camshaft plays before any file is imported (and that the
    factory presets are built on, so they carry no audio of their own): 64
    single cycles of 2048 samples that morph through eight additive
    waveforms (sine, triangle, saw, square, pulse, a sung "ah", a sparse
    metallic spectrum, a resonant saw), eight cycles per waveform.

    Since the period is exactly 2048 samples, a lobe of 2048 plays one
    cycle at the note's pitch wherever the centre is, and sweeping the
    centre walks the morph.

    Generated in a few milliseconds on a non-audio thread. JUCE-free.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <FxmeTools/synth/WavetableBuilder.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace cam
{

inline std::shared_ptr<fxme::WavetableSource> makeDemoSource()
{
    constexpr int cycle = 2048;
    constexpr int numKeys = 8;
    constexpr int cyclesPerKey = 8;
    constexpr int numHarmonics = 200;
    constexpr double twoPi = 6.283185307179586;

    // Harmonic amplitudes of each key waveform.
    auto amplitude = [] (int key, int n) -> double
    {
        const double h = (double) n;
        switch (key)
        {
            case 0: return n == 1 ? 1.0 : 0.0;                                            // sine
            case 1: return (n % 2 == 1) ? ((n / 2) % 2 == 0 ? 1.0 : -1.0) / (h * h) : 0.0; // triangle
            case 2: return 1.0 / h;                                                        // saw
            case 3: return (n % 2 == 1) ? 1.0 / h : 0.0;                                   // square
            case 4: return std::sin (3.14159265358979 * h * 0.25) / h;                     // 25 % pulse
            case 5:                                                                        // "ah"
            {
                auto peak = [h] (double c, double w, double g) { return g * std::exp (-0.5 * (h - c) * (h - c) / (w * w)); };
                return (peak (6.0, 1.6, 1.0) + peak (11.0, 2.0, 0.5) + peak (24.0, 3.0, 0.3) + 0.04 / h);
            }
            case 6:                                                                        // metallic
                return (n == 1 || n == 3 || n == 7 || n == 11 || n == 17 || n == 23 || n == 31 || n == 41) ? 0.6 / std::sqrt (h) : 0.0;
            case 7:                                                                        // resonant saw
                return 1.0 / h + 1.5 * std::exp (-0.5 * (h - 9.0) * (h - 9.0)) / h;
            default: return 0.0;
        }
    };

    std::vector<std::vector<float>> keys ((size_t) numKeys, std::vector<float> ((size_t) cycle, 0.0f));
    std::vector<double> sines ((size_t) cycle);
    for (int n = 1; n <= numHarmonics; ++n)
    {
        for (int i = 0; i < cycle; ++i)
            sines[(size_t) i] = std::sin (twoPi * (double) n * (double) i / (double) cycle);
        for (int k = 0; k < numKeys; ++k)
        {
            const double a = amplitude (k, n);
            if (! (a > 0.0 || a < 0.0))
                continue;
            for (int i = 0; i < cycle; ++i)
                keys[(size_t) k][(size_t) i] += (float) (a * sines[(size_t) i]);
        }
    }
    for (auto& key : keys)
    {
        float peak = 1.0e-9f;
        for (auto v : key)
            peak = std::max (peak, std::abs (v));
        for (auto& v : key)
            v *= 0.8f / peak;
    }

    auto source = std::make_shared<fxme::WavetableSource>();
    const int total = numKeys * cyclesPerKey * cycle;
    source->channels.assign (1, std::vector<float> ((size_t) total, 0.0f));
    source->numSamples = total;
    source->sampleRate = 48000.0;

    auto& out = source->channels[0];
    for (int c = 0; c < numKeys * cyclesPerKey; ++c)
    {
        // Within each key's eight cycles, the last four crossfade into the next key.
        const int key = c / cyclesPerKey;
        const int next = std::min (key + 1, numKeys - 1);
        const int within = c % cyclesPerKey;
        const float t = within < cyclesPerKey / 2 ? 0.0f
                                                  : (float) (within - cyclesPerKey / 2 + 1) / (float) (cyclesPerKey / 2 + 1);
        for (int i = 0; i < cycle; ++i)
            out[(size_t) (c * cycle + i)] = (1.0f - t) * keys[(size_t) key][(size_t) i] + t * keys[(size_t) next][(size_t) i];
    }
    return source;
}

} // namespace cam
