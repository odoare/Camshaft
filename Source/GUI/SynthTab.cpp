/*
  ------------------------------------------------------------------------------
    SynthTab.cpp

    See SynthTab.h.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "SynthTab.h"
#include "PluginProcessor.h"
#include "Theme.h"
#include "Tooltips.h"
#include "Shell/FactorySamples.h"

namespace camshaft
{

using namespace cam;

//==============================================================================
void DiceButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto b = getLocalBounds().toFloat().reduced (2.0f);
    const float d = juce::jmin (b.getWidth(), b.getHeight());
    b = b.withSizeKeepingCentre (d, d);
    const auto colour = down ? theme::accent.darker (0.3f) : over ? theme::accent.brighter (0.3f) : theme::accent;
    g.setColour (colour);
    g.drawRoundedRectangle (b, d * 0.18f, 1.4f);
    // Five pips.
    const float pip = d * 0.16f;
    for (auto [px, py] : { std::pair<float, float> { 0.27f, 0.27f }, { 0.73f, 0.27f }, { 0.5f, 0.5f }, { 0.27f, 0.73f }, { 0.73f, 0.73f } })
        g.fillEllipse (juce::Rectangle<float> (pip, pip).withCentre ({ b.getX() + px * d, b.getY() + py * d }));
}

//==============================================================================
SynthTab::SynthTab (CamshaftAudioProcessor& p)
    : processor (p), waveform (p), modulators (p), matrix (p), belt (p), dashboard (p)
{
    // ---- File band ----
    importButton.setTooltip (tips::file::import);
    exportButton.setTooltip (tips::file::exportAudio);
    builtInButton.setTooltip (tips::file::builtIn);
    importButton.onClick = [this] { importDialog(); };
    exportButton.onClick = [this] { exportDialog(); };
    builtInButton.onClick = [this] { builtInMenu(); };
    for (auto* b : { &importButton, &exportButton, &builtInButton })
    {
        b->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }

    channelBox.addItemList ({ "Stereo", "Left only", "Right only", "Mid only" }, 1);
    channelBox.setTooltip (tips::file::channels);
    theme::accentCombo (channelBox, theme::accent);
    channelBox.onChange = [this]
    {
        processor.getSources().setChannelMode ((SourceManager::ChannelMode) (channelBox.getSelectedId() - 1));
    };
    addAndMakeVisible (channelBox);

    normaliseButton.setTooltip (tips::file::normalise);
    theme::accentToggle (normaliseButton, theme::accent);
    normaliseButton.onClick = [this] { processor.getSources().setNormalise (normaliseButton.getToggleState()); };
    addAndMakeVisible (normaliseButton);

    fileLabel.setColour (juce::Label::textColourId, theme::text);
    fileLabel.setFont (theme::gaugeFont (12.0f));
    addAndMakeVisible (fileLabel);
    messageLabel.setColour (juce::Label::textColourId, theme::accent);
    messageLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (messageLabel);

    waveform.setTooltip (tips::file::waveform);
    waveform.onImported = [this] (const juce::String& error) { showMessage (error); };
    addAndMakeVisible (waveform);

    // ---- Scopes and strips ----
    for (int c = 0; c < numCams; ++c)
    {
        auto row = std::make_unique<ScopeRow> (processor, c);
        addAndMakeVisible (*row);
        scopes.push_back (std::move (row));
        auto strip = std::make_unique<CamStrip> (processor, c);
        addAndMakeVisible (*strip);
        strips.push_back (std::move (strip));
    }

    // ---- Bottom band ----
    const char* names[] = { "Belt & Viz", "Modulators", "Matrix", "Turbo 1", "Turbo 2", "Exhaust" };
    const char* tabTips[] = { tips::bottom::belt, tips::bottom::modulators, tips::bottom::matrix,
                              tips::bottom::turbo, tips::bottom::turbo, tips::bottom::exhaust };
    for (int i = 0; i < numBottomTabs; ++i)
    {
        auto& t = bottomTabs[(size_t) i];
        t.setButtonText (names[i]);
        t.setTooltip (tabTips[i]);
        t.setColour (juce::TextButton::buttonOnColourId, theme::accent.brighter (0.35f));
        t.setColour (juce::TextButton::textColourOnId, theme::background);
        t.setMouseClickGrabsKeyboardFocus (false);
        t.onClick = [this, i] { showBottom (i); };
        addAndMakeVisible (t);
    }
    modulators.onCurveProbe = [this] (int slot, float value) { waveform.setProbe (slot, value); };
    addChildComponent (modulators);
    addChildComponent (matrix);
    addChildComponent (belt);
    addChildComponent (dashboard);
    dashboard.onToggleFullView = [this] { setDashboardFull (! dashboardFull); };
    for (int c = 0; c < 3; ++c)
    {
        chainPanes[(size_t) c] = std::make_unique<ChainPane> (processor, c);
        addChildComponent (*chainPanes[(size_t) c]);
    }
    showBottom (0);

    camsDice.setTooltip (tips::dice::cams);
    filtersDice.setTooltip (tips::dice::filters);
    modsDice.setTooltip (tips::dice::modulators);
    camsDice.onClick = [this] { processor.randomize (RandomSection::cams); };
    filtersDice.onClick = [this] { processor.randomize (RandomSection::filters); };
    modsDice.onClick = [this] { processor.randomize (RandomSection::modulators); };
    for (auto* d : { &camsDice, &filtersDice, &modsDice })
        addAndMakeVisible (*d);

    processor.getSources().addChangeListener (this);
    changeListenerCallback (nullptr);
}

SynthTab::~SynthTab()
{
    processor.getSources().removeChangeListener (this);
}

void SynthTab::changeListenerCallback (juce::ChangeBroadcaster*)
{
    auto& sources = processor.getSources();
    channelBox.setSelectedId ((int) sources.getChannelMode() + 1, juce::dontSendNotification);
    normaliseButton.setToggleState (sources.getNormalise(), juce::dontSendNotification);

    if (const auto view = sources.getView(); view != nullptr && view->source != nullptr)
    {
        const auto& s = *view->source;
        juce::String info = view->name + "   " + juce::String ((double) s.numSamples / s.sampleRate, 2) + " s, "
                          + juce::String (s.sampleRate / 1000.0, 1) + " kHz, "
                          + (s.getNumChannels() > 1 ? "stereo" : "mono");
        if ((! view->builtIn || view->factoryId.isNotEmpty()) && sources.getNormalise())
            info << ", normalised " << juce::String (view->normaliseGainDb, 1) << " dB";
        fileLabel.setText (info, juce::dontSendNotification);
        exportButton.setEnabled (! view->builtIn || view->factoryId.isNotEmpty());
    }

    if (const auto error = sources.getLastError(); error.isNotEmpty())
        showMessage (error);
}

void SynthTab::showMessage (const juce::String& text)
{
    messageLabel.setText (text, juce::dontSendNotification);
}

void SynthTab::importDialog()
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    chooser = std::make_unique<juce::FileChooser> ("Import audio", juce::File(), formats.getWildcardForAllFormats());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file.existsAsFile())
            showMessage (processor.getSources().importFile (file));
    });
}

void SynthTab::builtInMenu()
{
    // The morph and the bundled samples (FactorySamples.h), the current one ticked.
    auto& sources = processor.getSources();
    const auto view = sources.getView();
    const bool imported = view != nullptr && ! view->builtIn;
    const auto current = imported ? juce::String ("-") : sources.getFactorySample();

    juce::PopupMenu menu;
    menu.addItem (1, "Built-in morph", true, ! imported && current.isEmpty());
    int id = 2;
    for (const auto& s : factorySamples)
        menu.addItem (id++, s.name, true, current == s.id);

    juce::Component::SafePointer<SynthTab> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&builtInButton), [safe] (int result)
    {
        if (safe == nullptr || result <= 0)
            return;
        const int index = result - 2;
        safe->processor.getSources().setFactorySample (index >= 0 && index < (int) std::size (factorySamples)
                                                           ? juce::String (factorySamples[(size_t) index].id)
                                                           : juce::String());
    });
}

void SynthTab::exportDialog()
{
    const auto suggested = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                               .getChildFile (processor.getSources().getExportName());
    chooser = std::make_unique<juce::FileChooser> ("Export the source audio", suggested, "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File())
            return;
        file = file.withFileExtension ("wav");
        showMessage (processor.getSources().exportOriginal (file) ? juce::String() : "Could not write " + file.getFileName());
    });
}

void SynthTab::showBottom (int index)
{
    bottom = index;
    const bool lower = ! dashboardFull;      // the strips and the bottom band
    belt.setVisible (lower && index == 0);
    dashboard.setVisible (dashboardFull || index == 0);
    modulators.setVisible (lower && index == 1);
    matrix.setVisible (lower && index == 2);
    for (int c = 0; c < 3; ++c)
        chainPanes[(size_t) c]->setVisible (lower && index == 3 + c);
    for (int i = 0; i < numBottomTabs; ++i)
    {
        bottomTabs[(size_t) i].setVisible (lower);
        bottomTabs[(size_t) i].setToggleState (i == index, juce::dontSendNotification);
    }
    modsDice.setVisible (lower && index == 1);
    for (auto& s : strips)
        s->setVisible (lower);
    camsDice.setVisible (lower);
    filtersDice.setVisible (lower);
}

void SynthTab::setDashboardFull (bool full)
{
    dashboardFull = full;
    dashboard.setFullView (full);
    showBottom (bottom);
    resized();
    repaint();
}

//==============================================================================
void SynthTab::paint (juce::Graphics& g)
{
    theme::paintEngraved (g, "Audio", fileHeader);
    if (! dashboardFull)
        theme::paintEngraved (g, "Cams", stripsHeader);
}

void SynthTab::resized()
{
    auto area = getLocalBounds().reduced (8, 6);
    const int h = area.getHeight();

    // File band.
    fileHeader = area.removeFromTop (24);
    {
        auto line = fileHeader.withTrimmedLeft (54);
        importButton.setBounds (line.removeFromLeft (78).reduced (2, 1));
        exportButton.setBounds (line.removeFromLeft (78).reduced (2, 1));
        builtInButton.setBounds (line.removeFromLeft (84).reduced (2, 1));
        line.removeFromLeft (8);
        channelBox.setBounds (line.removeFromLeft (110).reduced (2, 1));
        normaliseButton.setBounds (line.removeFromLeft (96).reduced (2, 1));
        line.removeFromLeft (8);
        messageLabel.setBounds (line.removeFromRight (260));
        fileLabel.setBounds (line);
    }
    waveform.setBounds (area.removeFromTop (juce::jmax (90, h * 16 / 100)));
    area.removeFromTop (6);

    // Scopes and strips, one column per cam.
    auto scopesArea = area.removeFromTop (juce::jmax (90, h * 13 / 100));
    area.removeFromTop (4);
    stripsHeader = area.removeFromTop (20);
    camsDice.setBounds (stripsHeader.getX() + 48, stripsHeader.getY(), 20, 20);
    filtersDice.setBounds (stripsHeader.getX() + 72, stripsHeader.getY(), 20, 20);
    // In full view the dashboard takes everything from here down.
    const auto fullView = stripsHeader.getUnion (area);
    auto stripsArea = area.removeFromTop (juce::jmax (210, h * 28 / 100));
    area.removeFromTop (6);

    const int colW = scopesArea.getWidth() / numCams;
    for (int c = 0; c < numCams; ++c)
    {
        scopes[(size_t) c]->setBounds (scopesArea.removeFromLeft (colW).reduced (3, 0));
        strips[(size_t) c]->setBounds (stripsArea.removeFromLeft (colW).reduced (3, 0));
    }

    // Bottom band.
    bottomHeader = area.removeFromTop (24);
    auto tabsLine = bottomHeader;
    for (auto& t : bottomTabs)
        t.setBounds (tabsLine.removeFromLeft (110).reduced (2, 1));
    modsDice.setBounds (tabsLine.removeFromLeft (26).reduced (1));
    area.removeFromTop (4);
    modulators.setBounds (area);
    matrix.setBounds (area);
    for (auto& pane : chainPanes)
        pane->setBounds (area);
    auto left = area.removeFromLeft (area.getWidth() / 2);
    belt.setBounds (left.withTrimmedRight (3));
    dashboard.setBounds (dashboardFull ? fullView.reduced (3, 0) : area.withTrimmedLeft (3));
}

} // namespace camshaft
