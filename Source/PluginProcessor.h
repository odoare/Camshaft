/*
  ------------------------------------------------------------------------------
    PluginProcessor.h

    Camshaft's plugin shell: the host parameters (APVTS), the presets, the
    structural state, the embedded audio, MIDI learn, the effect chains and
    the master stage around the JUCE-free engine (Source/Engine).

    Per block: parameters -> engine settings, MIDI split into the engine's
    render calls, modulation of the global destinations, the two aux chains
    (Turbo 1 and 2) with their returns, the master chain (Exhaust), output
    level, the rev limiter (look-ahead safety limiter, always last), then the
    taps for the editor's scopes and meters.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "Engine/CamEngine.h"
#include "Effects/EffectChain.h"
#include "Shell/Destinations.h"
#include "Shell/MidiLearn.h"
#include "Shell/Randomizer.h"
#include "Shell/SourceManager.h"
#include "Shell/StructureState.h"
#include <map>

class CamshaftAudioProcessor : public juce::AudioProcessor
{
public:
    CamshaftAudioProcessor();
    ~CamshaftAudioProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept       { return apvts; }
    fxme::PresetManager& getPresetManager() noexcept              { return presetManager; }
    cam::CamEngine& getEngine() noexcept                          { return engine; }
    camshaft::StructureState& getStructure() noexcept             { return structure; }
    camshaft::SourceManager& getSources() noexcept                { return sources; }
    camshaft::MidiLearn& getMidiLearn() noexcept                  { return midiLearn; }
    const camshaft::Destinations& getDestinations() const noexcept { return destinations; }

    camshaft::EffectChain& getAuxChain (int a) noexcept { return auxChains[(size_t) a]; }
    camshaft::EffectChain& getMasterChain() noexcept    { return masterChain; }

    /** The module presets of one effect in one slot, by the slot's per-type
        prefix (EffectSlot::perTypePrefix, e.g. "aux1_fx0_Comp"). */
    fxme::ModulePresetTarget* getEffectPresets (const juce::String& perTypePrefix) const
    {
        const auto it = effectPresetTargets.find (perTypePrefix);
        return it != effectPresetTargets.end() ? it->second.get() : nullptr;
    }

    void randomize (camshaft::RandomSection section) { camshaft::randomize (section, apvts, structure); }

    /** Keep the current audio when a preset is loaded (per instance, not saved). */
    void setKeepAudioOnPresetLoad (bool keep) noexcept { keepAudioOnPresetLoad = keep; }
    bool getKeepAudioOnPresetLoad() const noexcept      { return keepAudioOnPresetLoad; }

    // Master view data (any thread).
    const fxme::StereoTap& getOutputTap() const noexcept { return outputTap; }
    float getOutputPeakDb() const noexcept               { return outputPeakDb.load (std::memory_order_relaxed); }
    float getLimiterReductionDb() const noexcept         { return limiter.getGainReductionDb(); }

    bool takeFirstSplash() noexcept { return ! std::exchange (splashShown, true); }

private:
    void readParameters();
    void handleMidi (const juce::MidiMessage& m);
    void applyGlobalModulation();
    void processChunk (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, int start, int length);
    void createEffectPresets();

    juce::AudioProcessorValueTreeState apvts;
    cam::CamEngine engine;
    camshaft::Destinations destinations { apvts };
    camshaft::StructureState structure { apvts, engine, destinations };
    camshaft::SourceManager sources { apvts, engine };
    camshaft::MidiLearn midiLearn { apvts, structure };
    fxme::PresetManager presetManager;

    // Effect module presets (shared with FxmeFX's own plugins): one library
    // per effect type, one target per effect per slot. After the APVTS.
    std::vector<std::unique_ptr<fxme::ModulePresetLibrary>> effectPresetLibraries;
    std::map<juce::String, std::unique_ptr<fxme::ModulePresetTarget>> effectPresetTargets;

    std::array<camshaft::EffectChain, cam::numAux> auxChains;
    camshaft::EffectChain masterChain;

    // Loads the IR of a Cab or Reverb slot once a slot is set to it, off the
    // audio thread (as in MechanOdd). Declared after the chains; stopped
    // first in the destructor.
    class EffectLoader : public juce::Thread
    {
    public:
        explicit EffectLoader (CamshaftAudioProcessor& p) : juce::Thread ("Camshaft effect loader"), owner (p) {}
        void run() override
        {
            while (! threadShouldExit())
            {
                for (auto& c : owner.auxChains)
                    c.loadActiveEffects();
                owner.masterChain.loadActiveEffects();
                wait (20);
            }
        }
    private:
        CamshaftAudioProcessor& owner;
    };
    EffectLoader effectLoader { *this };

    // Parameter reads (raw atomics, resolved once) and per-block values.
    std::atomic<float>* camRaw[cam::numCams][cam::numCamFields] {};
    std::atomic<float>* modRaw[cam::numModulators][cam::numModFields] {};
    std::atomic<float>* routeRaw[cam::maxRoutes] {};
    float camValues[cam::numCams][cam::numCamFields] {};
    float modValues[cam::numModulators][cam::numModFields] {};
    float routeDepths[cam::maxRoutes] {};
    std::atomic<float> *pVoices = nullptr, *pPolyMode = nullptr, *pGlideTime = nullptr, *pGlideSync = nullptr,
                       *pGlideDiv = nullptr, *pGlideMode = nullptr, *pPbRange = nullptr, *pInterp = nullptr,
                       *pMasterOut = nullptr, *pLimiter = nullptr;
    std::atomic<float>* pAuxReturn[cam::numAux] {};
    std::atomic<float>* pAuxPan[cam::numAux] {};
    std::atomic<float>* pAuxDryMute[cam::numAux] {};
    cam::CamEngine::BlockSettings settings;

    std::vector<char> globalWasModulated;

    juce::AudioBuffer<float> dryBuffer;
    std::array<juce::AudioBuffer<float>, cam::numAux> auxBuffers;
    int preparedBlock = 0;

    juce::LinearSmoothedValue<float> outputGain { 1.0f };
    std::array<juce::LinearSmoothedValue<float>, cam::numAux> auxGainL, auxGainR;

    fxme::LookaheadLimiter limiter;
    fxme::StereoTap outputTap;
    std::atomic<float> outputPeakDb { -100.0f };
    float peakEnvelope = 0.0f;

    bool keepAudioOnPresetLoad = false;
    juce::ValueTree keptAudio, keptOptions, keptLearn, keptLocks;
    bool splashShown = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CamshaftAudioProcessor)
};
