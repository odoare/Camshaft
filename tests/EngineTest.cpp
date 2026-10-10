/*
  ------------------------------------------------------------------------------
    EngineTest.cpp

    The JUCE-free engine on its own (links FxmeCore only): a chord on the
    built-in source with an LFO on cam 1's centre and FM from cam 2, then a
    full-load run (24 voices, 4 cams, 4 cylinders, both filters). Checks the
    output is finite and not silent, that voices are freed after their
    release, and prints the CPU time per second of audio.

        cmake --build build --target CamshaftEngineTest
        ./build/CamshaftEngineTest

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include "CamEngine.h"
#include "DemoSource.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

using namespace cam;

static int failures = 0;
static void check (bool ok, const char* what)
{
    std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (! ok)
        ++failures;
}

namespace
{
    struct Rig
    {
        static constexpr int block = 512;
        CamEngine engine;
        float camVals[numCams][numCamFields];
        float modVals[numModulators][numModFields];
        float depths[maxRoutes] {};
        CamEngine::BlockSettings settings;
        std::vector<float> buf[6];
        CamEngine::Buses buses;

        Rig()
        {
            engine.setNumGlobalDests (4);
            engine.prepare (48000.0, block);
            engine.setSource (EngineSource::make (makeDemoSource()));
            for (int c = 0; c < numCams; ++c)
                for (int f = 0; f < numCamFields; ++f)
                    camVals[c][f] = camSpecs[f].def;
            for (int c = 1; c < numCams; ++c)
                camVals[c][cfAudible] = 0.0f;
            for (int m = 0; m < numModulators; ++m)
                for (int f = 0; f < numModFields; ++f)
                    modVals[m][f] = modSpecs[f].def;
            for (int c = 0; c < numCams; ++c) settings.camValues[c] = camVals[c];
            for (int m = 0; m < numModulators; ++m) settings.modValues[m] = modVals[m];
            settings.routeDepths = depths;
            for (auto& b : buf) b.assign (block, 0.0f);
            buses = { { buf[0].data(), buf[1].data() }, { { buf[2].data(), buf[3].data() }, { buf[4].data(), buf[5].data() } } };
        }

        /** One block; returns the peak of the dry left channel, false in
            `finite` on a NaN or an infinity. */
        float run (bool& finite)
        {
            for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
            engine.beginBlock (settings);
            engine.render (buses, 0, block);
            float peak = 0.0f;
            for (int i = 0; i < block; ++i)
            {
                if (! std::isfinite (buf[0][(size_t) i]) || ! std::isfinite (buf[1][(size_t) i])) finite = false;
                peak = std::max (peak, std::abs (buf[0][(size_t) i]));
            }
            return peak;
        }
    };
}

