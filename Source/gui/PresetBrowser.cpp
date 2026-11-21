#include "PresetBrowser.h"

#include "../Presets.h"

namespace
{
void styleEditor (juce::TextEditor& e, bool multiLine)
{
    e.setFont (theme::font (13.0f));
    e.setMultiLine (multiLine, true);
    e.setReturnKeyStartsNewLine (multiLine);
    e.setColour (juce::TextEditor::backgroundColourId, theme::display);
    e.setColour (juce::TextEditor::outlineColourId, theme::outline);
    e.setColour (juce::TextEditor::textColourId, theme::text);
}

void styleLabel (juce::Label& l, float size, bool bold, juce::Colour c)
{
    l.setFont (theme::font (size, bold));
    l.setColour (juce::Label::textColourId, c);
    l.setJustificationType (juce::Justification::centredLeft);
}
} // namespace

PresetBrowser::PresetBrowser (NachhallAudioProcessor& p) : proc (p)
{
    setComponentID ("presetBrowser");
    setWantsKeyboardFocus (true);

    search.setTextToShowWhenEmpty ("Search name, folder, tag...", theme::textDim);
    search.setFont (theme::font (14.0f));
    search.setJustification (juce::Justification::centredLeft);
    search.setIndents (8, 0);
    search.onTextChange = [this] { applyFilter(); };
    search.onEscapeKey = [this] {
        if (onClose)
            onClose();
    };
    search.onReturnKey = [this] { load (list.getSelectedRow() >= 0 ? list.getSelectedRow() : 0, true); };
    addAndMakeVisible (search);

    list.setRowHeight (24);
    list.setColour (juce::ListBox::backgroundColourId, theme::display);
    list.setColour (juce::ListBox::outlineColourId, theme::outline);
    list.setOutlineThickness (1);
    list.setWantsKeyboardFocus (true);
    addAndMakeVisible (list);

    for (auto* b : { &closeButton, &folderButton, &defaultButton })
    {
        b->setColour (juce::TextButton::buttonColourId, theme::panelLight);
        addAndMakeVisible (*b);
    }
    closeButton.onClick = [this] {
        if (onClose)
            onClose();
    };
    folderButton.onClick = [] { NachhallAudioProcessor::getUserPresetFolder().startAsProcess(); };
    defaultButton.setTooltip ("Saves the current settings as \"Default Setting\": loaded automatically when Nachhall starts");
    defaultButton.onClick = [this] {
        proc.saveAsDefaultPreset();
        refresh();
        if (onPresetLoaded)
            onPresetLoaded();
    };

    juce::StringArray filters { "All", "Favourites", "User" };
    for (const auto& pr : factoryPresets())
        filters.addIfNotAlreadyThere (pr.category);
    for (const auto& f : filters)
    {
        auto* b = filterButtons.add (new juce::TextButton (f));
        b->setColour (juce::TextButton::buttonColourId, theme::panelLight);
        b->setColour (juce::TextButton::buttonOnColourId, theme::yellow.withAlpha (0.85f));
        b->onClick = [this, f] { setFilter (f); };
        addAndMakeVisible (b);
    }

    styleLabel (nameLabel, 16.0f, true, theme::text);
    styleLabel (categoryLabel, 12.5f, false, theme::textDim);
    styleLabel (authorTitle, 12.0f, false, theme::textDim);
    styleLabel (tagsTitle, 12.0f, false, theme::textDim);
    styleLabel (descriptionTitle, 12.0f, false, theme::textDim);
    authorTitle.setText ("Author", juce::dontSendNotification);
    tagsTitle.setText ("Tags (comma separated)", juce::dontSendNotification);
    descriptionTitle.setText ("Description", juce::dontSendNotification);
    styleEditor (authorEdit, false);
    styleEditor (tagsEdit, false);
    styleEditor (descriptionEdit, true);
    for (auto* e : { &authorEdit, &tagsEdit, &descriptionEdit })
    {
        e->onFocusLost = [this] { storeDetails(); };
        e->onReturnKey = [this] { storeDetails(); };
    }
    for (juce::Component* c : { (juce::Component*) &nameLabel, (juce::Component*) &categoryLabel, (juce::Component*) &authorTitle,
                                (juce::Component*) &tagsTitle, (juce::Component*) &descriptionTitle, (juce::Component*) &authorEdit,
                                (juce::Component*) &tagsEdit, (juce::Component*) &descriptionEdit })
        addAndMakeVisible (c);

    refresh();
}

PresetBrowser::~PresetBrowser() { list.setModel (nullptr); }

