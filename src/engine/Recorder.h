#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <functional>
#include <vector>

namespace pablo
{
// Captures the plugin's audio input while armed. The audio thread only
// memcpys interleaved samples into a lock-free FIFO; a message-thread timer
// drains it into a growing capture buffer, so the audio thread never
// allocates. stop() hands the finished take to onRecordingFinished.
class Recorder : private juce::Timer
{
public:
    Recorder();

    void prepare (double sampleRate);

    // Audio thread.
    void pushBlock (const juce::AudioBuffer<float>& input, int numSamples);

    // Message thread.
    void start();
    void stop();
    bool isRecording() const { return recording.load(); }
    float getInputPeak() const { return inputPeak.load(); }

    // (buffer, sampleRate), called on the message thread.
    std::function<void (std::unique_ptr<juce::AudioBuffer<float>>, double)> onRecordingFinished;

private:
    void timerCallback() override { drain(); }
    void drain();

    static constexpr int fifoFrames = 1 << 16;   // ~1.5 s of stereo at 44.1k
    juce::AbstractFifo fifo { fifoFrames };
    std::vector<float> fifoData;                 // interleaved stereo

    std::atomic<bool> recording { false };
    std::atomic<float> inputPeak { 0.0f };
    double sampleRate = 44100.0;

    std::vector<float> captureL, captureR;
    static constexpr size_t maxCaptureFrames = 44100u * 60u * 10u;   // 10 min
};
} // namespace pablo