int main()
{
    std::printf ("Camshaft engine\n");
    {
        Rig rig;
        rig.camVals[1][cfAudible] = 1.0f;
        rig.camVals[1][cfCentre] = 0.8f;
        rig.camVals[1][cfCylinders] = 4.0f;
        rig.depths[0] = 0.3f;
        rig.engine.setRoute (0, srcModFirst, camDest (0, cfCentre), 0);   // LFO 1 -> cam 1 centre
        rig.engine.setXmod (1, 0, 1);                                     // FM cam 2 -> cam 1

        bool finite = true;
        rig.run (finite);
        std::this_thread::sleep_for (std::chrono::milliseconds (100));    // the worker builds the tables

        rig.engine.noteOn (60, 0.8f);
        rig.engine.noteOn (64, 0.8f);
        rig.engine.noteOn (67, 0.8f);
        float peak = 0.0f;
        for (int b = 0; b < 200; ++b)
            peak = std::max (peak, rig.run (finite));
        check (finite, "output is finite");
        check (peak > 0.01f, "a chord sounds");
        check (rig.engine.getDisplay().voicesActive.load() == 3, "three voices playing");

        rig.engine.noteOff (60, 0.0f);
        rig.engine.noteOff (64, 0.0f);
        rig.engine.noteOff (67, 0.0f);
        for (int b = 0; b < 100; ++b)
            rig.run (finite);
        check (rig.engine.getDisplay().voicesActive.load() == 0, "voices freed after their release");
    }

    {
        Rig rig;
        for (int c = 0; c < numCams; ++c)
        {
            rig.camVals[c][cfAudible] = 1.0f;
            rig.camVals[c][cfCylinders] = (float) maxCylinders;
            rig.camVals[c][cfF1On] = 1.0f;
            rig.camVals[c][cfF2On] = 1.0f;
            rig.camVals[c][cfCentre] = 0.2f * (float) c + 0.1f;
        }
        rig.settings.polyphony = maxVoices;
        bool finite = true;
        rig.run (finite);
        std::this_thread::sleep_for (std::chrono::milliseconds (100));
        for (int n = 0; n < maxVoices; ++n)
            rig.engine.noteOn (36 + 2 * n, 0.8f);

        const int blocks = 400;
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b)
            rig.run (finite);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        const double audio = blocks * Rig::block / 48000.0;
        check (finite, "full load stays finite");
        check (rig.engine.getDisplay().voicesActive.load() == maxVoices, "every voice playing");
        std::printf ("  full load (%d voices x %d cams x %d cylinders, 2 filters): %.1f %% of one core\n",
                     maxVoices, numCams, maxCylinders, 100.0 * secs / audio);
    }

    {
        // A centre swept across the built-in morph (sine at 0, saws further
        // on): the sound must follow during the sweep, not jump at its end.
        Rig rig;
        rig.camVals[0][cfCentre] = 0.02f;
        bool finite = true;
        rig.run (finite);
        std::this_thread::sleep_for (std::chrono::milliseconds (100));
        rig.engine.noteOn (48, 0.8f);

        // Brightness of a block: energy of the first difference over energy.
        auto brightness = [&rig]
        {
            double e = 0.0, d = 0.0;
            for (int i = 1; i < Rig::block; ++i)
            {
                const double x = rig.buf[0][(size_t) i], dx = x - rig.buf[0][(size_t) i - 1];
                e += x * x;
                d += dx * dx;
            }
            return e > 1.0e-12 ? d / e : 0.0;
        };

        const int blocks = 200;                   // about 2 s, swept 0.02 -> 0.5
        std::vector<double> bright;
        for (int b = 0; b < blocks; ++b)
        {
            rig.camVals[0][cfCentre] = 0.02f + 0.48f * (float) b / (float) blocks;
            rig.run (finite);
            bright.push_back (brightness());
            std::this_thread::sleep_for (std::chrono::microseconds (500));   // let the worker breathe, as a real host would
        }
        // The same note held still at a given centre, for reference.
        auto staticBrightness = [&] (float centre)
        {
            Rig still;
            still.camVals[0][cfCentre] = centre;
            bool ok = true;
            still.run (ok);
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
            still.engine.noteOn (48, 0.8f);
            for (int b = 0; b < 40; ++b)
                still.run (ok);
            double e = 0.0, d = 0.0;
            for (int i = 1; i < Rig::block; ++i)
            {
                const double x = still.buf[0][(size_t) i], dx = x - still.buf[0][(size_t) i - 1];
                e += x * x;
                d += dx * dx;
            }
            return d / e;
        };

        // Where the sweep is a few blocks before each check point, since the
        // table follows the position a few milliseconds late.
        const double start = bright[10];
        const double middle = bright[(size_t) blocks / 2];
        const double refMiddle = staticBrightness (0.02f + 0.48f * 0.49f);
        const double refStart = staticBrightness (0.02f);
        check (finite, "sweep stays finite");
        check (middle > 3.0 * start, "the timbre changes during the sweep");
        check (std::abs (middle - refMiddle) < 0.35 * refMiddle,
               "mid-sweep, it sounds like a held note at that centre");
        std::printf ("  brightness: start %.4f (held %.4f), middle %.4f (held %.4f)\n", start, refStart, middle, refMiddle);
    }

    {
        // A global LFO locked to a playing host, with host blocks that do not
        // line up with the control blocks (441 samples): its value must match
        // the host position at the end of each control span, every block.
        Rig rig;
        rig.modVals[0][mfMode] = 1.0f;        // global
        rig.modVals[0][mfSync] = 1.0f;
        rig.modVals[0][mfDivision] = 5.0f;    // 1/4: one cycle per beat
        rig.settings.bpm = 120.0;
        rig.settings.playing = true;

        const int hostBlock = 441;
        const double beatsPerSample = 120.0 / 60.0 / 48000.0;
        double worst = 0.0;
        long long t = 0;
        bool finite = true;
        for (int b = 0; b < 300; ++b)
        {
            rig.settings.ppq = (double) t * beatsPerSample;
            for (auto& buf : rig.buf) std::fill (buf.begin(), buf.end(), 0.0f);
            rig.engine.beginBlock (rig.settings);
            rig.engine.render (rig.buses, 0, hostBlock);
            t += hostBlock;

            // The last control update in this block started at a multiple of
            // controlBlock; its value is the phase at the end of its span.
            const long long lastStart = ((t - 1) / controlBlock) * controlBlock;
            const double ppqEnd = (double) (lastStart + controlBlock) * beatsPerSample;
            const double expected = std::sin (6.283185307179586 * ppqEnd);
            const double got = rig.engine.getDisplay().source[(size_t) srcModFirst].load();
            worst = std::max (worst, std::abs (got - expected));
            finite = finite && std::isfinite (got);
        }
        check (finite, "host-locked LFO finite");
        check (worst < 1.0e-3, "host-locked LFO follows the host position across odd block sizes");
        std::printf ("  host-locked LFO, worst phase error as output: %.2e\n", worst);
    }

    {
        // The value published for the modulator meters: a 2 Hz per-voice sine
        // LFO under a held note must move continuously, block after block,
        // by no more than its slope allows.
        Rig rig;
        rig.modVals[0][mfRate] = 2.0f;
        bool finite = true;
        rig.run (finite);
        rig.engine.noteOn (60, 0.8f);
        double maxStep = 0.0, prev = 0.0;
        for (int b = 0; b < 200; ++b)
        {
            rig.run (finite);
            const double v = rig.engine.getDisplay().source[(size_t) srcModFirst].load();
            if (b > 0)
                maxStep = std::max (maxStep, std::abs (v - prev));
            prev = v;
        }
        // Slope of sin (2 pi 2 t) is at most 4 pi per second; one block is
        // 512 / 48000 s; allow one control block of slack.
        const double allowed = 4.0 * 3.14159265 * (512.0 + 32.0) / 48000.0;
        check (maxStep <= allowed, "per-voice LFO value moves continuously");
        std::printf ("  LFO meter value: largest step per block %.4f (allowed %.4f)\n", maxStep, allowed);
    }

    std::printf (failures == 0 ? "All engine tests passed\n" : "%d engine test(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
