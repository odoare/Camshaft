/*
  ------------------------------------------------------------------------------
    SourceManager.h

    The audio file the cams are cut from: importing it, keeping it inside the
    state (fxme::EmbeddedAudio, so a patch never depends on a path), decoding
    it again on a session or preset load, and handing it to the engine and to
    the editor.

    Import (message thread): any format JUCE's format manager reads (WAV,
    AIFF, FLAC, Ogg...). Longer than maxSeconds, only the start is kept. The
    original file is embedded as it is (its own rate, all its channels);
    what the engine gets is decoded from the embedded copy on a background
    thread, with the per-file options applied, which are stored next to it:

      channels   stereo (mono files are mono), left, right or mid only
      normalise  peak-normalised to -1 dBFS, the gain kept so it can be undone

    The engine reads the source at its original rate: the cams' widths are
    in source samples, so a patch sounds the same at any session rate (see
    doc/architecture.md, "Sample handling", for why this departs from the
    plan's resampling on import).

    Without any embedded audio the built-in demo source is used
    (Engine/DemoSource.h): the plugin sounds from the first note, and the
    factory presets need no audio of their own.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "Engine/CamEngine.h"
#include <mutex>

namespace camshaft
{

class SourceManager : public juce::ChangeBroadcaster
{
public:
    static constexpr double maxSeconds = 60.0;
    static constexpr const char* slotId = "source";

    enum class ChannelMode { stereo = 0, left, right, mid };

    /** What the editor draws: the decoded source plus its overview. */
    struct View
    {
        std::shared_ptr<const fxme::WavetableSource> source;
        std::vector<fxme::MinMaxPyramid> overview;   // one per channel
        juce::String name;                           // file name, or "Built-in"
        bool builtIn = true;
        float normaliseGainDb = 0.0f;
    };

    SourceManager (juce::AudioProcessorValueTreeState& apvts, cam::CamEngine& engine);
    ~SourceManager() override;

    //==========================================================================
    // Message thread

    /** Embeds `file` and loads it, on the loader thread (encoding a long
        file takes a while); the embedded copy lands in the state when it is
        ready. Returns an error message for a file that cannot be read at
        all; a later failure shows in getLastError(). */
    juce::String importFile (const juce::File& file);

    /** Back to the built-in source (removes the embedded audio). */
    void clear();

    /** Writes the embedded audio as it was imported. */
    bool exportOriginal (const juce::File& destination) const;

    ChannelMode getChannelMode() const;
    void setChannelMode (ChannelMode mode);
    bool getNormalise() const;
    void setNormalise (bool shouldNormalise);

    /** The latest decoded source (may lag a reload by a few milliseconds). */
    std::shared_ptr<const View> getView() const;

    bool isLoading() const noexcept { return loading.load(); }

    /** Why the last decode failed (empty when it worked). Message thread. */
    juce::String getLastError() const;

    /** Decodes the state's audio again (after a session or preset load). Any
        thread that owns the state at that moment. */
    void reload();

    /** As reload(), for a whole new state (session or preset load): an import
        still in flight is dropped. */
    void reloadFromState();

    static bool canImport (const juce::String& fileName);

private:
    struct Job
    {
        juce::ValueTree audio;       // detached copy of the EmbeddedAudio container
        juce::File importFile;       // a file to embed first (an import)
        juce::String name;
        ChannelMode mode = ChannelMode::stereo;
        bool normalise = true;
        int serial = 0;
    };

    class Loader;
    void run (Job job);
    bool embedInto (const juce::File& file, juce::ValueTree& holder, double& rate, int& channels, juce::String& error);
    juce::ValueTree options() const;

    juce::AudioProcessorValueTreeState& apvts;
    cam::CamEngine& engine;

    std::unique_ptr<Loader> loader;
    std::atomic<bool> loading { false };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    // An import still being encoded (message thread): a reload meanwhile
    // re-runs it with the new options rather than decode the old audio.
    juce::File inFlightImport;
    std::atomic<int> importSerial { 0 };
    std::atomic<int> serial { 0 };

    mutable std::mutex viewLock;
    std::shared_ptr<const View> view;
    juce::String lastError;              // guarded by viewLock
};

} // namespace camshaft
