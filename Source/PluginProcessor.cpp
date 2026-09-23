#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <limits>

namespace
{
constexpr std::array<float, 8> defaultTargets {0.995f, 0.98f, 0.90f, 0.72f, 0.48f, 0.26f, 0.31f, 0.38f};
float safeValue(float value, float low, float high, float fallback)
{
    return std::isfinite(value) ? juce::jlimit(low, high, value) : fallback;
}
}

AutoShapeAudioProcessor::AutoShapeAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "AutoShapeStereo", createParameterLayout())
{
    widthParam = parameters.getRawParameterValue("width");
    stereoizeParam = parameters.getRawParameterValue("stereoize");
    lowMonoParam = parameters.getRawParameterValue("lowMono");
    monoSlopeParam = parameters.getRawParameterValue("monoSlope");
    transientParam = parameters.getRawParameterValue("transient");
    centerParam = parameters.getRawParameterValue("center");
    adaptParam = parameters.getRawParameterValue("adaptSeconds");
    characterParam = parameters.getRawParameterValue("character");
    outputParam = parameters.getRawParameterValue("output");
    mixParam = parameters.getRawParameterValue("mix");
    bypassParam = parameters.getRawParameterValue("bypass");
    learningParam = parameters.getRawParameterValue("autoShape");
    for (size_t i = 0; i < autoshape::bandCount; ++i)
        targets[i] = parameters.getRawParameterValue("target" + juce::String(static_cast<int>(i)));
    publishEngine();
}

juce::AudioProcessorValueTreeState::ParameterLayout AutoShapeAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto add = [&layout](const char* id, const char* name, float low, float high, float initial, float skew = 1.0f)
    {
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(id, 1), name,
            juce::NormalisableRange<float>(low, high, 0.0001f, skew), initial));
    };
    add("width", "Width", 0, 3, 1);
    add("stereoize", "Stereoize", 0, 1, 0.65f);
    add("lowMono", "Low-end Mono", 0, 1000, 180, 0.5f);
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("monoSlope", 1), "Mono Slope",
        juce::StringArray {"12 dB/oct", "24 dB/oct", "48 dB/oct"}, 1));
    add("transient", "Transient Protection", 0, 1, 0.6f);
    add("center", "Center Preservation", 0, 1, 1);
    add("adaptSeconds", "Adapt Speed", 0.25f, 15, 3, 0.55f);
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("character", 1), "Character",
        juce::StringArray {"Tight", "Natural", "Diffuse"}, 1));
    add("output", "Output", -24, 12, -3);
    add("mix", "Mix", 0, 1, 1);
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID("bypass", 1), "Bypass", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID("autoShape", 1), "Auto Shape", false));
    for (size_t i = 0; i < autoshape::bandCount; ++i)
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("target" + juce::String(static_cast<int>(i)), 1),
            "Target " + juce::String(static_cast<int>(autoshape::bandFrequencies[i])) + " Hz",
            juce::NormalisableRange<float>(0.05f, 1.0f, 0.0001f), defaultTargets[i]));
    return layout;
}

bool AutoShapeAudioProcessor::isBusesLayoutSupported(const BusesLayout& layout) const
{
    const auto in = layout.getMainInputChannelSet();
    const auto out = layout.getMainOutputChannelSet();
    // Stereo output is required for generating an image from a mono source.
    return (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo())
        && out == juce::AudioChannelSet::stereo();
}

void AutoShapeAudioProcessor::prepareToPlay(double sampleRate, int maximumBlockSize)
{
    scratchSize = juce::jmax(1, maximumBlockSize);
    wetBuffer.setSize(2, scratchSize);
    engine.prepare(sampleRate);
    consumeEdits();
    outputGain.reset(sampleRate, 0.025);
    outputGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(outputParam->load()));
    wetMix.reset(sampleRate, 0.025);
    wetMix.setCurrentAndTargetValue(mixParam->load());
    bypassMix.reset(sampleRate, 0.01);
    bypassMix.setCurrentAndTargetValue(bypassParam->load() >= 0.5f ? 0.0f : 1.0f);
    publishEngine();
    measuredInputPeak = finalPeak = 0;
    const double meterRate = std::isfinite(sampleRate) ? std::clamp(sampleRate, 8000.0, 768000.0) : 48000.0;
    meterSmoothing = -std::expm1(-1.0 / (0.15 * meterRate));
    meterPeakDecay = static_cast<float>(std::exp(-1.0 / (0.5 * meterRate)));
    for (size_t i = 0; i < meterBands.size(); ++i)
        meterBands[i].prepare(meterRate, autoshape::bandFrequencies[i]);
    publishMeters();
    setLatencySamples(0);
}