void PresetBrowser::refresh()
{
    favourites = NachhallAudioProcessor::getFavouritePresets();
    all.clear();
    const auto& fp = factoryPresets();
    for (size_t i = 0; i < fp.size(); ++i)
    {
        Entry e { fp[i].name, fp[i].category, "F:" + fp[i].name, (int) i, {}, {} };
        e.info.author = "Nachhall";
        e.info.tags = factoryPresetTags (fp[i]);
        all.push_back (e);
    }
    auto files = NachhallAudioProcessor::getUserPresetFolder().findChildFiles (juce::File::findFiles, false, "*.nhpreset");
    files.sort();
    for (const auto& f : files)
        all.push_back ({ f.getFileNameWithoutExtension(), "User", "U:" + f.getFileName(), -1, f, NachhallAudioProcessor::readPresetInfo (f) });
    applyFilter();
}

void PresetBrowser::setFilter (const juce::String& f)
{
    filter = f;
    applyFilter();
}

void PresetBrowser::applyFilter()
{
    storeDetails();
    detailsRow = -1;
    const auto words = juce::StringArray::fromTokens (search.getText().trim(), true);
    shown.clear();
    for (const auto& e : all)
    {
        if (filter == "Favourites" && ! isFavourite (e))
            continue;
        if (filter != "All" && filter != "Favourites" && e.category != filter)
            continue;
        const auto haystack = e.name + " " + e.category + " " + e.info.tags.joinIntoString (" ") + " " + e.info.description + " " + e.info.author;
        bool match = true;
        for (const auto& w : words)
            if (! haystack.containsIgnoreCase (w))
                match = false;
        if (match)
            shown.push_back (e);
    }
    for (auto* b : filterButtons)
        b->setToggleState (b->getButtonText() == filter, juce::dontSendNotification);
    list.updateContent();
    int current = -1;
    for (size_t i = 0; i < shown.size(); ++i)
        if (shown[i].name == proc.getCurrentPresetName())
            current = (int) i;
    if (current >= 0)
        list.selectRow (current, true, true);
    else
        list.deselectAllRows();
    showDetails (current);
    list.repaint();
    repaint();
}

void PresetBrowser::toggleFavourite (const Entry& e)
{
    if (favourites.contains (e.key))
        favourites.removeString (e.key);
    else
        favourites.add (e.key);
    NachhallAudioProcessor::setFavouritePresets (favourites);
    applyFilter();
}

void PresetBrowser::load (int row, bool closeAfterwards)
{
    if (! juce::isPositiveAndBelow (row, (int) shown.size()))
        return;
    const auto e = shown[(size_t) row];
    if (e.factoryIndex >= 0)
        proc.loadFactoryPreset (e.factoryIndex);
    else
        proc.loadPresetFile (e.file);
    list.selectRow (row, true, true);
    showDetails (row);
    if (onPresetLoaded)
        onPresetLoaded();
    list.repaint();
    if (closeAfterwards && onClose)
        onClose();
}

void PresetBrowser::step (int delta)
{
    if (shown.empty())
        return;
    int row = list.getSelectedRow();
    row = row < 0 ? 0 : (row + delta + (int) shown.size()) % (int) shown.size();
    load (row, false);
}

void PresetBrowser::showDetails (int row)
{
    if (row != detailsRow)
        storeDetails();
    detailsRow = row;
    const bool valid = juce::isPositiveAndBelow (row, (int) shown.size());
    const bool editable = valid && shown[(size_t) row].factoryIndex < 0;
    nameLabel.setText (valid ? shown[(size_t) row].name : juce::String(), juce::dontSendNotification);
    categoryLabel.setText (valid ? (shown[(size_t) row].factoryIndex >= 0 ? "Factory / " : "") + shown[(size_t) row].category : juce::String(),
                           juce::dontSendNotification);
    authorEdit.setText (valid ? shown[(size_t) row].info.author : juce::String(), false);
    tagsEdit.setText (valid ? shown[(size_t) row].info.tags.joinIntoString (", ") : juce::String(), false);
    descriptionEdit.setText (valid ? shown[(size_t) row].info.description : juce::String(), false);
    for (auto* e : { &authorEdit, &tagsEdit, &descriptionEdit })
    {
        e->setReadOnly (! editable);
        e->setColour (juce::TextEditor::textColourId, editable ? theme::text : theme::textDim);
    }
}

void PresetBrowser::storeDetails()
{
    if (! juce::isPositiveAndBelow (detailsRow, (int) shown.size()))
        return;
    auto& e = shown[(size_t) detailsRow];
    if (e.factoryIndex >= 0)
        return;
    PresetInfo info;
    info.author = authorEdit.getText().trim();
    info.description = descriptionEdit.getText().trim();
    info.tags = juce::StringArray::fromTokens (tagsEdit.getText(), ",", "");
    info.tags.trim();
    info.tags.removeEmptyStrings();
    if (info.author == e.info.author && info.description == e.info.description && info.tags == e.info.tags)
        return;
    if (NachhallAudioProcessor::writePresetInfo (e.file, info))
    {
        e.info = info;
        for (auto& a : all)
            if (a.key == e.key)
                a.info = info;
    }
}

