/*
  ------------------------------------------------------------------------------
    Randomizer.cpp

    See Randomizer.h.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "Randomizer.h"
#include "ParamIDs.h"
#include "Effects/EffectChain.h"
#include "Effects/EffectFactory.h"

namespace camshaft
{

using namespace cam;

namespace
{
    struct Ctx
    {
        juce::AudioProcessorValueTreeState& apvts;
        const StructureState& structure;
        juce::Random& rng;

        juce::RangedAudioParameter* get (const juce::String& id) const
        {
            if (id == ids::masterOut || id == ids::limiter || structure.isLocked (id))
                return nullptr;
            return apvts.getParameter (id);
        }

        void setValue (const juce::String& id, float value) const
        {
            if (auto* p = get (id))
                p->setValueNotifyingHost (p->convertTo0to1 (value));
        }

        void setNorm (const juce::String& id, float norm) const
        {
            if (auto* p = get (id))
                p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, norm));
        }

        /** Uniform in the spec's normalised space between two values. */
        void range (const juce::String& id, const ParamSpec& spec, float lo, float hi) const
        {
            const float a = spec.toNorm (lo), b = spec.toNorm (hi);
            setValue (id, spec.fromNorm (a + (b - a) * rng.nextFloat()));
        }

        void pick (const juce::String& id, std::initializer_list<float> values) const
        {
            const auto i = (size_t) rng.nextInt ((int) values.size());
            setValue (id, *(values.begin() + i));
        }

        void choice (const juce::String& id, const ParamSpec& spec) const
        {
            setValue (id, (float) rng.nextInt (juce::jmax (1, spec.numChoices)));
        }

        void chance (const juce::String& id, float probability) const
        {
            setValue (id, rng.nextFloat() < probability ? 1.0f : 0.0f);
        }
    };

    void randomizeCams (const Ctx& x)
    {
        for (int c = 0; c < numCams; ++c)
        {
            auto id = [c] (int f) { return ids::cam (c, f); };
            auto r = [&] (int f, float lo, float hi) { x.range (id (f), camSpecs[f], lo, hi); };

            r (cfCentre, 0.0f, 1.0f);
            r (cfWidth, 128.0f, 16384.0f);
            r (cfOverlap, 0.0f, 30.0f);
            x.choice (id (cfOverlapShape), camSpecs[cfOverlapShape]);
            x.choice (id (cfSnap), camSpecs[cfSnap]);
            x.pick (id (cfCoarse), { -12.0f, -5.0f, 0.0f, 0.0f, 0.0f, 7.0f, 12.0f });
            r (cfFine, -15.0f, 15.0f);
            x.pick (id (cfCylinders), { 1.0f, 1.0f, 2.0f, 3.0f, 4.0f });
            r (cfDetune, 0.0f, 30.0f);
            r (cfSpread, 0.0f, 1.0f);
            x.chance (id (cfRandomPhase), 0.7f);
            r (cfVolume, -12.0f, -3.0f);
            r (cfPan, -0.5f, 0.5f);
            r (cfSend1, 0.0f, 0.5f);
            r (cfSend2, 0.0f, 0.5f);
            r (cfAttack, 0.0f, 300.0f);
            r (cfDecay, 50.0f, 2000.0f);
            r (cfSustain, 0.2f, 1.0f);
            r (cfRelease, 50.0f, 2000.0f);
            r (cfAttackCurve, -0.8f, 0.3f);
            r (cfDecayCurve, -0.8f, 0.3f);
            r (cfReleaseCurve, -0.8f, 0.3f);
            r (cfVelocity, 0.2f, 1.0f);
            for (int s = 0; s < numCams; ++s)
                r (cfLift1 + s, 0.0f, 0.6f);
        }
    }

    void randomizeFilters (const Ctx& x)
    {
        for (int c = 0; c < numCams; ++c)
        {
            auto id = [c] (int f) { return ids::cam (c, f); };
            x.choice (id (cfFilterRouting), camSpecs[cfFilterRouting]);
            x.range (id (cfFilterBalance), camSpecs[cfFilterBalance], -1.0f, 1.0f);

            for (int fi = 0; fi < 2; ++fi)
            {
                const int o = fi * filterFieldStride;
                auto r = [&] (int f, float lo, float hi) { x.range (id (f + o), camSpecs[f + o], lo, hi); };
                x.chance (id (cfF1On + o), fi == 0 ? 0.7f : 0.4f);
                x.choice (id (cfF1Type + o), camSpecs[cfF1Type + o]);
                r (cfF1Cutoff, 200.0f, 14000.0f);
                r (cfF1Res, 0.0f, 0.7f);
                r (cfF1Gain, -9.0f, 9.0f);
                r (cfF1KeyTrack, 0.0f, 1.0f);
                r (cfF1EnvAmount, -0.5f, 0.8f);
                x.choice (id (cfF1VowelA + o), camSpecs[cfF1VowelA + o]);
                x.choice (id (cfF1VowelB + o), camSpecs[cfF1VowelB + o]);
                r (cfF1VowelPos, 0.0f, 1.0f);
            }
        }
    }

    void randomizeModulators (const Ctx& x)
    {
        for (int m = 0; m < numModulators; ++m)
        {
            auto id = [m] (int f) { return ids::mod (m, f); };
            auto r = [&] (int f, float lo, float hi) { x.range (id (f), modSpecs[f], lo, hi); };

            x.pick (id (mfType), { 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 2.0f });
            x.choice (id (mfShape), modSpecs[mfShape]);
            r (mfRate, 0.05f, 8.0f);
            x.chance (id (mfSync), 0.3f);
            x.choice (id (mfDivision), modSpecs[mfDivision]);
            r (mfPhase, 0.0f, 1.0f);
            r (mfDelay, 0.0f, 500.0f);
            r (mfFade, 0.0f, 1000.0f);
            x.chance (id (mfBipolar), 0.6f);
            x.chance (id (mfMode), 0.25f);
            r (mfAttack, 0.0f, 500.0f);
            r (mfDecay, 50.0f, 2000.0f);
            r (mfSustain, 0.0f, 1.0f);
            r (mfRelease, 50.0f, 3000.0f);
            r (mfAttackCurve, -0.8f, 0.5f);
            r (mfDecayCurve, -0.8f, 0.5f);
            r (mfReleaseCurve, -0.8f, 0.5f);
            r (mfVelocity, 0.0f, 1.0f);
            r (mfLevel, 0.5f, 1.0f);
            r (mfDuration, 0.5f, 8.0f);
            x.choice (id (mfDurationMode), modSpecs[mfDurationMode]);
            r (mfBeats, 1.0f, 16.0f);
            x.chance (id (mfDawSync), 0.2f);
            r (mfSpeed, 0.5f, 2.0f);
        }

        // Depths of the routes that exist (the routes themselves are the user's).
        for (int i = 0; i < maxRoutes; ++i)
            if (x.structure.getRoute (i).isUsed())
                x.setValue (ids::routeDepth (i), -0.6f + 1.2f * x.rng.nextFloat());
    }

    void randomizeEffects (const Ctx& x)
    {
        const auto& types = EffectFactory::types();
        juce::StringArray chains;
        for (int a = 0; a < numAux; ++a)
        {
            chains.add (ids::auxPrefix (a));
            x.range (ids::auxReturn (a), ParamSpec { "", "", Mapping::linear, -60, 6, 0, 1, false, "" }, -12.0f, 0.0f);
        }
        chains.add (ids::masterPrefix);

        for (const auto& chain : chains)
            for (int s = 0; s < EffectChain::numSlots; ++s)
            {
                const auto slot = EffectChain::slotPrefix (chain, s);
                const auto* typeParam = x.apvts.getRawParameterValue (EffectSlot::typeParamId (slot));
                const int type = typeParam != nullptr ? (int) typeParam->load() : 0;
                if (type <= 0 || type > (int) types.size())
                    continue;

                const auto prefix = EffectFactory::presetPrefix (EffectSlot::perTypePrefix (slot, types[(size_t) type - 1].name),
                                                                 types[(size_t) type - 1]);
                for (auto* p : x.apvts.processor.getParameters())
                {
                    auto* fp = dynamic_cast<juce::AudioParameterFloat*> (p);
                    if (fp == nullptr || ! fp->paramID.startsWith (prefix))
                        continue;
                    // Levels stay where they are: a random output gain is how
                    // a patch gets loud.
                    const auto key = fp->paramID.substring (prefix.length()).toLowerCase();
                    if (key.contains ("gain") || key.contains ("out") || key.contains ("vol") || key.contains ("level"))
                        continue;
                    x.setNorm (fp->paramID, 0.1f + 0.8f * x.rng.nextFloat());
                }
            }
    }
}

void randomize (RandomSection section, juce::AudioProcessorValueTreeState& apvts, const StructureState& structure)
{
    juce::Random rng;
    const Ctx x { apvts, structure, rng };

    if (section == RandomSection::cams || section == RandomSection::all)       randomizeCams (x);
    if (section == RandomSection::filters || section == RandomSection::all)    randomizeFilters (x);
    if (section == RandomSection::modulators || section == RandomSection::all) randomizeModulators (x);
    if (section == RandomSection::effects || section == RandomSection::all)    randomizeEffects (x);
}

} // namespace camshaft
