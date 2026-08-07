#include "Recorder.h"

namespace pablo
{
Recorder::Recorder()
{
    fifoData.resize ((size_t) fifoFrames * 2);
}

void Recorder::prepare (double sr)
{
    sampleRate = sr;
}

void Recorder::pushBlock (const juce::AudioBuffer<float>& input, int numSamples)
{
    if (input.getNumChannels() == 0 || numSamples <= 0)
        return;

    const auto* left = input.getReadPointer (0);
    const auto* right = input.getNumChannels() > 1 ? input.getReadPointer (1) : left;

    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        peak = juce::jmax (peak, std::abs (left[i]), std::abs (right[i]));
    inputPeak.store (peak);

    if (! recording.load (std::memory_order_relaxed))
        return;

    const auto scope = fifo.write (numSamples);
    int n = 0;
    for (int i = 0; i < scope.blockSize1; ++i, ++n)
    {
        fifoData[(size_t) (scope.startIndex1 + i) * 2]     = left[n];
        fifoData[(size_t) (scope.startIndex1 + i) * 2 + 1] = right[n];
    }
    for (int i = 0; i < scope.blockSize2; ++i, ++n)
    {
        fifoData[(size_t) (scope.startIndex2 + i) * 2]     = left[n];
        fifoData[(size_t) (scope.startIndex2 + i) * 2 + 1] = right[n];
    }
}

void Recorder::start()
{
    captureL.clear();
    captureR.clear();
    captureL.reserve (44100 * 30);
    captureR.reserve (44100 * 30);
    recording.store (true);
    startTimerHz (30);
}

void Recorder::stop()
{
    recording.store (false);
    stopTimer();
    drain();

    if (captureL.empty())
        return;

    auto buffer = std::make_unique<juce::AudioBuffer<float>> (2, (int) captureL.size());
    buffer->copyFrom (0, 0, captureL.data(), (int) captureL.size());
    buffer->copyFrom (1, 0, captureR.data(), (int) captureR.size());
    captureL.clear();
    captureR.clear();

    if (onRecordingFinished)
        onRecordingFinished (std::move (buffer), sampleRate);
}

void Recorder::drain()
{
    const auto scope = fifo.read (fifo.getNumReady());
    auto append = [this] (int start, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            if (captureL.size() >= maxCaptureFrames)
                return;
            captureL.push_back (fifoData[(size_t) (start + i) * 2]);
            captureR.push_back (fifoData[(size_t) (start + i) * 2 + 1]);
        }
    };
    append (scope.startIndex1, scope.blockSize1);
    append (scope.startIndex2, scope.blockSize2);
}
} // namespace pablo