void AutoShapeAudioProcessor::consumeEdits()
{
    auto state = engine.getBandState();
    bool edited = false;
    for (size_t i = 0; i < autoshape::bandCount; ++i)
    {
        const auto w = published[i].pendingWidth.exchange(-1.0f);
        const auto g = published[i].pendingGenerated.exchange(-1.0f);
        if (w >= 0) { state.width[i] = w; edited = true; }
        if (g >= 0) { state.generated[i] = g; edited = true; }
    }
    if (edited) engine.setBandState(state);
}

void AutoShapeAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() < 2 || buffer.getNumSamples() == 0) return;
    if (getTotalNumInputChannels() == 1)
        buffer.copyFrom(1, 0, buffer, 0, 0, buffer.getNumSamples());
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear(c, 0, buffer.getNumSamples());
    consumeEdits();
    autoshape::Settings settings;
    settings.width = widthParam->load();
    settings.stereoize = stereoizeParam->load();
    settings.lowMonoHz = lowMonoParam->load();
    constexpr float slopes[] {12, 24, 48};
    settings.monoSlope = slopes[juce::jlimit(0, 2, static_cast<int>(monoSlopeParam->load()))];
    settings.transientProtection = transientParam->load();
    settings.centerPreservation = centerParam->load();
    settings.adaptSeconds = adaptParam->load();
    settings.character = static_cast<int>(characterParam->load());
    settings.learning = isLearning() && bypassParam->load() < 0.5f;
    for (size_t i = 0; i < autoshape::bandCount; ++i) settings.targetCorrelation[i] = targets[i]->load();
    outputGain.setTargetValue(juce::Decibels::decibelsToGain(outputParam->load()));
    wetMix.setTargetValue(mixParam->load());
    bypassMix.setTargetValue(bypassParam->load() >= 0.5f ? 0.0f : 1.0f);

    // A host may exceed its advertised block size. Chunk through preallocated scratch.
    for (int offset = 0; offset < buffer.getNumSamples(); offset += scratchSize)
    {
        const auto count = juce::jmin(scratchSize, buffer.getNumSamples() - offset);
        for (int c = 0; c < 2; ++c) wetBuffer.copyFrom(c, 0, buffer, c, offset, count);
        engine.process(wetBuffer.getWritePointer(0), wetBuffer.getWritePointer(1), count, settings);
        for (int n = 0; n < count; ++n)
        {
            const auto inputLeft = buffer.getSample(0, offset + n);
            const auto inputRight = buffer.getSample(1, offset + n);
            const auto mix = wetMix.getNextValue();
            const auto gain = outputGain.getNextValue();
            const auto active = bypassMix.getNextValue();
            for (int c = 0; c < 2; ++c)
            {
                const auto dry = buffer.getSample(c, offset + n);
                const auto processed = (dry + mix * (wetBuffer.getSample(c, n) - dry)) * gain;
                const auto result = dry + active * (processed - dry);
                buffer.setSample(c, offset + n, std::isfinite(result) ? result : 0.0f);
            }
            measureAudio(inputLeft, inputRight, buffer.getSample(0, offset + n), buffer.getSample(1, offset + n));
        }
    }
    publishEngine();
    publishMeters();
}

float AutoShapeAudioProcessor::MeterBand::Power::correlation() const
{
    const double total = ll + rr;
    const double denominator = std::sqrt(std::max(0.0, ll * rr));
    return total < 1.0e-14 ? 1.0f : denominator < total * 1.0e-5 ? 0.0f
        : static_cast<float>(std::clamp(lr / denominator, -1.0, 1.0));
}

void AutoShapeAudioProcessor::MeterBand::prepare(double sampleRate, double frequency)
{
    *this = {};
    // Constant-peak-gain RBJ bandpass, Q=1.4. Both traces use precisely the
    // same analysis response, independent of the DSP's overlapping work bands.
    const double omega = juce::MathConstants<double>::twoPi * std::min(frequency, sampleRate * 0.45) / sampleRate;
    const double alpha = std::sin(omega) / (2.0 * 1.4);
    const double a0 = 1.0 + alpha;
    b0 = alpha / a0;
    b2 = -b0;
    a1 = -2.0 * std::cos(omega) / a0;
    a2 = (1.0 - alpha) / a0;
}

void AutoShapeAudioProcessor::MeterBand::process(const std::array<double, 4>& samples, double smoothing)
{
    std::array<double, 4> filtered {};
    for (size_t channel = 0; channel < filters.size(); ++channel)
    {
        auto& state = filters[channel];
        const double sample = samples[channel];
        const double output = b0 * sample + b2 * state.x2 - a1 * state.y1 - a2 * state.y2;
        state.x2 = state.x1;
        state.x1 = sample;
        state.y2 = state.y1;
        state.y1 = output;
        filtered[channel] = output;
    }
    for (size_t trace = 0; trace < powers.size(); ++trace)
    {
        auto& power = powers[trace];
        const double left = filtered[trace * 2], right = filtered[trace * 2 + 1];
        power.ll += smoothing * (left * left - power.ll);
        power.rr += smoothing * (right * right - power.rr);
        power.lr += smoothing * (left * right - power.lr);
    }
}