int PresetBrowser::getNumRows() { return (int) shown.size(); }

void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) shown.size()))
        return;
    const auto& e = shown[(size_t) row];
    const bool current = e.name == proc.getCurrentPresetName();
    if (selected || current)
        g.fillAll (theme::yellow.withAlpha (current ? 0.22f : 0.12f));

    const bool fav = isFavourite (e);
    g.setColour (fav ? theme::yellow : theme::textDim.withAlpha (0.5f));
    g.setFont (theme::font (16.0f));
    g.drawText (fav ? juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85")) : juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x86")),
                juce::Rectangle<int> (0, 0, 28, height), juce::Justification::centred);

    g.setColour (theme::text);
    g.setFont (theme::font (14.0f, current));
    g.drawText (e.name, juce::Rectangle<int> (32, 0, width - 150, height), juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (theme::font (12.0f));
    g.drawText (e.category, juce::Rectangle<int> (width - 115, 0, 105, height), juce::Justification::centredRight);
}

void PresetBrowser::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (! juce::isPositiveAndBelow (row, (int) shown.size()))
        return;
    if (e.x < 28)
    {
        toggleFavourite (shown[(size_t) row]);
        return;
    }
    load (row, false); // click = load; the browser stays open for auditioning
}

void PresetBrowser::returnKeyPressed (int row) { load (row, true); }

void PresetBrowser::selectedRowsChanged (int lastRowSelected) { showDetails (lastRowSelected); }

bool PresetBrowser::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey)
    {
        if (onClose)
            onClose();
        return true;
    }
    if (k == juce::KeyPress::returnKey)
    {
        load (list.getSelectedRow(), true);
        return true;
    }
    if (k == juce::KeyPress::rightKey)
    {
        load (list.getSelectedRow(), false); // load without closing
        return true;
    }
    if (k.getTextCharacter() == '[' || k.getTextCharacter() == ']')
    {
        step (k.getTextCharacter() == '[' ? -1 : 1);
        return true;
    }
    return false;
}

void PresetBrowser::visibilityChanged()
{
    if (isVisible())
    {
        refresh();
        search.grabKeyboardFocus();
    }
    else
        storeDetails();
}

void PresetBrowser::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (theme::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    g.setColour (theme::yellow);
    g.setFont (theme::font (15.0f, true));
    g.drawText ("PRESETS", juce::Rectangle<int> (14, 8, 100, 26), juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (theme::font (12.0f));
    g.drawText (juce::String ((int) shown.size()) + " presets  -  click = load,  Enter = load + close,  [ ] = previous / next,  star = favourite",
                juce::Rectangle<int> (174, getHeight() - 32, 480, 24), juce::Justification::centredLeft);
}

void PresetBrowser::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    auto top = r.removeFromTop (26);
    top.removeFromLeft (164);
    search.setBounds (top.removeFromLeft (320));

    auto bottom = r.removeFromBottom (28);
    closeButton.setBounds (bottom.removeFromRight (80).reduced (0, 2));
    bottom.removeFromRight (6);
    defaultButton.setBounds (bottom.removeFromRight (124).reduced (0, 2));
    bottom.removeFromRight (6);
    folderButton.setBounds (bottom.removeFromRight (100).reduced (0, 2));

    r.removeFromTop (6);
    r.removeFromBottom (4);
    auto left = r.removeFromLeft (150);
    for (auto* b : filterButtons)
    {
        b->setBounds (left.removeFromTop (22));
        left.removeFromTop (3);
    }
    r.removeFromLeft (8);

    auto details = r.removeFromRight (280);
    r.removeFromRight (8);
    list.setBounds (r);

    details.reduce (6, 2);
    nameLabel.setBounds (details.removeFromTop (24));
    categoryLabel.setBounds (details.removeFromTop (18));
    details.removeFromTop (8);
    authorTitle.setBounds (details.removeFromTop (18));
    authorEdit.setBounds (details.removeFromTop (24));
    details.removeFromTop (6);
    tagsTitle.setBounds (details.removeFromTop (18));
    tagsEdit.setBounds (details.removeFromTop (24));
    details.removeFromTop (6);
    descriptionTitle.setBounds (details.removeFromTop (18));
    descriptionEdit.setBounds (details.removeFromTop (std::min (120, details.getHeight())));
}
