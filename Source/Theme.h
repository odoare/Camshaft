/*
  ------------------------------------------------------------------------------
    Theme.h

    Camshaft's look, in one place. Flat design with mechanical cues: graphite
    panels with a faint brushed sheen, engraved section labels, gauge
    typography, and one warning-orange accent. Each cam has its own colour,
    used from its scope row down to its strip so a cam reads vertically.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>

namespace camshaft::theme
{
    inline const juce::String displayName { "Camshaft" };
    inline constexpr const char* tagline = "Range wavetable synth";
    inline constexpr int topBarHeight = 54;

    // ---- Palette ---------------------------------------------------------------
    inline const juce::Colour background { 0xff15161a };   // graphite
    inline const juce::Colour panel      { 0xff1d1f24 };
    inline const juce::Colour panelHi    { 0xff262931 };
    inline const juce::Colour panelLine  { 0xff383b45 };
    inline const juce::Colour plotBack   { 0xff0e0f12 };
    inline const juce::Colour grid       { 0x26ffffff };
    inline const juce::Colour text       { 0xffe6e4de };
    inline const juce::Colour dimText    { 0xff9a978f };
    inline const juce::Colour engraved   { 0xff6e6c66 };
    inline const juce::Colour waveform   { 0xffffffff };   // the wavetable plot: almost white
    inline const juce::Colour topBarBg   { 0xff0f1013 };
    inline const juce::Colour knobBody   { 0xff2b2b2e };
    inline const juce::Colour knobCentre { 0xff0c0c0e };   // the knobs' disc: almost black

    /** The warning orange: the top bar hairline, the presets, the redline. */
    inline const juce::Colour accent     { 0xffff7a1a };
    inline const juce::Colour redline    { 0xffe5332a };
    inline const juce::Colour steel      { 0xff9fb3c8 };   // modulators, matrix

    inline juce::Colour camColour (int c) noexcept
    {
        static const juce::Colour colours[] = { juce::Colour (0xffe8a33a),    // brass
                                                juce::Colour (0xff4fc3c7),    // teal
                                                juce::Colour (0xffd2668f),    // rose
                                                juce::Colour (0xff8f9cf5) };  // periwinkle
        return colours[(size_t) juce::jlimit (0, 3, c)];
    }

    /** Modulator slot m's colour: its chip, and the ring it draws around the
        knobs it modulates. Hues spread by the golden ratio, so neighbouring
        slots never look alike. */
    inline juce::Colour modColour (int m) noexcept
    {
        const float hue = std::fmod (0.03f + 0.6180339887f * (float) m, 1.0f);
        return juce::Colour::fromHSV (hue, 0.58f, 0.95f, 1.0f);
    }

    inline juce::Colour auxColour (int a) noexcept { return a == 0 ? juce::Colour (0xff7cc46e) : juce::Colour (0xff5aa0e0); }
    inline const juce::Colour exhaustColour { 0xffd0613f };

    // ---- Fonts ---------------------------------------------------------------
    /** Every label and read-out is drawn this much larger than its nominal
        size (the layouts keep their sizes). */
    inline constexpr float fontScale = 1.12f;

    inline juce::Font labelFont (float h = 12.0f)  { return juce::Font (juce::FontOptions (h * fontScale, juce::Font::bold)); }
    inline juce::Font gaugeFont (float h = 11.0f)  { return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h * fontScale, juce::Font::plain)); }

    // ---- Control styling -----------------------------------------------------
    inline void accentSlider (juce::Slider& s, juce::Colour a)
    {
        s.setColour (juce::Slider::rotarySliderFillColourId,    knobCentre);
        s.setColour (juce::Slider::rotarySliderOutlineColourId, a.darker (1.6f));
        s.setColour (juce::Slider::trackColourId,               a);
        s.setColour (juce::Slider::thumbColourId,               a.brighter (0.4f));
        s.setColour (juce::Slider::backgroundColourId,          juce::Colours::black.withAlpha (0.35f));
    }

    inline void accentCombo (juce::ComboBox& c, juce::Colour a)
    {
        c.setColour (juce::ComboBox::outlineColourId, a.darker());
        c.setColour (juce::ComboBox::arrowColourId,   a.brighter (0.3f));
    }

    inline void accentToggle (juce::ToggleButton& b, juce::Colour a)
    {
        b.setColour (juce::ToggleButton::tickColourId,         a);
        b.setColour (juce::ToggleButton::tickDisabledColourId, a.withAlpha (0.4f));
    }

    // ---- Painting --------------------------------------------------------------
    inline void paintBackground (juce::Graphics& g, juce::Rectangle<float> b)
    {
        g.setColour (background);
        g.fillRect (b);
        // A faint brushed sheen across the top.
        juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.025f), b.getX(), b.getY(),
                                    juce::Colours::transparentWhite, b.getX(), b.getY() + b.getHeight() * 0.6f, false);
        g.setGradientFill (sheen);
        g.fillRect (b);
    }

    /** A machined panel: flat fill, hairline edge, a lighter top edge. */
    inline void paintPanel (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour tint = juce::Colours::transparentBlack)
    {
        g.setColour (panel.interpolatedWith (tint, tint.isTransparent() ? 0.0f : 0.06f));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (panelLine);
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 5.0f, r.getRight() - 5.0f);
    }

    /** An engraved label: dark text with a light edge under it. */
    inline void paintEngraved (juce::Graphics& g, const juce::String& textToDraw, juce::Rectangle<int> r,
                               juce::Justification j = juce::Justification::centredLeft, float h = 11.0f)
    {
        g.setFont (labelFont (h));
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawText (textToDraw.toUpperCase(), r.translated (0, 1), j, true);
        g.setColour (engraved);
        g.drawText (textToDraw.toUpperCase(), r, j, true);
    }
}
