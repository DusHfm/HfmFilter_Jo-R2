// Nachhall - preset browser: search (name, category, tags, description), folders, favourites,
// tags + author + description per preset, keyboard control, Save As Default
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"
#include "Theme.h"

class PresetBrowser : public juce::Component, private juce::ListBoxModel
{
public:
    explicit PresetBrowser (NachhallAudioProcessor& processor);
    ~PresetBrowser() override;

    std::function<void()> onClose;
    std::function<void()> onPresetLoaded;

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override;

private:
    struct Entry
    {
        juce::String name, category, key;
        int factoryIndex = -1;
        juce::File file;
        PresetInfo info;
    };

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override;
    void selectedRowsChanged (int lastRowSelected) override;

    void applyFilter();
    void load (int row, bool closeAfterwards);
    void step (int delta);
    void setFilter (const juce::String& f);
    bool isFavourite (const Entry& e) const { return favourites.contains (e.key); }
    void toggleFavourite (const Entry& e);
    void showDetails (int row);
    void storeDetails();

    NachhallAudioProcessor& proc;
    std::vector<Entry> all, shown;
    juce::StringArray favourites;
    juce::String filter { "All" };
    int detailsRow = -1;

    juce::TextEditor search;
    juce::ListBox list { "Presets", this };
    juce::TextButton closeButton { "Close" }, folderButton { "Open Folder" }, defaultButton { "Save As Default" };
    juce::OwnedArray<juce::TextButton> filterButtons;

    // details panel
    juce::Label nameLabel, categoryLabel, authorTitle, tagsTitle, descriptionTitle;
    juce::TextEditor authorEdit, tagsEdit, descriptionEdit;
};
