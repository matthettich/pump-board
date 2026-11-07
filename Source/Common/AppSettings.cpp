#include "Common/AppSettings.h"

namespace pb
{
namespace settings
{
namespace
{
juce::PropertiesFile::Options options (const juce::File& file)
{
    juce::PropertiesFile::Options o;
    o.applicationName = "pump BOARD";
    o.filenameSuffix = "";
    o.osxLibrarySubFolder = "Application Support";
    o.folderName = file.getParentDirectory().getFullPathName();

    // The file IS the name: "settings" has no suffix, and the caller decides which file.
    if (file.hasFileExtension (".xml"))
        o.filenameSuffix = ".xml";

    return o;
}

// PropertiesFile works on a file name plus a folder, so a caller-supplied file is split up rather than replaced.
std::unique_ptr<juce::PropertiesFile> open (const juce::File& file)
{
    auto o = options (file);
    o.applicationName = file.getFileNameWithoutExtension();
    o.filenameSuffix = file.hasFileExtension (".xml") ? ".xml" : "";

    return std::make_unique<juce::PropertiesFile> (file, o);
}
} // namespace

juce::File appSettingsFile()
{
    const auto folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                            .getChildFile ("pump BOARD");

    folder.createDirectory();
    return folder.getChildFile ("settings");
}

float readTempo (const juce::File& file, float fallback)
{
    if (! file.existsAsFile())
        return fallback;

    auto props = open (file);
    const double stored = props->getDoubleValue ("tempo", (double) fallback);

    // The registry clamps at 20..300 anyway; do it here too so a hand-edited file cannot claim 0 BPM.
    return juce::jlimit (20.0f, 300.0f, (float) stored);
}

bool writeTempo (const juce::File& file, float bpm)
{
    if (bpm <= 0.0f)
        return false;

    file.getParentDirectory().createDirectory();

    auto props = open (file);
    props->setValue ("tempo", (double) juce::jlimit (20.0f, 300.0f, bpm));

    return props->saveIfNeeded();
}

float readTempo()
{
    return readTempo (appSettingsFile());
}

bool writeTempo (float bpm)
{
    return writeTempo (appSettingsFile(), bpm);
}
} // namespace settings
} // namespace pb
