#include "StemSeparator.h"
#include "ModelManager.h"
#include "../dsp/Resampler.h"

namespace pablo
{
StemSeparator::StemSeparator() : juce::Thread ("PabloStemSeparator") {}

StemSeparator::~StemSeparator()
{
    // The worker checks threadShouldExit() between segments, so a cancel
    // normally returns within one segment. A single ONNX Run() can take a few
    // seconds and cannot be interrupted, so give it a generous window rather
    // than force-killing mid-inference (which would leave ORT in an undefined
    // state). 30 s comfortably covers one CPU segment.
    signalThreadShouldExit();
    stopThread (30000);
}

bool StemSeparator::separate (const juce::AudioBuffer<float>& sourceToUse, double sourceSampleRate,
                              std::function<void (Result)> onDone)
{
    if (isThreadRunning())
        return false;

    source.makeCopyOf (sourceToUse);
    sourceRate = sourceSampleRate;
    doneCallback = std::move (onDone);
    progress.store (0.0f);
    startThread();
    return true;
}

void StemSeparator::cancel()
{
    signalThreadShouldExit();
}

void StemSeparator::run()
{
    Result result;
    result.sampleRate = sourceRate;

    auto finish = [this, &result]
    {
        juce::MessageManager::callAsync ([cb = doneCallback, res = std::make_shared<Result> (std::move (result))]() mutable
        {
            if (cb)
                cb (std::move (*res));
        });
    };

    setStage ("Loading model");
    OrtSession session;
    juce::String error;
    if (! session.load (ModelManager::getModelFile(), error))
    {
        result.message = error;
        finish();
        return;
    }

    // The model wants stereo 44.1 kHz.
    setStage ("Preparing audio");
    juce::AudioBuffer<float> stereo (2, source.getNumSamples());
    stereo.copyFrom (0, 0, source, 0, 0, source.getNumSamples());
    stereo.copyFrom (1, 0, source, source.getNumChannels() > 1 ? 1 : 0, 0, source.getNumSamples());

    auto work = resampleBuffer (stereo, sourceRate, modelSampleRate);
    const int totalSamples = work.getNumSamples();

    const int hop = (int) (segmentLength * (1.0 - overlapFraction));
    const int numSegments = juce::jmax (1, (totalSamples + hop - 1) / hop);

    int numStems = 0;
    std::vector<juce::AudioBuffer<float>> accum;      // one per stem
    std::vector<float> weight ((size_t) totalSamples, 0.0f);

    // Triangular cross-fade window over the whole segment.
    std::vector<float> window ((size_t) segmentLength);
    for (int i = 0; i < segmentLength; ++i)
    {
        const float x = (float) i / (float) (segmentLength - 1);
        window[(size_t) i] = 1.0f - std::abs (2.0f * x - 1.0f) * 0.999f;   // never exactly 0
    }

    juce::AudioBuffer<float> segment (2, segmentLength);

    setStage ("Separating stems");
    for (int seg = 0; seg < numSegments; ++seg)
    {
        if (threadShouldExit())
        {
            result.message = "Cancelled";
            finish();
            return;
        }

        const int segStart = seg * hop;
        const int available = juce::jmin (segmentLength, totalSamples - segStart);
        if (available <= 0)
            break;

        segment.clear();
        for (int ch = 0; ch < 2; ++ch)
            segment.copyFrom (ch, 0, work, ch, segStart, available);

        std::vector<juce::AudioBuffer<float>> stems;
        if (! session.run (segment, stems, error))
        {
            result.message = error;
            finish();
            return;
        }

        if (numStems == 0)
        {
            numStems = (int) stems.size();
            for (int s = 0; s < numStems; ++s)
                accum.emplace_back (2, totalSamples);
            for (auto& a : accum)
                a.clear();
        }

        for (int s = 0; s < juce::jmin (numStems, (int) stems.size()); ++s)
        {
            const auto& stem = stems[(size_t) s];
            const int copyLen = juce::jmin (available, stem.getNumSamples());
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* dst = accum[(size_t) s].getWritePointer (ch);
                const auto* src = stem.getReadPointer (juce::jmin (ch, stem.getNumChannels() - 1));
                for (int i = 0; i < copyLen; ++i)
                    dst[segStart + i] += src[i] * window[(size_t) i];
            }
        }

        for (int i = 0; i < available; ++i)
            weight[(size_t) (segStart + i)] += window[(size_t) i];

        progress.store (0.05f + 0.9f * (float) (seg + 1) / (float) numSegments);
    }

    if (numStems == 0)
    {
        result.message = "No audio was processed";
        finish();
        return;
    }

    setStage ("Finishing");
    for (auto& a : accum)
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = a.getWritePointer (ch);
            for (int i = 0; i < totalSamples; ++i)
                d[i] /= juce::jmax (1.0e-6f, weight[(size_t) i]);
        }

    static const char* fourStemNames[] = { "drums", "bass", "other", "vocals" };
    static const char* sixStemNames[]  = { "drums", "bass", "other", "vocals", "guitar", "piano" };

    for (int s = 0; s < numStems; ++s)
    {
        if (threadShouldExit())
        {
            result.message = "Cancelled";
            finish();
            return;
        }

        auto back = resampleBuffer (accum[(size_t) s], modelSampleRate, sourceRate);
        result.stems.push_back (std::make_unique<juce::AudioBuffer<float>> (std::move (back)));

        if (numStems == 6 && s < 6)      result.stemNames.push_back (sixStemNames[s]);
        else if (s < 4)                  result.stemNames.push_back (fourStemNames[s]);
        else                             result.stemNames.push_back ("stem " + juce::String (s + 1));
    }

    progress.store (1.0f);
    result.success = true;
    finish();
}
} // namespace pablo
