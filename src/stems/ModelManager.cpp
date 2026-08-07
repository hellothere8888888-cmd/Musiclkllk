#include "ModelManager.h"
#include <juce_events/juce_events.h>

namespace pablo
{
juce::File ModelManager::getModelDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("PabloSampler").getChildFile ("models");
}

juce::File ModelManager::getUserModelFile()
{
    return getModelDirectory().getChildFile ("user_model.onnx");
}

juce::File ModelManager::getModelFile()
{
    if (getUserModelFile().existsAsFile())
        return getUserModelFile();
    return getModelDirectory().getChildFile (modelFileName);
}

bool ModelManager::isModelPresent()
{
    const auto f = getModelFile();
    // Anything under 10 MB is a failed/partial download, not the model.
    return f.existsAsFile() && f.getSize() > 10 * 1024 * 1024;
}

juce::StringArray ModelManager::getModelUrls()
{
    return {
        "https://huggingface.co/StemSplitio/htdemucs-onnx/resolve/main/htdemucs_fp16weights.onnx",
        "https://huggingface.co/StemSplitio/htdemucs-onnx/resolve/main/htdemucs.onnx"
    };
}

class ModelManager::Listener : public juce::URL::DownloadTaskListener
{
public:
    explicit Listener (ModelManager& o) : owner (o) {}

    void finished (juce::URL::DownloadTask* t, bool success) override
    {
        // Guard against the ModelManager being destroyed before this async runs.
        juce::WeakReference<ModelManager> weak (&owner);
        juce::MessageManager::callAsync ([weak, success, length = t->getTotalLength()]
        {
            auto* owner = weak.get();
            if (owner == nullptr)
                return;
            owner->task.reset();
            if (success && length > 10 * 1024 * 1024)
            {
                if (owner->finishedCb)
                    owner->finishedCb (true, "Model downloaded");
                return;
            }
            owner->getModelDirectory().getChildFile (modelFileName).deleteFile();
            owner->urlIndex++;
            owner->tryNextUrl();
        });
    }

    void progress (juce::URL::DownloadTask*, juce::int64 downloaded, juce::int64 total) override
    {
        if (total <= 0)
            return;
        const float p = (float) downloaded / (float) total;
        juce::WeakReference<ModelManager> weak (&owner);
        juce::MessageManager::callAsync ([weak, p]
        {
            if (auto* owner = weak.get())
                if (owner->progressCb)
                    owner->progressCb (p);
        });
    }

private:
    ModelManager& owner;
};

// Defined here (not in the header) so unique_ptr<Listener>'s destructor sees a
// complete Listener type.
ModelManager::ModelManager() = default;
ModelManager::~ModelManager() = default;

bool ModelManager::startDownload (std::function<void (float)> onProgress,
                                  std::function<void (bool, juce::String)> onFinished)
{
    if (task != nullptr)
        return false;

    progressCb = std::move (onProgress);
    finishedCb = std::move (onFinished);
    urlIndex = 0;
    getModelDirectory().createDirectory();
    tryNextUrl();
    return true;
}

void ModelManager::tryNextUrl()
{
    const auto urls = getModelUrls();
    if (urlIndex >= urls.size())
    {
        if (finishedCb)
            finishedCb (false, "Model download failed. Check your internet connection, "
                               "or place a model file at: " + getUserModelFile().getFullPathName());
        return;
    }

    listener = std::make_unique<Listener> (*this);
    auto options = juce::URL::DownloadTaskOptions().withListener (listener.get());
    task = juce::URL (urls[urlIndex]).downloadToFile (getModelDirectory().getChildFile (modelFileName), options);

    if (task == nullptr)
    {
        urlIndex++;
        tryNextUrl();
    }
}

void ModelManager::cancelDownload()
{
    task.reset();
    getModelDirectory().getChildFile (modelFileName).deleteFile();
    if (finishedCb)
        finishedCb (false, "Download cancelled");
}
} // namespace pablo
