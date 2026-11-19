#include "PresetManager.h"

namespace cortex
{

PresetManager::PresetManager (juce::File folder) : userFolder (std::move (folder))
{
    refresh();
}

juce::File PresetManager::defaultFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Cortex").getChildFile ("Presets");
}

void PresetManager::refresh()
{
    loadFavourites();
    list.clear();

    for (int i = 0; i < numFactoryScenes(); ++i)
    {
        const auto& s = factoryScene (i);
        PresetEntry e;
        e.name = s.name;
        for (const auto& t : s.tags)
            e.tags.add (t);
        e.factory = true;
        e.factoryIndex = i;
        list.push_back (e);
    }

    if (userFolder.isDirectory())
    {
        auto files = userFolder.findChildFiles (juce::File::findFiles, false, "*.cortex");
        files.sort();
        for (const auto& f : files)
        {
            juce::String error;
            const auto preset = presetFromJson (f.loadFileAsString(), error);
            if (! preset)
                continue;   // unreadable files are left alone, not listed

            PresetEntry e;
            e.name = preset->scene.name.empty() ? f.getFileNameWithoutExtension() : juce::String (preset->scene.name);
            for (const auto& t : preset->scene.tags)
                e.tags.add (t);
            e.file = f;
            list.push_back (e);
        }
    }

    for (auto& e : list)
        e.favourite = favourites.contains (e.key());
}

std::vector<int> PresetManager::filter (const juce::String& search, const juce::StringArray& tags, bool favouritesOnly) const
{
    std::vector<int> out;
    const auto needle = search.trim();

    for (int i = 0; i < (int) list.size(); ++i)
    {
        const auto& e = list[(size_t) i];
        if (favouritesOnly && ! e.favourite)
            continue;

        bool hasAll = true;
        for (const auto& t : tags)
            hasAll = hasAll && e.tags.contains (t, true);
        if (! hasAll)
            continue;

        if (needle.isNotEmpty() && ! e.name.containsIgnoreCase (needle)
            && ! e.tags.joinIntoString (" ").containsIgnoreCase (needle))
            continue;

        out.push_back (i);
    }
    return out;
}

juce::StringArray PresetManager::allTags() const
{
    juce::StringArray tags;
    for (const auto& e : list)
        for (const auto& t : e.tags)
            tags.addIfNotAlreadyThere (t, true);
    tags.sortNatural();
    return tags;
}

std::optional<ScenePreset> PresetManager::load (const PresetEntry& e, juce::String& error) const
{
    if (e.factory)
        return factoryPreset (e.factoryIndex);

    if (! e.file.existsAsFile())
    {
        error = e.file.getFileName() + " no longer exists";
        return std::nullopt;
    }
    return presetFromJson (e.file.loadFileAsString(), error);
}

int PresetManager::save (ScenePreset preset, const juce::String& name, juce::String& error)
{
    const auto clean = name.trim();
    if (clean.isEmpty())
    {
        error = "give the preset a name";
        return -1;
    }

    if (! userFolder.createDirectory())
    {
        error = "could not create " + userFolder.getFullPathName();
        return -1;
    }

    preset.scene.name = clean.toStdString();
    const auto file = userFolder.getChildFile (juce::File::createLegalFileName (clean) + ".cortex");
    if (! file.replaceWithText (presetToJson (preset)))
    {
        error = "could not write " + file.getFullPathName();
        return -1;
    }

    refresh();
    for (int i = 0; i < (int) list.size(); ++i)
        if (! list[(size_t) i].factory && list[(size_t) i].file == file)
            return i;
    return -1;
}

bool PresetManager::remove (const PresetEntry& e)
{
    if (e.factory || ! e.file.existsAsFile())
        return false;

    const bool ok = e.file.moveToTrash() || e.file.deleteFile();   // trash first, so it can be recovered
    favourites.removeString (e.key());
    saveFavourites();
    refresh();
    return ok;
}

void PresetManager::setFavourite (const PresetEntry& e, bool on)
{
    if (on) favourites.addIfNotAlreadyThere (e.key());
    else    favourites.removeString (e.key());
    saveFavourites();

    for (auto& entry : list)
        if (entry.key() == e.key())
            entry.favourite = on;
}

void PresetManager::loadFavourites()
{
    favourites.clear();
    const auto file = userFolder.getParentDirectory().getChildFile ("favourites.json");
    const auto parsed = juce::JSON::parse (file.loadFileAsString());   // keep it alive while reading
    if (const auto* items = parsed.getArray())
        for (const auto& v : *items)
            favourites.add (v.toString());
}

void PresetManager::saveFavourites() const
{
    juce::Array<juce::var> items;
    for (const auto& f : favourites)
        items.add (f);

    const auto file = userFolder.getParentDirectory().getChildFile ("favourites.json");
    file.getParentDirectory().createDirectory();
    file.replaceWithText (juce::JSON::toString (items, false));
}

} // namespace cortex
