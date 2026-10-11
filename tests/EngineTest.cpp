/*
  ------------------------------------------------------------------------------
    EngineTest.cpp

    The JUCE-free engine on its own (links FxmeCore only): a chord on the
    built-in source with an LFO on cam 1's opening and FM from cam 2, then a
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
        rig.camVals[1][cfOpening] = 0.8f;
        rig.camVals[1][cfCylinders] = 4.0f;
        rig.modVals[0][mfType] = (float) mtLfo;
        rig.depths[0] = 0.3f;
        rig.engine.setRoute (0, srcModFirst, camDest (0, cfOpening), 0);    // LFO 1 -> cam 1 opening
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
            rig.camVals[c][cfOpening] = 0.2f * (float) c + 0.1f;
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
        // An opening swept across the built-in morph (sine at 0, saws further
        // on): the sound must follow during the sweep, not jump at its end.
        // Positions are given as the range's middle (a 2048-sample range on
        // the 131072-sample built-in source opens half a cycle earlier).
        // Random phase off and brightness summed over 4 blocks: one block is
        // under 2 cycles of the note, its brightness depends on where in the
        // cycle it falls.
        constexpr float half = 1024.0f / 131072.0f;
        constexpr int window = 4;

        // Brightness of the rig's last block: energy of the first difference
        // and energy, accumulated.
        auto accumulate = [] (const Rig& r, double& e, double& d)
        {
            for (int i = 1; i < Rig::block; ++i)
            {
                const double x = r.buf[0][(size_t) i], dx = x - r.buf[0][(size_t) i - 1];
                e += x * x;
                d += dx * dx;
            }
        };

        Rig rig;
        rig.camVals[0][cfRandomPhase] = 0.0f;
        rig.camVals[0][cfOpening] = 0.02f - half;
        bool finite = true;
        rig.run (finite);
        rig.engine.noteOn (48, 0.8f);

        const int blocks = 200;                   // about 2 s, swept 0.02 -> 0.5
        std::vector<double> energy, diff;
        for (int b = 0; b < blocks; ++b)
        {
            rig.camVals[0][cfOpening] = 0.02f + 0.48f * (float) b / (float) blocks - half;
            rig.run (finite);
            double e = 0.0, d = 0.0;
            accumulate (rig, e, d);
            energy.push_back (e);
            diff.push_back (d);
        }
        // The sweep's brightness over the window ending at block `last`.
        auto swept = [&] (int last)
        {
            double e = 0.0, d = 0.0;
            for (int b = last - window + 1; b <= last; ++b)
            {
                e += energy[(size_t) b];
                d += diff[(size_t) b];
            }
            return e > 1.0e-12 ? d / e : 0.0;
        };
        // The same note held still at a given position, for reference.
        auto staticBrightness = [&] (float centre)
        {
            Rig still;
            still.camVals[0][cfRandomPhase] = 0.0f;
            still.camVals[0][cfOpening] = centre - half;
            bool ok = true;
            still.run (ok);
            still.engine.noteOn (48, 0.8f);
            double e = 0.0, d = 0.0;
            for (int b = 0; b < 40; ++b)
            {
                still.run (ok);
                if (b >= 40 - window)
                    accumulate (still, e, d);
            }
            return e > 1.0e-12 ? d / e : 0.0;
        };

        // The reference is where the sweep was in the middle of the window,
        // a little earlier still since the range follows its parameter a few
        // milliseconds late. The middle is checked in the saw region (0.25
        // to 0.375), away from the morph's boundaries where the timbre
        // changes abruptly.
        const int at = 122;                       // the sweep at about 0.31
        const double start = swept (12);
        const double middle = swept (at);
        const double refMiddle = staticBrightness (0.02f + 0.48f * ((float) at - 0.5f * window - 0.5f) / (float) blocks);
        const double refStart = staticBrightness (0.02f);
        check (finite, "sweep stays finite");
        check (middle > 3.0 * start, "the timbre changes during the sweep");
        // Not equal: a moving range is a scan, its speed adds to the read
        // speed (here about 0.6 of the 5.6 source samples read per output
        // sample, +11 % in frequency, +23 % in this brightness measure).
        check (std::abs (middle - refMiddle) < 0.35 * refMiddle,
               "mid-sweep, it sounds like a held note at that opening");
        std::printf ("  brightness: start %.4f (held %.4f), middle %.4f (held %.4f)\n", start, refStart, middle, refMiddle);
    }

    {
        // A global LFO locked to a playing host, with host blocks that do not
        // line up with the control blocks (441 samples): its value must match
        // the host position at the end of each control span, every block.
        Rig rig;
        rig.modVals[0][mfType] = (float) mtLfo;
        rig.depths[0] = 0.01f;
        rig.engine.setRoute (0, srcModFirst, camDest (0, cfFine), 0);     // in use, so computed
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
        rig.modVals[0][mfType] = (float) mtLfo;
        rig.modVals[0][mfRate] = 2.0f;
        rig.depths[0] = 0.01f;
        rig.engine.setRoute (0, srcModFirst, camDest (0, cfFine), 0);
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

    {
        // The range opens at its opening and runs for its duration, cut short
        // at the end of the recording; the display shows what is read.
        Rig rig;
        const double n = (double) makeDemoSource()->numSamples;
        rig.camVals[0][cfOpening] = 0.25f;
        rig.camVals[0][cfDuration] = 4096.0f;
        rig.camVals[0][cfOverlap] = 0.0f;
        bool finite = true;
        rig.run (finite);
        rig.engine.noteOn (60, 0.8f);
        rig.run (finite);
        const auto& d = rig.engine.getDisplay();
        const int v = d.newestVoice.load();
        const auto& r = d.ranges[(size_t) std::max (0, v)][0];
        check (v >= 0 && std::abs (r.start.load() - 0.25 * n) < 1.0 && std::abs (r.cycle.load() - 4096.0) < 1.0,
               "range opens at its opening and lasts its duration");
        check (r.phase.load() >= 0.0f && d.ranges[(size_t) std::max (0, v)][1].phase.load() < 0.0f,
               "live range shown for the cams that play only");

        rig.camVals[0][cfOpening] = 0.99f;
        for (int b = 0; b < 20; ++b)
            rig.run (finite);
        check (std::abs (r.start.load() - 0.99 * n) < 1.0 && std::abs (r.cycle.load() - 0.01 * n) < 1.0,
               "near the end, the duration is cut short (the opening stays)");

        double start = 0.0, length = 0.0;
        camRange (1.0f, 4096.0f, n, start, length);
        check (std::abs (start - (n - 16.0)) < 1.0e-9 && std::abs (length - 16.0) < 1.0e-9, "range never shorter than 16 samples");
    }

    {
        // Modulators that feed nothing are not computed (value 0); one that
        // feeds a used one is; an "Off" one never is.
        Rig rig;
        rig.modVals[0][mfType] = (float) mtLfo;
        rig.modVals[1][mfType] = (float) mtLfo;
        rig.modVals[2][mfType] = (float) mtOff;
        rig.modVals[0][mfPhase] = 0.25f;       // starts at its peak
        bool finite = true;
        rig.run (finite);
        rig.engine.noteOn (60, 0.8f);
        rig.run (finite);
        const auto& d = rig.engine.getDisplay();
        check (! d.modUsed[0].load() && isZero (d.source[(size_t) srcModFirst].load()), "an unconnected LFO is not computed");

        rig.depths[0] = 0.1f;
        rig.engine.setRoute (0, srcModFirst, camDest (0, cfFine), 0);              // LFO 1 -> cam 1 fine
        rig.depths[1] = 0.1f;
        rig.engine.setRoute (1, srcModFirst + 1, modDest (0, mfRate), 0);          // LFO 2 -> LFO 1 rate
        rig.depths[2] = 0.1f;
        rig.engine.setRoute (2, srcModFirst + 2, camDest (0, cfPan), 0);           // Off 3 -> cam 1 pan
        rig.run (finite);
        check (d.modUsed[0].load() && std::abs (d.source[(size_t) srcModFirst].load()) > 0.1f, "a connected LFO runs");
        check (d.modUsed[1].load(), "an LFO modulating a used one runs");
        check (! d.modUsed[2].load() && isZero (d.source[(size_t) srcModFirst + 2].load()), "an Off modulator never runs");
    }

    {
        // A curve stuck to the host's timeline plays its start once, then
        // keeps to its sustain loop (no note-off on a timeline), instead of
        // repeating the whole curve.
        Rig rig;
        fxme::CurveShape shape;
        shape.numPoints = 4;
        shape.x = { 0.0f, 0.25f, 0.5f, 1.0f };
        shape.y = { 0.0f, 1.0f, 0.5f, 0.0f };
        shape.sustainEnabled = true;
        shape.sustainStart = 1;
        shape.sustainEnd = 2;
        shape.sustainLoop = true;
        shape.sanitise();
        rig.engine.curve (0).write (shape);
        rig.modVals[0][mfType] = (float) mtCurve;
        rig.modVals[0][mfDawSync] = 1.0f;
        rig.modVals[0][mfDurationMode] = 1.0f;     // in beats
        rig.modVals[0][mfBeats] = 4.0f;
        rig.depths[0] = 0.01f;
        rig.engine.setRoute (0, srcModFirst, camDest (0, cfFine), 0);
        rig.settings.bpm = 120.0;
        rig.settings.playing = true;

        bool finite = true, inLoop = true, startPlayed = false;
        const double beatsPerBlock = 120.0 / 60.0 / 48000.0 * Rig::block;
        for (int b = 0; b < 400; ++b)            // about 17 beats
        {
            rig.settings.ppq = b * beatsPerBlock;
            rig.run (finite);
            const float pos = rig.engine.getDisplay().curvePos[0][0].load();
            if (rig.settings.ppq < 0.8)
                startPlayed = startPlayed || pos < 0.2f;
            if (rig.settings.ppq > 3.0)          // past the sustain end (2 beats)
                inLoop = inLoop && pos >= 0.25f - 1.0e-4f && pos <= 0.5f + 1.0e-4f;
        }
        check (startPlayed && inLoop, "a curve on the host's timeline plays its start, then loops its sustain region");
    }

    {
        // Silent cams are not rendered (their read position stops) unless they
        // modulate another cam or a send can still carry them.
        auto phaseMoves = [] (auto setup)
        {
            Rig rig;
            rig.camVals[1][cfAudible] = 1.0f;
            setup (rig);
            bool finite = true;
            rig.run (finite);
            rig.engine.noteOn (60, 0.8f);
            for (int b = 0; b < 4; ++b)
                rig.run (finite);
            const auto& d = rig.engine.getDisplay();
            const int v = std::max (0, d.newestVoice.load());
            const float before = d.ranges[(size_t) v][1].phase.load();
            rig.run (finite);
            return std::abs (d.ranges[(size_t) v][1].phase.load() - before) > 1.0e-6f;
        };
        check (phaseMoves ([] (Rig&) {}), "a cam that is heard is rendered");
        check (! phaseMoves ([] (Rig& r) { r.camVals[1][cfVolume] = -60.0f; }),
               "a cam with its volume off and no send is not rendered");
        check (phaseMoves ([] (Rig& r) { r.camVals[1][cfVolume] = -60.0f; r.engine.setXmod (1, 0, 1); }),
               "a silent cam that modulates another is rendered");
        check (phaseMoves ([] (Rig& r) { r.camVals[1][cfVolume] = -60.0f; r.camVals[1][cfSend1] = 0.5f; r.camVals[1][cfTap1] = 0.0f; }),
               "a silent cam sending before its volume is rendered");
        check (! phaseMoves ([] (Rig& r) { r.camVals[1][cfVolume] = -60.0f; r.camVals[1][cfSend1] = 0.5f; r.camVals[1][cfTap1] = 2.0f; }),
               "a silent cam sending after its volume is not rendered");

        // Cams release at their own pace: one whose envelope ended stops while
        // the voice goes on.
        Rig rig;
        rig.camVals[1][cfAudible] = 1.0f;
        rig.camVals[1][cfRelease] = 10.0f;       // ms
        rig.camVals[0][cfRelease] = 5000.0f;
        bool finite = true;
        rig.run (finite);
        rig.engine.noteOn (60, 0.8f);
        for (int b = 0; b < 4; ++b)
            rig.run (finite);
        rig.engine.noteOff (60, 0.0f);
        for (int b = 0; b < 10; ++b)
            rig.run (finite);
        const auto& d = rig.engine.getDisplay();
        const int v = std::max (0, d.newestVoice.load());
        const float c0 = d.ranges[(size_t) v][0].phase.load(), c1 = d.ranges[(size_t) v][1].phase.load();
        float peak = rig.run (finite);
        check (d.voicesActive.load() == 1 && std::abs (d.ranges[(size_t) v][1].phase.load() - c1) < 1.0e-6f
                   && std::abs (d.ranges[(size_t) v][0].phase.load() - c0) > 1.0e-6f && peak > 0.0f,
               "a cam whose envelope ended stops while the voice plays on");
    }

    {
        // The dashboard's sources: notes and steals counted, the pedal shown,
        // and the hazard button silencing everything at once.
        Rig rig;
        rig.settings.polyphony = 2;
        bool finite = true;
        rig.run (finite);
        rig.engine.noteOn (60, 0.8f);
        rig.engine.noteOn (64, 0.8f);
        rig.engine.noteOn (67, 0.8f);                  // steals one
        rig.engine.controller (64, 1.0f);              // sustain pedal down
        rig.run (finite);
        const auto& d = rig.engine.getDisplay();
        check (d.noteOns.load() == 3 && d.steals.load() == 1 && d.sustainDown.load() && d.polyphony.load() == 2,
               "dashboard counters: notes, steals, pedal, polyphony");
        rig.engine.panic();
        const float peak = rig.run (finite);
        check (d.voicesActive.load() == 0 && peak < 1.0e-6f && ! d.sustainDown.load(), "panic silences every voice at once");
    }

    std::printf (failures == 0 ? "All engine tests passed\n" : "%d engine test(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
