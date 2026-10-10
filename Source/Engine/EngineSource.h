/*
  ------------------------------------------------------------------------------
    EngineSource.h

    The audio the cams read, in the form the engine reads it: the recording
    as an octave pyramid (fxme::MipmappedBuffer, for band-limited reading at
    any speed) and its analysis (fxme::SourceAnalysis: zero crossings and a
    period track, for snapping). Also keeps the plain recording, for the
    editor's displays.

    Made once per recording on a non-audio thread (make()), immutable
    afterwards, handed to the engine with CamEngine::setSource().

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <FxmeTools/synth/MipmappedBuffer.h>
#include <FxmeTools/synth/SourceAnalysis.h>
#include <FxmeTools/synth/WavetableBuilder.h>
#include <memory>
#include <vector>

namespace cam
{

struct EngineSource
{
    std::shared_ptr<const fxme::WavetableSource> raw;   // the recording, for displays
    fxme::MipmappedBuffer buffer;
    fxme::SourceAnalysis analysis;

    int numSamples() const noexcept  { return buffer.getNumSamples(); }
    int numChannels() const noexcept { return buffer.getNumChannels(); }

    /** Builds everything from a recording (allocates; a second or so for a
        minute of stereo audio). */
    static std::shared_ptr<const EngineSource> make (std::shared_ptr<const fxme::WavetableSource> recording)
    {
        auto s = std::make_shared<EngineSource>();
        s->raw = recording;
        if (recording != nullptr && recording->numSamples > 0)
        {
            std::vector<const float*> channels;
            for (const auto& ch : recording->channels)
                channels.push_back (ch.data());
            s->buffer.build (channels.data(), (int) channels.size(), recording->numSamples);
            s->analysis.build (channels.data(), (int) channels.size(), recording->numSamples);
        }
        return s;
    }
};

} // namespace cam
