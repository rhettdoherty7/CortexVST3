#pragma once

#include "PresetFormat.h"

#include <vector>

namespace cortex
{

/** One row in the preset browser. */
struct PresetEntry
{
    juce::String name;
    juce::StringArray tags;
    bool factory = false;
    int factoryIndex = -1;     // factory presets: index into factoryScene()
    juce::File file;           // user presets
    bool favourite = false;

    juce::String key() const   { return factory ? "factory:" + name : "user:" + file.getFileName(); }
};

/** Factory scenes plus the user's `.cortex` files (section 11), with favourites. Message thread only.
    User presets live in Documents/Cortex/Presets; favourites in Documents/Cortex/favourites.json. */
class PresetManager
{
public:
    explicit PresetManager (juce::File userFolder = defaultFolder());

    static juce::File defaultFolder();

    void refresh();
    const std::vector<PresetEntry>& entries() const noexcept   { return list; }

    /** Indices into entries(): name or tag contains `search`, has every tag in `tags`. */
    std::vector<int> filter (const juce::String& search, const juce::StringArray& tags, bool favouritesOnly) const;
    juce::StringArray allTags() const;

    std::optional<ScenePreset> load (const PresetEntry&, juce::String& error) const;

    /** Saves under `name` (replacing a user preset of the same name). Returns the new entry's index. */
    int save (ScenePreset, const juce::String& name, juce::String& error);
    bool remove (const PresetEntry&);
    void setFavourite (const PresetEntry&, bool);

    const juce::File& folder() const noexcept   { return userFolder; }

private:
    void loadFavourites();
    void saveFavourites() const;

    juce::File userFolder;
    std::vector<PresetEntry> list;
    juce::StringArray favourites;
};

} // namespace cortex
