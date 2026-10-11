/*
  ------------------------------------------------------------------------------
    EffectFactory.cpp

    See EffectFactory.h. The Cab defers its IR loading until a slot is set
    to it, as in MechanOdd. Every effect's component is made in its compact
    layout (no title, flat knobs, one row), for Camshaft's panes.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "EffectFactory.h"

#include "StereoDelay.h"
#include "StereoDelayComponent.h"
#include "Tube.h"
#include "TubeComponent.h"
#include "Equalizer.h"
#include "EqualizerComponent.h"
#include "Compressor.h"
#include "CompressorComponent.h"
#include "Cab.h"
#include "CabComponent.h"
#include "Chorus.h"
#include "ChorusComponent.h"
#include "Flanger.h"
#include "FlangerComponent.h"
#include "Phaser.h"
#include "PhaserComponent.h"
#include "Reverb.h"
#include "ReverbComponent.h"

namespace camshaft
{

namespace
{
    /** An effect's component in its compact layout (the Tube's constructor
        has an extra layout flag before it). */
    template <class FXComponent, class FX>
    std::unique_ptr<juce::Component> makeCompact (FX& fx, juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix)
    {
        if constexpr (std::is_same_v<FXComponent, TubeComponent>)
            return std::make_unique<FXComponent> (fx, apvts, prefix, false, true, true);
        else
            return std::make_unique<FXComponent> (fx, apvts, prefix, false, true);
    }

    template <class FX, class FXComponent, PrepareArgs Args>
    EffectTypeInfo makeEntry (const juce::String& name, const juce::String& moduleName, const juce::String& paramTag)
    {
        using Adapter = EffectAdapter<FX, Args>;
        return {
            name, moduleName, paramTag,
            [] { return std::unique_ptr<Effect> (new Adapter()); },
            [] (EffectTypeInfo::ParameterList& params, const juce::String& prefix) { FX::addParameters (params, prefix); },
            [] (Effect& e, juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix)
            {
                return makeCompact<FXComponent> (static_cast<Adapter&> (e).get(), apvts, prefix);
            }
        };
    }

    //==========================================================================
    // IR-backed effects: their IR list (and so their first IR load) is handed
    // over only once a slot is set to them; until then they output silence.
    // The IR list goes in before prepare(), or the effect's polling thread
    // would find an empty list and never load the IR its parameter names.
    template <class Impl>
    class DeferredIRAdapter : public Effect
    {
    public:
        Impl& get() { return impl; }

        void prepare (double newSampleRate, int, int newBlockSize) override
        {
            const juce::ScopedLock sl (loadLock);
            sampleRate = newSampleRate;
            blockSize = newBlockSize;
            prepared = true;
            if (loaded)
            {
                ready = false;
                impl.prepare (sampleRate, blockSize);
                ready = true;
            }
        }

        void ensureLoaded() override
        {
            const juce::ScopedLock sl (loadLock);
            if (loaded)
                return;
            setImpulseList();
            loaded = true;
            if (prepared)
            {
                impl.prepare (sampleRate, blockSize);
                ready = true;
            }
        }

        void process (juce::AudioBuffer<float>& buffer) override
        {
            if (ready.load (std::memory_order_acquire))
                impl.process (buffer);
            else
                buffer.clear();
        }

        void assignParameters (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override
        {
            impl.assignParameters (apvts, prefix);
        }

        void checkParameters() override
        {
            if (ready.load (std::memory_order_acquire))
                impl.checkParameters();
        }

    protected:
        virtual void setImpulseList() = 0;
        Impl impl;

    private:
        juce::CriticalSection loadLock;      // ensureLoaded vs prepare; never the audio thread
        bool loaded = false, prepared = false;
        double sampleRate = 44100.0;
        int blockSize = 512;
        std::atomic<bool> ready { false };
    };

    class CabAdapter : public DeferredIRAdapter<Cab>
    {
    public:
        // Display names -> BinaryData symbols of lib/FxmeFX/Source/Cab/IR.
        static constexpr std::pair<const char*, const char*> kIRs[] = {
            { "2off-pres5",                           "_2offpres5_wav"                      },
            { "Allure 67 Brit Greenback",             "Allure_67_Brit_Greenback_wav"         },
            { "Allure 90s Cali V30",                  "Allure_90s_Cali_V30_wav"              },
            { "Annihilator Feast",                    "AnnihilatorFeast_wav"                 },
            { "Brohymn Mesa 4x12 SM57 V30 1",         "Brohymn_Mesa_4x12_SM57_V30_1_wav"     },
            { "Brohymn-Mesa-4x12-SM57-V30-5",         "BrohymnMesa4x12SM57V305_wav"          },
            { "Cenzo Celestion V30 Mix",              "Cenzo_Celestion_V30_Mix_wav"          },
            { "Excalibur 1 - Bright1, a",             "Excalibur_1__Bright1_a_wav"           },
            { "Excalibur 2 - Dark3, b",               "Excalibur_2__Dark3_b_wav"             },
            { "GuitarHack JJ CENTRE45 0",             "GuitarHack_JJ_CENTRE45_0_wav"         },
            { "GuitarHack JJ FRED45 0",               "GuitarHack_JJ_FRED45_0_wav"           },
            { "KornKorn",                             "KornKorn_wav"                         },
            { "Marshall1960A-G12Ms-M160-CapEdge-3in", "Marshall1960AG12MsM160CapEdge3in_wav" },
            { "Marshall1960A-G12Ms-SM57-Cap-0in",     "Marshall1960AG12MsSM57Cap0in_wav"     },
            { "Neumann U87 0_dc",                     "NewmannU87_0_dc_wav"                  },
            { "OH 412 MES-ST V60 421-04",             "OH_412_MESST_V60_42104_wav"           },
            { "OH 412 MES-ST V60 57-00",              "OH_412_MESST_V60_5700_wav"            },
            { "Shure SM57 0_dc",                      "ShureSM57_0_dc_wav"                   },
            { "s-preshigh",                           "spreshigh_wav"                        },
        };
        static constexpr int numIRs = (int) std::size (kIRs);

        static void addParameters (EffectTypeInfo::ParameterList& params, const juce::String& prefix)
        {
            Cab::addParameters (params, prefix, numIRs);
        }

    protected:
        void setImpulseList() override
        {
            juce::StringArray names, resources;
            for (const auto& [name, resource] : kIRs)
            {
                names.add (name);
                resources.add (resource);
            }
            impl.setImpulseList (names, resources);
        }
    };

    template <class Adapter, class FXComponent>
    EffectTypeInfo makeIREntry (const juce::String& name, const juce::String& moduleName, const juce::String& paramTag)
    {
        return {
            name, moduleName, paramTag,
            [] { return std::unique_ptr<Effect> (new Adapter()); },
            [] (EffectTypeInfo::ParameterList& params, const juce::String& prefix) { Adapter::addParameters (params, prefix); },
            [] (Effect& e, juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix)
            {
                return makeCompact<FXComponent> (static_cast<Adapter&> (e).get(), apvts, prefix);
            }
        };
    }
}

