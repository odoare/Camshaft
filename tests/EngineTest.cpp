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
            engine.prepare (48000.0, block, 32);
            engine.setSource (makeDemoSource());
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

    std::printf (failures == 0 ? "All engine tests passed\n" : "%d engine test(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
