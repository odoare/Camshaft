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

    Three kinds of source, chosen in this order on every load:

      imported   audio embedded in the state (an imported file)
      bundled    one of the samples bundled with the plugin (FactorySamples.h),
                 named in the state (SourceOptions factory="<id>"), not
                 embedded: the factory presets use them, and a preset made
                 on one stays small
      morph      neither: the built-in demo source (Engine/DemoSource.h),
                 so the plugin sounds from the first note

    Importing a file replaces a bundled sample; choosing a bundled sample or
    the morph removes the imported audio.

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
        juce::String name;                           // file name, sample name, or "Built-in morph"
        bool builtIn = true;                         // nothing imported: the morph or a bundled sample
        juce::String factoryId;                      // the bundled sample, when it is one
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

    /** Back to the built-in morph (removes the embedded audio and any
        bundled sample). */
    void clear();

    /** Plays bundled sample `id` (FactorySamples.h), removing the embedded
        audio; an empty or unknown id is the morph. */
    void setFactorySample (const juce::String& id);

    /** The bundled sample the state names, empty when none. */
    juce::String getFactorySample() const;

    /** Writes the source as it was imported, or the bundled sample's file;
        false for the morph. */
    bool exportOriginal (const juce::File& destination) const;

    /** The file name an export would suggest (empty for the morph). */
    juce::String getExportName() const;

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
        juce::String factoryId;      // a bundled sample, when there is no embedded audio
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