const std::vector<EffectTypeInfo>& EffectFactory::types()
{
    using P = PrepareArgs;
    // A slot's type parameter is an index into this list.
    static const std::vector<EffectTypeInfo> registry = {
        makeEntry<StereoDelay, StereoDelayComponent, P::rateBlock>         ("Delay",   "StereoDelay", "Del"),
        makeEntry<Tube,        TubeComponent,        P::rate>              ("Tube",    "Tube",        "Tube"),
        makeEntry<Equalizer,   EqualizerComponent,   P::rateChannels>      ("EQ",      "Equalizer",   "EQ"),
        makeEntry<Compressor,  CompressorComponent,  P::rateChannels>      ("Comp",    "Compressor",  "Comp"),
        makeIREntry<CabAdapter, CabComponent>                              ("Cab",     "Cab",         "Cab"),
        makeEntry<Chorus,      ChorusComponent,      P::rateBlockChannels> ("Chorus",  "Chorus",      "Chorus"),
        makeEntry<Flanger,     FlangerComponent,     P::rateBlockChannels> ("Flanger", "Flanger",     "Flanger"),
        makeEntry<Phaser,      PhaserComponent,      P::rateBlockChannels> ("Phaser",  "Phaser",      "Phaser"),
        makeEntry<ReverbEffect, ReverbComponent,     P::rateBlockChannels> ("Reverb",  "Reverb",      "Verb"),
    };
    return registry;
}

juce::StringArray EffectFactory::typeChoices()
{
    juce::StringArray choices;
    choices.add ("Off");
    for (const auto& info : types())
        choices.add (info.name);
    return choices;
}

} // namespace camshaft
