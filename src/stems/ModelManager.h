#pragma once
#include <juce_core/juce_core.h>
#include <functional>

namespace pablo
{
// Locates or downloads the HTDemucs ONNX model. The model is ~170 MB so it is
// never shipped with the plugin; it is fetched once into the user data dir.
class ModelManager
{
public:
    ModelManager();
    ~ModelManager();      // defined in .cpp where Listener is complete

    static juce::File getModelDirectory();
    static juce::File getModelFile();          // preferred (possibly downloaded) model
    static juce::File getUserModelFile();      // user-supplied override, if present
    static bool isModelPresent();

    // Starts an async download. onProgress (0..1) and onFinished (success,
    // message) are called on the message thread. Returns false if a download
    // is already running.
    bool startDownload (std::function<void (float)> onProgress,
                        std::function<void (bool, juce::String)> onFinished);
    void cancelDownload();
    bool isDownloading() const { return task != nullptr; }

    static constexpr auto modelFileName = "htdemucs_fp16.onnx";
    // Primary source + mirror, tried in order.
    static juce::StringArray getModelUrls();

private:
    class Listener;
    std::unique_ptr<juce::URL::DownloadTask> task;
    std::unique_ptr<Listener> listener;
    int urlIndex = 0;

    void tryNextUrl();
    std::function<void (float)> progressCb;
    std::function<void (bool, juce::String)> finishedCb;

    JUCE_DECLARE_WEAK_REFERENCEABLE (ModelManager)
};
} // namespace pablo
