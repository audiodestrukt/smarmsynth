#pragma once
// Finding patch files on disk.
//
// The original's 45 factory patches live inside its own resources rather than
// as files, so they have to be extracted from a copy of the plugin --
// analysis/extract_presets.sh does that. They are the original author's work
// and are not distributed with this project, so the library simply looks in a
// few sensible places and lists whatever it finds.

#include <juce_core/juce_core.h>

namespace swarm {

/** Directories searched for patches, in order. */
inline juce::Array<juce::File> presetSearchPaths()
{
    juce::Array<juce::File> dirs;

    if (auto env = juce::SystemStats::getEnvironmentVariable ("SWARM_PRESETS", {}); env.isNotEmpty())
        dirs.add (juce::File (env));

    // running the standalone straight out of the build tree
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    for (auto up = exe.getParentDirectory(); up.exists() && up != up.getParentDirectory();
         up = up.getParentDirectory())
    {
        auto candidate = up.getChildFile ("presets");
        if (candidate.isDirectory()) { dirs.add (candidate); break; }
    }

    dirs.add (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                  .getChildFile ("SwarmSynth").getChildFile ("presets"));
    dirs.add (juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                  .getChildFile ("SwarmSynth").getChildFile ("presets"));
    return dirs;
}

/** Every patch file found, sorted by name and de-duplicated. */
inline juce::Array<juce::File> findPresets()
{
    juce::Array<juce::File> out;
    juce::StringArray seen;
    for (const auto& dir : presetSearchPaths())
    {
        if (! dir.isDirectory()) continue;
        for (const auto& f : dir.findChildFiles (juce::File::findFiles, true,
                                                 "*.swarmpatch;*.fxp;*.fxb;*.chunk"))
            if (! seen.contains (f.getFileNameWithoutExtension()))
            {
                seen.add (f.getFileNameWithoutExtension());
                out.add (f);
            }
    }
    std::sort (out.begin(), out.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getFileNameWithoutExtension().compareIgnoreCase (b.getFileNameWithoutExtension()) < 0;
    });
    return out;
}

/** "Vowel_Swarm" -> "Vowel Swarm" */
inline juce::String prettyPresetName (const juce::File& f)
{
    return f.getFileNameWithoutExtension().replaceCharacter ('_', ' ');
}

} // namespace swarm
