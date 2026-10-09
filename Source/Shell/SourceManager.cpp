/*
  ------------------------------------------------------------------------------
    SourceManager.cpp

    See SourceManager.h.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "SourceManager.h"
#include "Engine/DemoSource.h"

namespace camshaft
{

namespace
{
    const juce::Identifier optionsType ("SourceOptions");

    juce::ValueTree findEmbeddedEntry (const juce::ValueTree& state)
    {
        return state.getChildWithName (fxme::EmbeddedAudio::containerType)
                    .getChildWithProperty (fxme::EmbeddedAudio::slotProperty, SourceManager::slotId);
    }

    bool writeWav (juce::AudioFormatReader& reader, const juce::File& file, juce::int64 numSamples)
    {
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        if (! static_cast<juce::FileOutputStream*> (stream.get())->openedOk())
            return false;

        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                       .withSampleRate (reader.sampleRate)
                                                       .withNumChannels ((int) reader.numChannels)
                                                       .withBitsPerSample (32));
        return writer != nullptr && writer->writeFromAudioReader (reader, 0, numSamples);
    }
}

//==============================================================================
class SourceManager::Loader : public juce::Thread
{
public:
    explicit Loader (SourceManager& o) : juce::Thread ("Camshaft source loader"), owner (o) {}

    void post (Job job)
    {
        {
            const std::lock_guard<std::mutex> lock (jobLock);
            pending = std::move (job);
            hasJob = true;
        }
        notify();
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            Job job;
            bool got = false;
            {
                const std::lock_guard<std::mutex> lock (jobLock);
                if (hasJob)
                {
                    job = pending;
                    pending = {};
                    hasJob = false;
                    got = true;
                }
            }
            if (got)
                owner.run (job);
            else
                wait (200);
        }
    }

private:
    SourceManager& owner;
    std::mutex jobLock;
    Job pending;
    bool hasJob = false;
};

//==============================================================================
SourceManager::SourceManager (juce::AudioProcessorValueTreeState& state, cam::CamEngine& e)
    : apvts (state), engine (e)
{
    loader = std::make_unique<Loader> (*this);
    loader->startThread (juce::Thread::Priority::background);

    // The engine plays the built-in source until the state says otherwise.
    Job builtIn;
    builtIn.serial = ++serial;
    run (builtIn);
}

SourceManager::~SourceManager()
{
    loader->stopThread (4000);
}

bool SourceManager::canImport (const juce::String& fileName)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    return formats.findFormatForFileExtension (juce::File (fileName).getFileExtension()) != nullptr;
}

juce::ValueTree SourceManager::options() const
{
    auto state = apvts.state;
    return state.getOrCreateChildWithName (optionsType, nullptr);
}

SourceManager::ChannelMode SourceManager::getChannelMode() const
{
    return (ChannelMode) juce::jlimit (0, 3, (int) options().getProperty ("channels", 0));
}

void SourceManager::setChannelMode (ChannelMode mode)
{
    options().setProperty ("channels", (int) mode, nullptr);
    reload();
}

bool SourceManager::getNormalise() const
{
    return (bool) options().getProperty ("normalise", true);
}

void SourceManager::setNormalise (bool shouldNormalise)
{
    options().setProperty ("normalise", shouldNormalise, nullptr);
    reload();
}

std::shared_ptr<const SourceManager::View> SourceManager::getView() const
{
    const std::lock_guard<std::mutex> lock (viewLock);
    return view;
}

//==============================================================================
juce::String SourceManager::importFile (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return "Cannot read " + file.getFileName() + " as audio.";
    if (reader->lengthInSamples < 16)
        return file.getFileName() + " is too short.";

    const auto maxSamples = (juce::int64) (maxSeconds * reader->sampleRate);
    bool ok;
    if (reader->lengthInSamples > maxSamples)
    {
        // Keep the first maxSeconds only: embed a trimmed copy.
        juce::TemporaryFile trimmed (".wav");
        ok = writeWav (*reader, trimmed.getFile(), maxSamples)
             && fxme::EmbeddedAudio::embedFile (apvts.state, slotId, trimmed.getFile());
    }
    else
        ok = fxme::EmbeddedAudio::embedFile (apvts.state, slotId, file);

    if (! ok)
        return "Could not embed " + file.getFileName() + ".";

    // The display name is the original file's, whatever was embedded.
    findEmbeddedEntry (apvts.state).setProperty (fxme::EmbeddedAudio::nameProperty, file.getFileName(), nullptr);
    options().setProperty ("originalRate", reader->sampleRate, nullptr);
    options().setProperty ("originalChannels", (int) reader->numChannels, nullptr);

    reload();
    return {};
}

void SourceManager::clear()
{
    fxme::EmbeddedAudio::removeEmbedded (apvts.state, slotId);
    reload();
}

bool SourceManager::exportOriginal (const juce::File& destination) const
{
    auto reader = fxme::EmbeddedAudio::createReader (apvts.state, slotId);
    return reader != nullptr && writeWav (*reader, destination, reader->lengthInSamples);
}

void SourceManager::reload()
{
    Job job;
    const auto entry = findEmbeddedEntry (apvts.state);
    if (entry.isValid())
    {
        // A detached copy: the loader thread must not touch the live state.
        juce::ValueTree holder ("Holder");
        juce::ValueTree container (fxme::EmbeddedAudio::containerType);
        container.appendChild (entry.createCopy(), nullptr);
        holder.appendChild (container, nullptr);
        job.audio = holder;
        job.name = entry.getProperty (fxme::EmbeddedAudio::nameProperty).toString();
    }
    job.mode = getChannelMode();
    job.normalise = getNormalise();
    job.serial = ++serial;
    loading.store (true);
    loader->post (job);
}

//==============================================================================
void SourceManager::run (const Job& job)
{
    auto result = std::make_shared<View>();
    std::shared_ptr<fxme::WavetableSource> src;

    if (job.audio.isValid())
    {
        if (auto reader = fxme::EmbeddedAudio::createReader (job.audio, slotId))
        {
            const auto maxSamples = (juce::int64) (maxSeconds * reader->sampleRate);
            const int n = (int) juce::jmin (reader->lengthInSamples, maxSamples);
            const int inChannels = juce::jmax (1, (int) reader->numChannels);
            juce::AudioBuffer<float> buffer (inChannels, n);
            reader->read (&buffer, 0, n, 0, true, true);

            src = std::make_shared<fxme::WavetableSource>();
            src->numSamples = n;
            src->sampleRate = reader->sampleRate;

            auto channel = [&] (int ch) { return std::vector<float> (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + n); };
            if (inChannels == 1)
                src->channels.push_back (channel (0));
            else switch (job.mode)
            {
                case ChannelMode::left:  src->channels.push_back (channel (0)); break;
                case ChannelMode::right: src->channels.push_back (channel (1)); break;
                case ChannelMode::mid:
                {
                    std::vector<float> mid ((size_t) n);
                    for (int i = 0; i < n; ++i)
                        mid[(size_t) i] = 0.5f * (buffer.getSample (0, i) + buffer.getSample (1, i));
                    src->channels.push_back (std::move (mid));
                    break;
                }
                case ChannelMode::stereo:
                default:
                    src->channels.push_back (channel (0));
                    src->channels.push_back (channel (1));
                    break;
            }

            if (job.normalise)
            {
                float peak = 0.0f;
                for (const auto& ch : src->channels)
                    for (auto v : ch)
                        peak = juce::jmax (peak, std::abs (v));
                if (peak > 1.0e-6f)
                {
                    const float gain = juce::Decibels::decibelsToGain (-1.0f) / peak;
                    for (auto& ch : src->channels)
                        for (auto& v : ch)
                            v *= gain;
                    result->normaliseGainDb = juce::Decibels::gainToDecibels (gain);
                }
            }

            result->name = job.name.isNotEmpty() ? job.name : juce::String ("Imported audio");
            result->builtIn = false;
        }
    }

    if (src == nullptr)
    {
        src = cam::makeDemoSource();
        result->name = "Built-in morph";
        result->builtIn = true;
    }

    // A newer request arrived meanwhile: let that one win.
    if (job.serial != serial.load())
        return;

    engine.setSource (src);

    result->source = src;
    result->overview.resize (src->channels.size());
    for (size_t ch = 0; ch < src->channels.size(); ++ch)
        result->overview[ch].build (src->channels[ch].data(), src->numSamples);

    {
        const std::lock_guard<std::mutex> lock (viewLock);
        view = result;
    }
    loading.store (false);
    sendChangeMessage();
}

} // namespace camshaft
