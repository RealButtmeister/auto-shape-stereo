#include "PluginProcessor.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
    std::cout << "PASS " << message << '\n';
}
void set(AutoShapeAudioProcessor& processor, const juce::String& id, float value)
{
    auto* parameter = processor.getValueTreeState().getParameter(id);
    if (!parameter) throw std::runtime_error("Missing parameter");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
bool sameBands(const autoshape::BandState& a, const autoshape::BandState& b)
{
    for (size_t i = 0; i < autoshape::bandCount; ++i)
        if (a.width[i] != b.width[i] || a.generated[i] != b.generated[i]) return false;
    return true;
}
void feed(AutoShapeAudioProcessor& processor, int samples, int blockSize = 257)
{
    juce::AudioBuffer<float> block(2, blockSize);
    juce::MidiBuffer midi;
    juce::Random random(194385);
    for (int offset = 0; offset < samples; offset += blockSize)
    {
        const int count = juce::jmin(blockSize, samples - offset);
        block.setSize(2, count, false, false, true);
        for (int n = 0; n < count; ++n)
        {
            const float sample = (random.nextFloat() * 2 - 1) * 0.13f;
            block.setSample(0, n, sample);
            block.setSample(1, n, sample);
        }
        processor.processBlock(block, midi);
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < count; ++n)
                if (!std::isfinite(block.getSample(c, n))) throw std::runtime_error("Nonfinite plugin audio");
    }
}
void writeWav(const juce::File& file, const juce::AudioBuffer<float>& buffer)
{
    juce::WavAudioFormat format;
    auto fileStream = file.createOutputStream();
    if (!fileStream) throw std::runtime_error("Cannot create WAV");
    fileStream->setPosition(0);
    if (fileStream->truncate().failed()) throw std::runtime_error("Cannot truncate WAV");
    std::unique_ptr<juce::OutputStream> stream = std::move(fileStream);
    auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions()
        .withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));
    if (!writer || !writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples()))
        throw std::runtime_error("Cannot write WAV");
}
void artifacts(const juce::File& folder, AutoShapeAudioProcessor& processor)
{
    folder.createDirectory();
    constexpr int sampleRate = 48000;
    constexpr int length = sampleRate * 8;
    juce::AudioBuffer<float> dry(2, length), wet(2, length);
    juce::Random random(420);
    constexpr double notes[] {261.626, 329.628, 391.995, 493.883, 220.0, 261.626, 329.628, 391.995};
    for (int n = 0; n < length; ++n)
    {
        const double t = n / static_cast<double>(sampleRate);
        const double beat = std::fmod(t, 0.5);
        const double step = std::fmod(t, 0.25);
        const int noteIndex = static_cast<int>(t / 0.25) % 8;
        const double f = notes[noteIndex];
        const double pluck = std::exp(-step * 13) * (std::sin(juce::MathConstants<double>::twoPi * f * t)
            + 0.3 * std::sin(juce::MathConstants<double>::twoPi * f * 2 * t)
            + 0.14 * std::sin(juce::MathConstants<double>::twoPi * f * 5 * t));
        const double kick = std::sin(juce::MathConstants<double>::twoPi * (48 * beat + 8 * (1 - std::exp(-beat * 28)))) * std::exp(-beat * 15);
        const double hat = (random.nextFloat() * 2 - 1) * std::exp(-step * 95);
        const auto sample = static_cast<float>(0.12 * pluck + 0.18 * kick + 0.035 * hat);
        dry.setSample(0, n, sample);
        dry.setSample(1, n, sample);
    }
    // Learn the fixture, then render its held settings from fresh filter history.
    set(processor, "autoShape", 1);
    set(processor, "adaptSeconds", 1);
    set(processor, "output", 0);
    juce::MidiBuffer midi;
    for (int offset = 0; offset < length; offset += 256)
    {
        const int count = juce::jmin(256, length - offset);
        juce::AudioBuffer<float> block(2, count);
        for (int c = 0; c < 2; ++c) block.copyFrom(c, 0, dry, c, offset, count);
        processor.processBlock(block, midi);
    }
    set(processor, "autoShape", 0);
    processor.prepareToPlay(sampleRate, 256);
    for (int offset = 0; offset < length; offset += 256)
    {
        const int count = juce::jmin(256, length - offset);
        juce::AudioBuffer<float> block(2, count);
        for (int c = 0; c < 2; ++c) block.copyFrom(c, 0, dry, c, offset, count);
        processor.processBlock(block, midi);
        for (int c = 0; c < 2; ++c) wet.copyFrom(c, offset, block, c, 0, count);
    }
    writeWav(folder.getChildFile("demo-mono.wav"), dry);
    writeWav(folder.getChildFile("demo-auto-shaped.wav"), wet);
    const auto presetFile = folder.getChildFile("Demo Learned.autoshape");
    check(processor.savePreset(presetFile), "File preset saves successfully");
    AutoShapeAudioProcessor recalled;
    check(recalled.loadPreset(presetFile) && sameBands(recalled.getBandState(), processor.getBandState()),
          "File preset restores the captured coefficients exactly");
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditorIfNeeded());
    editor->setVisible(true);
    auto snapshot = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
    juce::PNGImageFormat png;
    auto stream = folder.getChildFile("Auto-Shape-Stereo.png").createOutputStream();
    if (stream) { stream->setPosition(0); stream->truncate(); }
    check(stream && png.writeImageToStream(snapshot, *stream), "Editor snapshot rendered");
    processor.editorBeingDeleted(editor.get());
}
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialise;
    try
    {
        AutoShapeAudioProcessor processor;
        const auto stereoLayout = processor.getBusesLayout();
        check(processor.isBusesLayoutSupported(stereoLayout), "Stereo input and output supported");
        auto monoInput = stereoLayout;
        monoInput.inputBuses.set(0, juce::AudioChannelSet::mono());
        check(processor.isBusesLayoutSupported(monoInput), "Mono input to stereo output supported");
        auto monoOutput = monoInput;
        monoOutput.outputBuses.set(0, juce::AudioChannelSet::mono());
        check(!processor.isBusesLayoutSupported(monoOutput), "Mono-only output rejected honestly");

        processor.prepareToPlay(48000, 128);
        set(processor, "adaptSeconds", 0.5f);
        const auto original = processor.getBandState();
        set(processor, "autoShape", 1);
        feed(processor, 48000 * 4, 701); // exceeds prepare maximum on purpose
        const auto learned = processor.getBandState();
        check(!sameBands(original, learned), "Learning changes actual processing coefficients");
        set(processor, "autoShape", 0);
        feed(processor, 48000 * 2, 31);
        check(sameBands(learned, processor.getBandState()), "Second click holds every learned coefficient exactly");
        processor.releaseResources();
        processor.prepareToPlay(96000, 512);
        check(sameBands(learned, processor.getBandState()), "Device restart preserves held settings");

        processor.setBandValue(3, false, 1.2345f);
        processor.setBandValue(3, true, 0.4567f);
        processor.setBandValue(4, true, 3.75f);
        juce::MemoryBlock data;
        processor.getStateInformation(data); // save before audio consumes manual edits
        AutoShapeAudioProcessor restored;
        restored.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
        restored.prepareToPlay(48000, 256);
        check(sameBands(processor.getBandState(), restored.getBandState()), "State recall includes pending manual band edits exactly");
        check(!restored.isLearning(), "Project and preset recall opens in Held mode");
        check(restored.getBandState().width[3] == 1.2345f, "Manual learned width is editable and retained");
        check(restored.getBandState().generated[4] == 3.75f, "Extended stereo-generation range survives state recall");
        set(restored, "autoShape", 1);
        restored.getStateInformation(data);
        AutoShapeAudioProcessor activeRecall;
        activeRecall.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
        check(!activeRecall.isLearning(), "Saving during learning captures settings without restarting learning on reload");
        // Older or partial state must not inherit an active Learn switch.
        auto partialXml = juce::AudioProcessor::getXmlFromBinary(data.getData(), static_cast<int>(data.getSize()));
        for (auto* child : partialXml->getChildIterator())
            if (child->getStringAttribute("id") == "autoShape")
            {
                partialXml->removeChildElement(child, true);
                break;
            }
        juce::MemoryBlock partialData;
        juce::AudioProcessor::copyXmlToBinary(*partialXml, partialData);
        set(activeRecall, "autoShape", 1);
        activeRecall.setStateInformation(partialData.getData(), static_cast<int>(partialData.getSize()));
        check(!activeRecall.isLearning(), "Partial state without a Learn parameter still recalls Held");

        set(restored, "autoShape", 0);
        set(restored, "bypass", 1);
        feed(restored, 2048);
        juce::AudioBuffer<float> bypass(2, 257);
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < bypass.getNumSamples(); ++n) bypass.setSample(c, n, (c == 0 ? 0.15f : -0.08f));
        juce::MidiBuffer midi;
        restored.processBlock(bypass, midi);
        check(std::abs(bypass.getSample(0, 128) - 0.15f) < 1.0e-7f
            && std::abs(bypass.getSample(1, 128) + 0.08f) < 1.0e-7f, "Bypass is transparent after its click-free transition");
        check(restored.getBypassParameter() != nullptr, "Host bypass parameter exposed");

        AutoShapeAudioProcessor mono;
        check(mono.setBusesLayout(monoInput), "Host can activate mono-to-stereo layout");
        mono.prepareToPlay(48000, 256);
        set(mono, "output", 0);
        set(mono, "width", 0);
        feed(mono, 24000); // Settle the smoothed Width change and filter tails.
        juce::AudioBuffer<float> monoBlock(2, 256);
        monoBlock.clear();
        for (int n = 0; n < 256; ++n) monoBlock.setSample(0, n, 0.1f);
        mono.processBlock(monoBlock, midi);
        check(std::abs(monoBlock.getSample(0, 255) - monoBlock.getSample(1, 255)) < 1.0e-6f,
              "Mono source copies correctly into stereo output");
        set(restored, "bypass", 0);
        set(restored, "mix", 0);
        restored.prepareToPlay(48000, 256); // Clear the preceding asymmetric DC fixture's meter history.
        feed(restored, 48000);
        bool dryMeterIsMono = true;
        for (auto correlation : restored.getMeters().outputCorrelation)
            dryMeterIsMono = dryMeterIsMono && correlation > 0.999f;
        check(dryMeterIsMono, "Output correlation measures the actual dry/wet result");

        AutoShapeAudioProcessor selectiveMeter;
        set(selectiveMeter, "bypass", 1);
        selectiveMeter.prepareToPlay(48000, 256);
        juce::AudioBuffer<float> spectralBlock(2, 256);
        for (int offset = 0; offset < 48000; offset += 256)
        {
            for (int n = 0; n < 256; ++n)
            {
                const double t = (offset + n) / 48000.0;
                const float low = 0.04f * static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * 50 * t));
                const float high = 0.4f * static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * 4000 * t));
                spectralBlock.setSample(0, n, low + high);
                spectralBlock.setSample(1, n, low - high);
            }
            selectiveMeter.processBlock(spectralBlock, midi);
        }
        const auto selective = selectiveMeter.getMeters();
        check(selective.outputCorrelation[0] > 0.97f && selective.outputCorrelation[5] < -0.97f,
              "Meter distinguishes mono bass from much louder anti-phase highs");
        bool matchingAnalyzers = true;
        for (size_t i = 0; i < autoshape::bandCount; ++i)
            matchingAnalyzers = matchingAnalyzers && std::abs(selective.inputCorrelation[i] - selective.outputCorrelation[i]) < 1.0e-6f;
        check(matchingAnalyzers, "Input and output analyzers agree exactly in bypass");

        if (argc > 1)
        {
            AutoShapeAudioProcessor demo;
            demo.prepareToPlay(48000, 256);
            artifacts(juce::File(juce::String(argv[1])), demo);
        }
        std::cout << "All plugin contracts passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
