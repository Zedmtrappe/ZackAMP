#include "FileUtils.h"

namespace af
{
namespace
{
    juce::File& overrideDir()
    {
        static juce::File dir;
        return dir;
    }
} // namespace

void setAppDataDirectoryOverride (const juce::File& dir) { overrideDir() = dir; }

juce::File getAppDataDirectory()
{
    if (overrideDir() != juce::File())
        return overrideDir();
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
#if JUCE_MAC
        .getChildFile ("Application Support")
#endif
        .getChildFile (AMPFORGE_PRODUCT_NAME);
}

juce::File getUserPresetDirectory() { return getAppDataDirectory().getChildFile ("Presets"); }
juce::File getUserIRDirectory() { return getAppDataDirectory().getChildFile ("IR Library"); }

bool writeFileAtomically (const juce::File& target, const juce::String& text, juce::String* error)
{
    if (! target.getParentDirectory().createDirectory())
    {
        if (error != nullptr)
            *error = "Could not create folder " + target.getParentDirectory().getFileName();
        return false;
    }
    juce::TemporaryFile temp (target);
    if (! temp.getFile().replaceWithText (text, false, false, "\n"))
    {
        if (error != nullptr)
            *error = "Could not write the file (disk full or no permission?)";
        return false;
    }
    if (! temp.overwriteTargetFileWithTemporary())
    {
        if (error != nullptr)
            *error = "Could not replace " + target.getFileName();
        return false;
    }
    return true;
}
} // namespace af
