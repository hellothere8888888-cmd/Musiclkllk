#pragma once
#include "OrtDynamicApi.h"
#include <juce_events/juce_events.h>
#include <atomic>
#include <functional>

namespace pablo
{
// Background job that splits one sample into stems: resample to 44.1 kHz,
// run the model over 7.8 s segments with 25% overlap, triangular-window
// overlap-add, resample back. Progress is polled by the UI; the result is
// delivered on the message thread.
class StemSeparator : private juce::Thread
{
public:
    StemSeparator();
    ~StemSeparator() override;

    struct Result
    {
        bool success = false;
        juce::String message;
        std::vector<std::unique_ptr<juce::AudioBuffer<float>>> stems;
        std::vector<juce::String> stemNames;
        double sampleRate = 44100.0;
    };

    // Returns false if a job is already running. onDone is called on the
    // message thread.
    bool separate (const juce::AudioBuffer<float>& source, double sourceSampleRate,
                   std::function<void (Result)> onDone);

    void cancel();
    bool isRunning() const { return isThreadRunning(); }
    float getProgress() const { return progress.load(); }
    juce::String getStage() const { juce::ScopedLock l (stageLock); return stage; }

    static constexpr double modelSampleRate = 44100.0;
    static constexpr int segmentLength = 343980;         // 7.8 s @ 44.1 kHz (HTDemucs native)
    static constexpr double overlapFraction = 0.25;

private:
    void run() override;
    void setStage (const juce::String& s) { juce::ScopedLock l (stageLock); stage = s; }

    juce::AudioBuffer<float> source;
    double sourceRate = 44100.0;
    std::function<void (Result)> doneCallback;

    std::atomic<float> progress { 0.0f };
    mutable juce::CriticalSection stageLock;
    juce::String stage;
};
} // namespace pablo
