#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace pablo
{
PabloAudioProcessor::PabloAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &session.getUndoManager(), "PARAMS", params::createLayout())
{
    masterGainParam  = apvts.getRawParameterValue (params::masterGain);
    globalPitchParam = apvts.getRawParameterValue (params::globalPitch);
    chokeParam       = apvts.getRawParameterValue (params::choke);
    gateParam        = apvts.getRawParameterValue (params::gateMode);
    baseNoteParam    = apvts.getRawParameterValue (params::baseNote);

    recorder.onRecordingFinished = [this] (std::unique_ptr<juce::AudioBuffer<float>> buffer, double sr)
    {
        session.addTrackFromBuffer ("REC " + juce::String (++recordingCounter), std::move (buffer), sr);
    };
}

PabloAudioProcessor::~PabloAudioProcessor() = default;

void PabloAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    recorder.prepare (sampleRate);
    session.publishNow();
}

bool PabloAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    const auto in = layouts.getMainInputChannelSet();
    return in == juce::AudioChannelSet::stereo()
        || in == juce::AudioChannelSet::mono()
        || in == juce::AudioChannelSet::disabled();
}

void PabloAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    // Tap the input for the recorder before we overwrite the buffer.
    if (getTotalNumInputChannels() > 0)
        recorder.pushBlock (getBusBuffer (buffer, true, 0), numSamples);

    buffer.clear();

    SamplerEngine::Params p;
    p.masterGainDb = masterGainParam->load();
    p.globalPitch  = globalPitchParam->load();
    p.choke        = chokeParam->load() > 0.5f;
    p.gate         = gateParam->load() > 0.5f;
    p.baseNote     = (int) baseNoteParam->load();

    engine.process (buffer, midi, exchange.acquire(), p);
    midi.clear();
}

juce::AudioProcessorEditor* PabloAudioProcessor::createEditor()
{
    return new PabloAudioEditor (*this);
}

void PabloAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("PABLO_STATE");
    state.appendChild (apvts.copyState(), nullptr);
    state.appendChild (session.createSaveTree(), nullptr);

    juce::MemoryOutputStream stream (destData, false);
    state.writeToStream (stream);
}

void PabloAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto state = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (! state.isValid())
        return;

    auto apply = [this, state]
    {
        auto paramsTree = state.getChildWithName (apvts.state.getType());
        if (paramsTree.isValid())
            apvts.replaceState (paramsTree);

        auto sessionTree = state.getChildWithName (id::SESSION);
        if (sessionTree.isValid())
            session.restoreFromSaveTree (sessionTree);
    };

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        apply();
    else
        juce::MessageManager::callAsync (apply);
}
} // namespace pablo

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new pablo::PabloAudioProcessor();
}