void AutoShapeAudioProcessor::measureAudio(float inputLeft, float inputRight, float outputLeft, float outputRight)
{
    // Invalid host samples must not poison recursive meter history. Double
    // states/powers also preserve finite metering of very large finite input.
    const std::array<double, 4> samples {
        std::isfinite(inputLeft) ? static_cast<double>(inputLeft) : 0.0,
        std::isfinite(inputRight) ? static_cast<double>(inputRight) : 0.0,
        std::isfinite(outputLeft) ? static_cast<double>(outputLeft) : 0.0,
        std::isfinite(outputRight) ? static_cast<double>(outputRight) : 0.0 };
    measuredInputPeak = std::max({static_cast<float>(std::abs(samples[0])),
        static_cast<float>(std::abs(samples[1])), measuredInputPeak * meterPeakDecay});
    finalPeak = std::max({static_cast<float>(std::abs(samples[2])),
        static_cast<float>(std::abs(samples[3])), finalPeak * meterPeakDecay});
    for (auto& band : meterBands)
        band.process(samples, meterSmoothing);
}

void AutoShapeAudioProcessor::publishMeters()
{
    for (size_t i = 0; i < meterBands.size(); ++i)
    {
        const auto& input = meterBands[i].powers[0];
        published[i].inputCorrelation.store(input.correlation(), std::memory_order_relaxed);
        published[i].outputCorrelation.store(meterBands[i].powers[1].correlation(), std::memory_order_relaxed);
        const double rms = std::sqrt(std::max(0.0, 0.5 * (input.ll + input.rr)));
        published[i].energy.store(static_cast<float>(std::min(rms,
            static_cast<double>(std::numeric_limits<float>::max()))), std::memory_order_relaxed);
    }
    inputPeak.store(measuredInputPeak, std::memory_order_relaxed);
    outputPeak.store(finalPeak, std::memory_order_relaxed);
}

void AutoShapeAudioProcessor::publishEngine()
{
    const auto bands = engine.getBandState();
    for (size_t i = 0; i < autoshape::bandCount; ++i)
    {
        published[i].width.store(bands.width[i], std::memory_order_relaxed);
        published[i].generated.store(bands.generated[i], std::memory_order_relaxed);
    }
}

autoshape::BandState AutoShapeAudioProcessor::getBandState() const
{
    autoshape::BandState result;
    for (size_t i = 0; i < autoshape::bandCount; ++i)
    {
        const auto pendingW = published[i].pendingWidth.load();
        const auto pendingG = published[i].pendingGenerated.load();
        result.width[i] = pendingW >= 0 ? pendingW : published[i].width.load();
        result.generated[i] = pendingG >= 0 ? pendingG : published[i].generated.load();
    }
    return result;
}

autoshape::Meter AutoShapeAudioProcessor::getMeters() const
{
    autoshape::Meter result;
    for (size_t i = 0; i < autoshape::bandCount; ++i)
    {
        result.inputCorrelation[i] = published[i].inputCorrelation.load(std::memory_order_relaxed);
        result.outputCorrelation[i] = published[i].outputCorrelation.load(std::memory_order_relaxed);
        result.energy[i] = published[i].energy.load(std::memory_order_relaxed);
    }
    result.inputPeak = inputPeak.load(std::memory_order_relaxed);
    result.outputPeak = outputPeak.load(std::memory_order_relaxed);
    return result;
}

bool AutoShapeAudioProcessor::isLearning() const { return learningParam->load() >= 0.5f; }

void AutoShapeAudioProcessor::stopLearning()
{
    if (auto* p = parameters.getParameter("autoShape"))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(0.0f);
        p->endChangeGesture();
    }
}

void AutoShapeAudioProcessor::setBandValue(size_t band, bool generated, float value)
{
    if (band >= autoshape::bandCount) return;
    stopLearning();
    if (generated) published[band].pendingGenerated.store(safeValue(value, 0, 4, 0));
    else published[band].pendingWidth.store(safeValue(value, 0, 3, 1));
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
}

void AutoShapeAudioProcessor::queueBandState(const autoshape::BandState& bands)
{
    for (size_t i = 0; i < autoshape::bandCount; ++i)
    {
        published[i].pendingWidth.store(safeValue(bands.width[i], 0, 3, 1));
        published[i].pendingGenerated.store(safeValue(bands.generated[i], 0, 4, 0));
    }
}

