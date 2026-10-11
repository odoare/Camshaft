/*
  ------------------------------------------------------------------------------
    MatrixView.cpp

    See MatrixView.h.

    Author: Olivier Doaré, github.com/odoare
    Dual-licensed, mirroring the JUCE framework it depends on: under the GNU
    AGPL Version 3.0, or under commercial terms available from the author.
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "MatrixView.h"
#include "PluginProcessor.h"
#include "ParamIDs.h"
#include "Theme.h"
#include "Tooltips.h"
#include "Effects/EffectFactory.h"

namespace camshaft
{

using namespace cam;

namespace
{
    constexpr int rowHeight = 24;
}

juce::PopupMenu destinationsMenu (CamshaftAudioProcessor& processor, const juce::String& ticked)
{
    const auto& all = processor.getDestinations().all();
    auto& apvts = processor.getAPVTS();

    // Section -> submenu, in the order sections first appear; effect slots
    // get one submenu per slot listing only the loaded effect.
    std::vector<std::pair<juce::String, juce::PopupMenu>> sections;
    auto sectionFor = [&] (const juce::String& name) -> juce::PopupMenu&
    {
        for (auto& s : sections)
            if (s.first == name)
                return s.second;
        sections.push_back ({ name, {} });
        return sections.back().second;
    };

    std::map<juce::String, juce::PopupMenu> slotMenus;   // "aux1_fx0" -> its parameters
    for (int i = 0; i < (int) all.size(); ++i)
    {
        const auto& e = all[(size_t) i];
        const auto id = e.paramID;
        const bool effect = id.matchesWildcard ("aux?_fx*", true) || id.startsWith ("master_fx");
        if (! effect)
        {
            sectionFor (e.group).addItem (1 + i, e.name, true, id == ticked);
            continue;
        }

        // aux1_fx0_<Effect>_...: keep it only if the slot holds that effect.
        const auto slot = id.upToFirstOccurrenceOf ("_", true, false)
                        + id.fromFirstOccurrenceOf ("_", false, false).upToFirstOccurrenceOf ("_", false, false);
        const auto rest = id.substring (slot.length() + 1);
        const auto effectName = rest.upToFirstOccurrenceOf ("_", false, false);
        const auto* type = apvts.getRawParameterValue (slot + "_type");
        const int t = type != nullptr ? (int) type->load() : 0;
        const auto& types = EffectFactory::types();
        if (t <= 0 || t > (int) types.size() || types[(size_t) t - 1].name != effectName)
            continue;
        auto name = e.name;
        name = name.fromFirstOccurrenceOf (" ", false, false);   // drop the prefix the effects put in their names
        slotMenus[slot].addItem (1 + i, name.isNotEmpty() ? name : id, true, id == ticked);
    }

    for (const auto& [slot, menu] : slotMenus)
    {
        const auto chain = slot.upToFirstOccurrenceOf ("_", false, false);
        const auto slotIndex = slot.fromLastOccurrenceOf ("fx", false, false).getIntValue();
        const juce::String chainName = chain == "aux1" ? "Turbo 1" : chain == "aux2" ? "Turbo 2" : "Exhaust";
        sectionFor (chainName).addSubMenu ("Slot " + juce::String (slotIndex + 1), menu);
    }

    juce::PopupMenu menu;
    for (auto& [name, sub] : sections)
        menu.addSubMenu (name, sub);
    return menu;
}

//==============================================================================
MatrixRow::MatrixRow (CamshaftAudioProcessor& p, int i)
    : processor (p), index (i),
      depth (p, ids::routeDepth (i), "Depth", theme::steel, true)
{
    depth.setBarStyle();
    depth.setTooltip (tips::matrix::depth);
    sourceButton.setTooltip (tips::matrix::source);
    destButton.setTooltip (tips::matrix::destination);
    viaButton.setTooltip (tips::matrix::via);
    clearButton.setTooltip (tips::matrix::clear);

    for (auto* b : { &sourceButton, &destButton, &viaButton, &clearButton })
    {
        b->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    addAndMakeVisible (depth);

    sourceButton.onClick = [this] { chooseSource (false); };
    viaButton.onClick = [this] { chooseSource (true); };
    destButton.onClick = [this] { chooseDestination(); };
    clearButton.onClick = [this] { pendingSource = 0; processor.getStructure().clearRoute (index); refresh(); };

    refresh();
}

void MatrixRow::refresh()
{
    auto& apvts = processor.getAPVTS();
    const auto r = processor.getStructure().getRoute (index);
    used = r.isUsed();

    if (used)
        pendingSource = 0;
    const int shownSource = r.source > 0 ? r.source : pendingSource;
    sourceButton.setButtonText (shownSource > 0 ? sourceName (apvts, shownSource) : juce::String ("Source..."));
    viaButton.setButtonText (r.via > 0 ? "x " + sourceName (apvts, r.via) : juce::String ("via..."));

    juce::String dest ("Destination...");
    if (r.dest.isNotEmpty())
        if (const auto* e = processor.getDestinations().entryFor (processor.getDestinations().flatFor (r.dest)))
            dest = e->group + ": " + e->name;
    destButton.setButtonText (dest);

    depth.setEnabled (used);
    viaButton.setEnabled (used);
    clearButton.setEnabled (used || pendingSource > 0);
    repaint();
}

void MatrixRow::chooseSource (bool via)
{
    auto& apvts = processor.getAPVTS();
    const auto r = processor.getStructure().getRoute (index);
    auto menu = sourcesMenu (apvts, via ? r.via : r.source, via);

    juce::Component::SafePointer<MatrixRow> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (via ? &viaButton : &sourceButton),
                        [safe, via] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        auto& st = safe->processor.getStructure();
        auto route = st.getRoute (safe->index);
        if (via)
            route.via = result - 1;
        else
            route.source = result - 1;
        if (route.dest.isEmpty() && ! via)
        {
            // A source with no destination yet: keep it in the row until one is chosen.
            safe->sourceButton.setButtonText (sourceName (safe->processor.getAPVTS(), route.source));
            safe->pendingSource = route.source;
            return;
        }
        st.setRoute (safe->index, route);
    });
}

void MatrixRow::chooseDestination()
{
    const auto r = processor.getStructure().getRoute (index);
    auto menu = destinationsMenu (processor, r.dest);

    juce::Component::SafePointer<MatrixRow> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&destButton),
                        [safe] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        const auto& all = safe->processor.getDestinations().all();
        if (! juce::isPositiveAndBelow (result - 1, (int) all.size()))
            return;
        auto& st = safe->processor.getStructure();
        auto route = st.getRoute (safe->index);
        route.dest = all[(size_t) result - 1].paramID;
        if (route.source <= 0)
            route.source = safe->pendingSource > 0 ? safe->pendingSource : srcModFirst;
        st.setRoute (safe->index, route);
        if (auto* p = safe->processor.getAPVTS().getParameter (ids::routeDepth (safe->index)))
            if (std::abs (p->getValue() - p->getDefaultValue()) < 1.0e-6f)
                p->setValueNotifyingHost (p->convertTo0to1 (0.3f));
    });
}

void MatrixRow::paint (juce::Graphics& g)
{
    auto b = getLocalBounds();
    g.setColour (used ? theme::panelHi : theme::panel);
    g.fillRect (b.reduced (0, 1));
    g.setColour (used ? theme::text : theme::dimText);
    g.setFont (theme::gaugeFont (11.0f));
    g.drawText (juce::String (index + 1).paddedLeft ('0', 2), b.removeFromLeft (28), juce::Justification::centred);
}

void MatrixRow::resized()
{
    auto b = getLocalBounds().reduced (2, 2);
    b.removeFromLeft (28);
    clearButton.setBounds (b.removeFromRight (22));
    b.removeFromRight (4);
    const int w = b.getWidth();
    sourceButton.setBounds (b.removeFromLeft (w * 22 / 100).reduced (1, 0));
    destButton.setBounds (b.removeFromLeft (w * 34 / 100).reduced (1, 0));
    viaButton.setBounds (b.removeFromLeft (w * 18 / 100).reduced (1, 0));
    depth.setBounds (b.reduced (2, 0));
}

//==============================================================================
MatrixView::MatrixView (CamshaftAudioProcessor& p) : processor (p)
{
    for (int i = 0; i < maxRoutes; ++i)
    {
        auto row = std::make_unique<MatrixRow> (processor, i);
        list.addAndMakeVisible (*row);
        rows.push_back (std::move (row));
    }
    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    processor.getStructure().addChangeListener (this);
    startTimerHz (4);   // source names follow the modulators' types
}

MatrixView::~MatrixView()
{
    processor.getStructure().removeChangeListener (this);
}

void MatrixView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    for (auto& r : rows)
        r->refresh();
}

void MatrixView::timerCallback()
{
    for (auto& r : rows)
        r->refresh();
}

void MatrixView::paint (juce::Graphics& g)
{
    theme::paintPanel (g, getLocalBounds().toFloat(), theme::steel);
    auto header = getLocalBounds().reduced (8, 4).removeFromTop (16);
    header.removeFromLeft (30);
    const int w = header.getWidth() - 26;
    theme::paintEngraved (g, "Source", header.removeFromLeft (w * 22 / 100));
    theme::paintEngraved (g, "Destination", header.removeFromLeft (w * 34 / 100));
    theme::paintEngraved (g, "Via", header.removeFromLeft (w * 18 / 100));
    theme::paintEngraved (g, "Depth", header);
}

void MatrixView::resized()
{
    auto area = getLocalBounds().reduced (6, 4);
    area.removeFromTop (18);
    viewport.setBounds (area);
    const int w = area.getWidth() - viewport.getScrollBarThickness();
    list.setSize (w, rowHeight * (int) rows.size());
    for (int i = 0; i < (int) rows.size(); ++i)
        rows[(size_t) i]->setBounds (0, i * rowHeight, w, rowHeight);
}

} // namespace camshaft
