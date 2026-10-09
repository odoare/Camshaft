/*
  ------------------------------------------------------------------------------
    EffectFactory.cpp

    See EffectFactory.h. The IR-backed effects (Cab, ConvolReverb) defer their
    IR loading until a slot is set to them, as in MechanOdd.

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
#include "Oct.h"
#include "OctComponent.h"
#include "Compressor.h"
#include "CompressorComponent.h"
#include "Limiter.h"
#include "LimiterComponent.h"
#include "Transient.h"
#include "TransientComponent.h"
#include "Cab.h"
#include "CabComponent.h"
#include "ConvolReverb.h"
#include "ConvolReverbComponent.h"
#include "Chorus.h"
#include "ChorusComponent.h"
#include "Flanger.h"
#include "FlangerComponent.h"
#include "Phaser.h"
#include "PhaserComponent.h"
#include "Freeze.h"
#include "FreezeComponent.h"

namespace camshaft
{

namespace
{
    template <class FX, class FXComponent, PrepareArgs Args>
    EffectTypeInfo makeEntry (const juce::String& name, const juce::String& moduleName, const juce::String& paramTag,
                              int preferredWidth, int preferredHeight)
    {
        using Adapter = EffectAdapter<FX, Args>;
        return {
            name, moduleName, paramTag,
            [] { return std::unique_ptr<Effect> (new Adapter()); },
            [] (EffectTypeInfo::ParameterList& params, const juce::String& prefix) { FX::addParameters (params, prefix); },
            [] (Effect& e, juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix)
            {
                auto& impl = static_cast<Adapter&> (e).get();
                return std::unique_ptr<juce::Component> (new FXComponent (impl, apvts, prefix));
            },
            preferredWidth, preferredHeight
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

    class ConvolReverbAdapter : public DeferredIRAdapter<ConvolReverb>
    {
    public:
        // Same order as FxmeFX's own ConvolReverb plugin (the IR parameter is
        // an index, and the module presets shared with it store it). The
        // first three hold mid / side and are decoded to left / right.
        struct IR { const char* name; const char* resource; bool midSide; };
        static constexpr IR kIRs[] = {
            { "Council Chamber",         "Council_Chamber_wav",         true  },
            { "Forest short",            "Forest_short_wav",            true  },
            { "Forest long",             "Forest_long_wav",             true  },
            { "Rectangular room small",  "Rectangular_room_small_wav",  false },
            { "Rectangular room medium", "Rectangular_room_medium_wav", false },
            { "Rectangular room large",  "Rectangular_room_large_wav",  false },
        };
        static constexpr int numIRs = (int) std::size (kIRs);

        static void addParameters (EffectTypeInfo::ParameterList& params, const juce::String& prefix)
        {
            ConvolReverb::addParameters (params, prefix, numIRs);
        }

    protected:
        void setImpulseList() override
        {
            juce::StringArray names, resources, midSide;
            for (const auto& ir : kIRs)
            {
                names.add (ir.name);
                resources.add (ir.resource);
                if (ir.midSide)
                    midSide.add (ir.resource);
            }
            impl.setImpulseList (names, resources, midSide);
        }
    };

    template <class Adapter, class FXComponent>
    EffectTypeInfo makeIREntry (const juce::String& name, const juce::String& moduleName, const juce::String& paramTag,
                                int preferredWidth, int preferredHeight)
    {
        return {
            name, moduleName, paramTag,
            [] { return std::unique_ptr<Effect> (new Adapter()); },
            [] (EffectTypeInfo::ParameterList& params, const juce::String& prefix) { Adapter::addParameters (params, prefix); },
            [] (Effect& e, juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix)
            {
                auto& impl = static_cast<Adapter&> (e).get();
                return std::unique_ptr<juce::Component> (new FXComponent (impl, apvts, prefix));
            },
            preferredWidth, preferredHeight
        };
    }
}

const std::vector<EffectTypeInfo>& EffectFactory::types()
{
    // The sizes of the FxmeFX plugins' editors (their PluginEditor.h), plus
    // the effect's own title row (fxmefx::kTopBarHeight).
    constexpr int titleRow = 44;
    using P = PrepareArgs;
    // Append only: a slot's type parameter is an index into this list.
    static const std::vector<EffectTypeInfo> registry = {
        makeEntry<StereoDelay, StereoDelayComponent, P::rateBlock>         ("Delay",     "StereoDelay", "Del",     600, 340 + titleRow),
        makeEntry<Tube,        TubeComponent,        P::rate>              ("Tube",      "Tube",        "Tube",    400, 300 + titleRow),
        makeEntry<Equalizer,   EqualizerComponent,   P::rateChannels>      ("EQ",        "Equalizer",   "EQ",      600, 700 + titleRow),
        makeEntry<Oct,         OctComponent,         P::rateChannels>      ("Oct",       "Oct",         "Oct",     520, 320 + titleRow),
        makeEntry<Compressor,  CompressorComponent,  P::rateChannels>      ("Comp",      "Compressor",  "Comp",    600, 300 + titleRow),
        makeEntry<Limiter,     LimiterComponent,     P::rateChannels>      ("Limit",     "Limiter",     "Lim",     600, 300 + titleRow),
        makeEntry<Transient,   TransientComponent,   P::rateChannels>      ("Transient", "Transient",   "Trans",   480, 300 + titleRow),
        makeIREntry<CabAdapter,          CabComponent>                     ("Cab",       "Cab",          "Cab",    600, 400 + titleRow),
        makeIREntry<ConvolReverbAdapter, ConvolReverbComponent>            ("Reverb",    "ConvolReverb", "Rev",    600, 400 + titleRow),
        makeEntry<Chorus,      ChorusComponent,      P::rateBlockChannels> ("Chorus",    "Chorus",      "Chorus",  560, 340 + titleRow),
        makeEntry<Flanger,     FlangerComponent,     P::rateBlockChannels> ("Flanger",   "Flanger",     "Flanger", 560, 340 + titleRow),
        makeEntry<Phaser,      PhaserComponent,      P::rateBlockChannels> ("Phaser",    "Phaser",      "Phaser",  620, 340 + titleRow),
        makeEntry<Freeze,      FreezeComponent,      P::rateBlockChannels> ("Freeze",    "Freeze",      "Freeze",  520, 360 + titleRow),
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