void AutoShapeAudioProcessor::resetShape()
{
    stopLearning();
    autoshape::StereoEngine fresh;
    queueBandState(fresh.getBandState());
    for (size_t i = 0; i < autoshape::bandCount; ++i)
        if (auto* p = parameters.getParameter("target" + juce::String(static_cast<int>(i))))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(defaultTargets[i]));
            p->endChangeGesture();
        }
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
}

juce::AudioProcessorParameter* AutoShapeAudioProcessor::getBypassParameter() const
{
    return parameters.getParameter("bypass");
}

void AutoShapeAudioProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    juce::ValueTree state;
    autoshape::BandState bands;
    {
        // JUCE's host wrapper uses this lock for processBlock. Snapshot under it
        // so a pending edit cannot disappear between consumption and publication.
        const juce::ScopedLock lock(getCallbackLock());
        state = parameters.copyState();
        bands = getBandState();
    }
    auto xml = state.createXml();
    xml->setAttribute("schemaVersion", 1);
    // Reload a learned sound in Held mode, even if it was saved during learning.
    for (auto* child : xml->getChildIterator())
        if (child->getStringAttribute("id") == "autoShape") child->setAttribute("value", 0.0);
    auto* learned = xml->createNewChildElement("LearnedBands");
    for (size_t i = 0; i < autoshape::bandCount; ++i)
    {
        auto* band = learned->createNewChildElement("Band");
        band->setAttribute("index", static_cast<int>(i));
        band->setAttribute("width", static_cast<double>(bands.width[i]));
        band->setAttribute("generated", static_cast<double>(bands.generated[i]));
    }
    copyXmlToBinary(*xml, destination);
}

bool AutoShapeAudioProcessor::restoreXml(const juce::XmlElement& xml)
{
    if (!xml.hasTagName("AutoShapeStereo") || xml.getIntAttribute("schemaVersion", 1) != 1) return false;
    auto valueTree = juce::ValueTree::fromXml(xml);
    const auto learned = valueTree.getChildWithName("LearnedBands");
    autoshape::StereoEngine fresh;
    auto bands = fresh.getBandState();
    for (const auto& band : learned)
    {
        const int index = band.getProperty("index", -1);
        if (index < 0 || index >= static_cast<int>(autoshape::bandCount)) continue;
        bands.width[static_cast<size_t>(index)] = safeValue(static_cast<float>(band.getProperty("width", 1.0)), 0, 3, 1);
        bands.generated[static_cast<size_t>(index)] = safeValue(static_cast<float>(band.getProperty("generated", 0.0)), 0, 4, 0);
    }
    valueTree.removeChild(learned, nullptr);
    auto learningChild = valueTree.getChildWithProperty("id", "autoShape");
    if (!learningChild.isValid())
    {
        learningChild = juce::ValueTree("PARAM");
        learningChild.setProperty("id", "autoShape", nullptr);
        valueTree.appendChild(learningChild, nullptr);
    }
    learningChild.setProperty("value", 0.0, nullptr);
    for (auto child : valueTree)
    {
        const auto id = child.getProperty("id").toString();
        if (id == "autoShape") child.setProperty("value", 0.0, nullptr);
        else if (auto* parameter = parameters.getParameter(id))
        {
            const float raw = static_cast<float>(child.getProperty("value"));
            const auto& range = parameter->getNormalisableRange();
            child.setProperty("value", safeValue(raw, range.start, range.end,
                parameter->convertFrom0to1(parameter->getDefaultValue())), nullptr);
        }
    }
    // Full state recall is serialized against processing by JUCE's callback lock.
    // processBlock itself does no locking, allocation, file I/O, or host notifications.
    const juce::ScopedLock lock(getCallbackLock());
    parameters.replaceState(valueTree);
    queueBandState(bands);
    return true;
}

void AutoShapeAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes)) restoreXml(*xml);
}

bool AutoShapeAudioProcessor::savePreset(const juce::File& file)
{
    juce::MemoryBlock block;
    getStateInformation(block);
    return file.replaceWithData(block.getData(), block.getSize());
}

bool AutoShapeAudioProcessor::loadPreset(const juce::File& file)
{
    juce::MemoryBlock block;
    if (!file.loadFileAsData(block)) return false;
    if (auto xml = getXmlFromBinary(block.getData(), static_cast<int>(block.getSize())))
    {
        if (!restoreXml(*xml)) return false;
        updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
        return true;
    }
    return false;
}

juce::AudioProcessorEditor* AutoShapeAudioProcessor::createEditor()
{
    return new AutoShapeAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AutoShapeAudioProcessor(); }
