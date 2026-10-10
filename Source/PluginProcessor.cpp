/*
  ------------------------------------------------------------------------------
    PluginProcessor.cpp

    See PluginProcessor.h.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamIDs.h"
#include "Effects/EffectFactory.h"
#include "Shell/ParameterLayout.h"
#include <BinaryData.h>
#include <unordered_set>
#if FXMEFX_HAS_MODULE_PRESETS
 #include <FxmeModulePresets.h>
#endif

using namespace camshaft;
using namespace cam;

//==============================================================================
CamshaftAudioProcessor::CamshaftAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout()),
      presetManager (apvts,
                     fxme::PresetManager::getVendorPresetDirectory ("Camshaft"),
                     BinaryData::namedResourceList,
                     BinaryData::namedResourceListSize,
                     BinaryData::getNamedResource)
{
    engine.setNumGlobalDests (destinations.numGlobal());
    globalWasModulated.assign ((size_t) destinations.numGlobal(), 0);

    for (int c = 0; c < numCams; ++c)
        for (int f = 0; f < numCamFields; ++f)
            camRaw[c][f] = apvts.getRawParameterValue (ids::cam (c, f));
    for (int m = 0; m < numModulators; ++m)
        for (int f = 0; f < numModFields; ++f)
            modRaw[m][f] = apvts.getRawParameterValue (ids::mod (m, f));
    for (int i = 0; i < maxRoutes; ++i)
        routeRaw[i] = apvts.getRawParameterValue (ids::routeDepth (i));

    pVoices    = apvts.getRawParameterValue (ids::voices);
    pPolyMode  = apvts.getRawParameterValue (ids::polyMode);
    pGlideTime = apvts.getRawParameterValue (ids::glideTime);
    pGlideSync = apvts.getRawParameterValue (ids::glideSync);
    pGlideDiv  = apvts.getRawParameterValue (ids::glideDiv);
    pGlideMode = apvts.getRawParameterValue (ids::glideMode);
    pPbRange   = apvts.getRawParameterValue (ids::pbRange);
    pInterp    = apvts.getRawParameterValue (ids::interp);
    pMasterOut = apvts.getRawParameterValue (ids::masterOut);
    pLimiter   = apvts.getRawParameterValue (ids::limiter);
    for (int a = 0; a < numAux; ++a)
    {
        pAuxReturn[a]  = apvts.getRawParameterValue (ids::auxReturn (a));
        pAuxPan[a]     = apvts.getRawParameterValue (ids::auxPan (a));
        pAuxDryMute[a] = apvts.getRawParameterValue (ids::auxDryMute (a));
    }

    for (int c = 0; c < numCams; ++c)
        settings.camValues[c] = camValues[c];
    for (int m = 0; m < numModulators; ++m)
        settings.modValues[m] = modValues[m];
    settings.routeDepths = routeDepths;

    createEffectPresets();

    // Connected here rather than in prepareToPlay: the editor can open before
    // the host prepares the plugin, and binds its panels to these instances.
    for (int a = 0; a < numAux; ++a)
        auxChains[(size_t) a].assignParameters (apvts, ids::auxPrefix (a));
    masterChain.assignParameters (apvts, ids::masterPrefix);

    outputTap.prepare (4096);

    // Presets: a partial preset (the factory ones list only what differs
    // from the defaults) must load the same whatever was playing before, so
    // everything goes back to its default first. The audio can be kept
    // across preset loads (keepAudioOnPresetLoad).
    presetManager.onBeforeSave = [this] { structure.stampVersion(); };
    presetManager.onBeforeLoad = [this]
    {
        keptAudio = {};
        keptOptions = {};
        // The MIDI learn map and the Kick-start locks are the player's, not
        // the patch's: kept when the incoming preset brings none.
        keptLearn = structure.getSection (StructureState::learnType).createCopy();
        keptLocks = structure.getSection (StructureState::locksType).createCopy();
        if (keepAudioOnPresetLoad)
        {
            keptAudio = apvts.state.getChildWithName (fxme::EmbeddedAudio::containerType).createCopy();
            keptOptions = apvts.state.getChildWithName ("SourceOptions").createCopy();
        }
        for (auto* p : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                rp->setValueNotifyingHost (rp->getDefaultValue());
    };
    presetManager.onAfterLoad = [this]
    {
        if (keepAudioOnPresetLoad)
        {
            auto state = apvts.state;
            auto oldAudio = state.getChildWithName (fxme::EmbeddedAudio::containerType);
            if (oldAudio.isValid())
                state.removeChild (oldAudio, nullptr);
            auto oldOptions = state.getChildWithName ("SourceOptions");
            if (oldOptions.isValid())
                state.removeChild (oldOptions, nullptr);
            if (keptAudio.isValid())
                state.appendChild (keptAudio, nullptr);
            if (keptOptions.isValid())
                state.appendChild (keptOptions, nullptr);
        }
        for (auto* kept : { &keptLearn, &keptLocks })
        {
            if (! kept->isValid())
                continue;
            auto section = structure.getSection (kept->getType());
            if (section.getNumChildren() == 0)
                for (const auto& child : *kept)
                    section.appendChild (child.createCopy(), nullptr);
        }
        structure.refreshFromState();

        // A preset older than schema 2 had its range parameters renamed by
        // the migration after the parameters were read: read them again.
        for (int c = 0; c < cam::numCams; ++c)
            for (int f : { (int) cam::cfStart, (int) cam::cfLength })
                if (auto* p = apvts.getParameter (ids::cam (c, f)))
                    if (const auto node = apvts.state.getChildWithProperty ("id", p->paramID); node.hasProperty ("value"))
                        p->setValueNotifyingHost (p->convertTo0to1 ((float) node.getProperty ("value")));

        sources.reloadFromState();
    };

    midiLearn.applyDefaultIfEmpty();
}

CamshaftAudioProcessor::~CamshaftAudioProcessor()
{
    effectLoader.stopThread (-1);   // before the chains it reads go away
}

void CamshaftAudioProcessor::createEffectPresets()
{
    const auto& types = EffectFactory::types();
    for (const auto& info : types)
        effectPresetLibraries.push_back (std::make_unique<fxme::ModulePresetLibrary> (
            info.moduleName, 1,
            fxme::PresetManager::getModulePresetDirectory (info.moduleName)
           #if FXMEFX_HAS_MODULE_PRESETS
            , FxmeModulePresets::namedResourceList,
            FxmeModulePresets::namedResourceListSize,
            FxmeModulePresets::getNamedResource
           #endif
            ));

    juce::StringArray chains;
    for (int a = 0; a < numAux; ++a)
        chains.add (ids::auxPrefix (a));
    chains.add (ids::masterPrefix);

    for (const auto& chain : chains)
        for (int slot = 0; slot < EffectChain::numSlots; ++slot)
            for (size_t t = 0; t < types.size(); ++t)
            {
                const auto perTypePfx = EffectSlot::perTypePrefix (EffectChain::slotPrefix (chain, slot), types[t].name);
                effectPresetTargets[perTypePfx] = std::make_unique<fxme::ModulePresetTarget> (
                    *effectPresetLibraries[t], apvts, EffectFactory::presetPrefix (perTypePfx, types[t]));
            }
}

//==============================================================================
int CamshaftAudioProcessor::getNumPrograms()
{
    return 1 + (int) presetManager.getFactoryPresets().size();   // 0 = current state
}

int CamshaftAudioProcessor::getCurrentProgram()
{
    return presetManager.getCurrentFactoryIndex() + 1;
}

void CamshaftAudioProcessor::setCurrentProgram (int index)
{
    if (index > 0)
        presetManager.loadFactoryPreset (index - 1);
}

const juce::String CamshaftAudioProcessor::getProgramName (int index)
{
    if (index == 0)
        return "Default";
    const auto& factory = presetManager.getFactoryPresets();
    return juce::isPositiveAndBelow (index - 1, (int) factory.size()) ? factory[(size_t) index - 1].name : juce::String();
}

//==============================================================================
void CamshaftAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    effectLoader.stopThread (-1);

    preparedBlock = juce::jmax (1, samplesPerBlock);
    engine.prepare (sampleRate, preparedBlock);

    dryBuffer.setSize (2, preparedBlock, false, true, false);
    for (auto& b : auxBuffers)
        b.setSize (2, preparedBlock, false, true, false);

    for (auto& c : auxChains)
        c.prepare (sampleRate, 2, preparedBlock);
    masterChain.prepare (sampleRate, 2, preparedBlock);
    for (auto& c : auxChains)
        c.loadActiveEffects();
    masterChain.loadActiveEffects();
    effectLoader.startThread (juce::Thread::Priority::background);

    limiter.prepare (sampleRate, 1.0f, 80.0f);
    limiter.setCeilingDb (-0.3f);
    setLatencySamples (limiter.getLatencySamples());

    outputGain.reset (sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pMasterOut->load(), -60.0f));
    for (int a = 0; a < numAux; ++a)
    {
        auxGainL[(size_t) a].reset (sampleRate, 0.02);
        auxGainR[(size_t) a].reset (sampleRate, 0.02);
    }
    peakEnvelope = 0.0f;
}

void CamshaftAudioProcessor::releaseResources() {}

bool CamshaftAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

//==============================================================================
void CamshaftAudioProcessor::readParameters()
{
    for (int c = 0; c < numCams; ++c)
        for (int f = 0; f < numCamFields; ++f)
            camValues[c][f] = camRaw[c][f]->load (std::memory_order_relaxed);
    for (int m = 0; m < numModulators; ++m)
        for (int f = 0; f < numModFields; ++f)
            modValues[m][f] = modRaw[m][f]->load (std::memory_order_relaxed);
    for (int i = 0; i < maxRoutes; ++i)
        routeDepths[i] = routeRaw[i]->load (std::memory_order_relaxed);

    settings.polyphony = (int) pVoices->load();
    settings.voiceMode = (int) pPolyMode->load();
    settings.glideLegatoOnly = (int) pGlideMode->load() == 1;
    settings.pitchBendRange = pPbRange->load();
    settings.interpolation = (int) pInterp->load();
    for (int a = 0; a < numAux; ++a)
        settings.auxDryMute[a] = pAuxDryMute[a]->load() > 0.5f;

    settings.bpm = 120.0;
    settings.playing = false;
    if (auto* head = getPlayHead())
        if (const auto pos = head->getPosition())
        {
            if (const auto bpm = pos->getBpm())
                settings.bpm = *bpm;
            if (const auto ppq = pos->getPpqPosition())
                settings.ppq = *ppq;
            settings.playing = pos->getIsPlaying();
        }

    settings.glideSeconds = pGlideSync->load() > 0.5f
        ? (float) (fxme::Lfo::syncDivisionBeats ((int) pGlideDiv->load()) * 60.0 / settings.bpm)
        : pGlideTime->load() * 0.001f;
}

void CamshaftAudioProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
        engine.noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())
        engine.noteOff (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isPitchWheel())
        engine.pitchBend ((float) (m.getPitchWheelValue() - 8192) / 8192.0f);
    else if (m.isAllNotesOff() || m.isAllSoundOff())
        engine.allNotesOff();
    else if (m.isController())
    {
        if (! midiLearn.handleController (m.getControllerNumber(), m.getControllerValue()))
            engine.controller (m.getControllerNumber(), (float) m.getControllerValue() / 127.0f);
    }
    else if (m.isChannelPressure())
        engine.channelPressure ((float) m.getChannelPressureValue() / 127.0f);
    else if (m.isAftertouch())
        engine.polyPressure (m.getNoteNumber(), (float) m.getAfterTouchValue() / 127.0f);
}

void CamshaftAudioProcessor::applyGlobalModulation()
{
    // Base value from the parameter itself (host automation, the GUI), plus
    // this block's modulation, written where the DSP reads it. A destination
    // no longer modulated is put back once.
    for (int g = 0; g < destinations.numGlobal(); ++g)
    {
        const bool modulated = engine.isGlobalDestModulated (g);
        if (! modulated && ! globalWasModulated[(size_t) g])
            continue;

        const auto& e = destinations.global (g);
        const float base = e.param->getValue();
        const float v = modulated ? juce::jlimit (0.0f, 1.0f, base + engine.globalDestOffset (g)) : base;
        e.raw->store (e.param->convertFrom0to1 (v), std::memory_order_relaxed);
        globalWasModulated[(size_t) g] = modulated ? 1 : 0;
    }
}

void CamshaftAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int total = buffer.getNumSamples();
    if (preparedBlock <= 0)
    {
        buffer.clear();
        return;
    }

    // A host may send more than it announced: work in prepared-size chunks.
    for (int start = 0; start < total; start += preparedBlock)
        processChunk (buffer, midi, start, juce::jmin (preparedBlock, total - start));
}

void CamshaftAudioProcessor::processChunk (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, int start, int n)
{
    readParameters();
    engine.beginBlock (settings);

    dryBuffer.clear (0, n);
    for (auto& b : auxBuffers)
        b.clear (0, n);

    cam::CamEngine::Buses buses;
    buses.dry[0] = dryBuffer.getWritePointer (0);
    buses.dry[1] = dryBuffer.getWritePointer (1);
    for (int a = 0; a < numAux; ++a)
    {
        buses.aux[a][0] = auxBuffers[(size_t) a].getWritePointer (0);
        buses.aux[a][1] = auxBuffers[(size_t) a].getWritePointer (1);
    }

    // The engine renders up to each MIDI event, which then takes effect.
    int pos = 0;
    for (const auto metadata : midi)
    {
        const int at = metadata.samplePosition - start;
        if (at < 0 || at >= n)
            continue;
        if (at > pos)
        {
            engine.render (buses, pos, at - pos);
            pos = at;
        }
        handleMidi (metadata.getMessage());
    }
    if (pos < n)
        engine.render (buses, pos, n - pos);

    applyGlobalModulation();

    // Turbo 1 and 2: their chains, then back into the mix.
    for (int a = 0; a < numAux; ++a)
    {
        juce::AudioBuffer<float> aux (auxBuffers[(size_t) a].getArrayOfWritePointers(), 2, n);
        auxChains[(size_t) a].checkParameters();
        auxChains[(size_t) a].process (aux);

        const float gain = juce::Decibels::decibelsToGain (pAuxReturn[a]->load(), -60.0f);
        const float pan = juce::jlimit (-1.0f, 1.0f, pAuxPan[a]->load());
        auxGainL[(size_t) a].setTargetValue (gain * juce::jmin (1.0f, 1.0f - pan));
        auxGainR[(size_t) a].setTargetValue (gain * juce::jmin (1.0f, 1.0f + pan));
        for (int i = 0; i < n; ++i)
        {
            dryBuffer.addSample (0, i, aux.getSample (0, i) * auxGainL[(size_t) a].getNextValue());
            dryBuffer.addSample (1, i, aux.getSample (1, i) * auxGainR[(size_t) a].getNextValue());
        }
    }

    // Exhaust.
    juce::AudioBuffer<float> mix (dryBuffer.getArrayOfWritePointers(), 2, n);
    masterChain.checkParameters();
    masterChain.process (mix);

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (pMasterOut->load(), -60.0f));
    for (int i = 0; i < n; ++i)
    {
        const float g = outputGain.getNextValue();
        mix.setSample (0, i, mix.getSample (0, i) * g);
        mix.setSample (1, i, mix.getSample (1, i) * g);
    }

    // Rev limiter: always in the path (constant latency), switched off by
    // raising its ceiling out of reach.
    limiter.setCeilingDb (pLimiter->load() > 0.5f ? -0.3f : 200.0f);
    limiter.process (mix.getWritePointer (0), mix.getWritePointer (1), n);

    // Out.
    const int outChannels = buffer.getNumChannels();
    if (outChannels >= 2)
    {
        buffer.copyFrom (0, start, mix, 0, 0, n);
        buffer.copyFrom (1, start, mix, 1, 0, n);
        for (int ch = 2; ch < outChannels; ++ch)
            buffer.clear (ch, start, n);
    }
    else if (outChannels == 1)
    {
        buffer.copyFrom (0, start, mix.getReadPointer (0), n, 0.5f);
        buffer.addFrom (0, start, mix.getReadPointer (1), n, 0.5f);
    }

    // Master view.
    outputTap.push (mix.getReadPointer (0), mix.getReadPointer (1), n);
    const float peak = juce::jmax (mix.getMagnitude (0, 0, n), mix.getMagnitude (1, 0, n));
    const float fall = std::exp (-(float) n / (0.25f * (float) getSampleRate()));
    peakEnvelope = juce::jmax (peak, peakEnvelope * fall);
    outputPeakDb.store (juce::Decibels::gainToDecibels (peakEnvelope, -100.0f), std::memory_order_relaxed);
}

//==============================================================================
void CamshaftAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    structure.stampVersion();
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void CamshaftAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    StructureState::migrate (tree);

    // Parameters the state does not mention (added after it was saved) take
    // their defaults rather than keep whatever was set before.
    std::unordered_set<std::string> present;
    for (const auto& child : tree)
        if (child.hasType ("PARAM"))
            present.insert (child.getProperty ("id").toString().toStdString());
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (present.count (rp->paramID.toStdString()) == 0)
            {
                juce::ValueTree param ("PARAM");
                param.setProperty ("id", rp->paramID, nullptr);
                param.setProperty ("value", rp->convertFrom0to1 (rp->getDefaultValue()), nullptr);
                tree.appendChild (param, nullptr);
            }

    apvts.replaceState (tree);
    sources.reloadFromState();
}

//==============================================================================
juce::AudioProcessorEditor* CamshaftAudioProcessor::createEditor()
{
    return new CamshaftAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CamshaftAudioProcessor();
}
