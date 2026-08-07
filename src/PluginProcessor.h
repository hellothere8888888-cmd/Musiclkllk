#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "model/SessionState.h"
#include "engine/SamplerEngine.h"
#include "engine/Recorder.h"
#include "engine/ParamIDs.h"
#include "stems/StemSeparator.h"
#include "stems/ModelManager.h"

namespace pablo
{
class PabloAudioProcessor : public juce::AudioProcessor,
                            private juce::Timer
{
public:
    PabloAudioProcessor();
    ~PabloAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "PABLO Sampler"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Declaration order matters: exchange feeds session, and apvts's
    // constructor uses session.getUndoManager(), so both must be constructed
    // first (members initialise in declaration order, not init-list order).
    SnapshotExchange exchange;
    SessionState session { exchange };
    juce::AudioProcessorValueTreeState apvts;
    SamplerEngine engine;
    Recorder recorder;
    StemSeparator separator;
    ModelManager modelManager;

private:
    void timerCallback() override;   // message thread: reclaim retired buffers

    std::atomic<float>* masterGainParam = nullptr;
    std::atomic<float>* globalPitchParam = nullptr;
    std::atomic<float>* chokeParam = nullptr;
    std::atomic<float>* gateParam = nullptr;
    std::atomic<float>* baseNoteParam = nullptr;
    int recordingCounter = 0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (PabloAudioProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PabloAudioProcessor)
};
} // namespace pablo
