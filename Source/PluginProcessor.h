#pragma once
#include <JuceHeader.h>
#include "StereoEngine.h"
#include <array>
#include <atomic>

class AutoShapeAudioProcessor final : public juce::AudioProcessor
{
public:
    AutoShapeAudioProcessor();
    ~AutoShapeAudioProcessor() override = default;
    void prepareToPlay(double sampleRate, int maximumBlockSize) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int sizeInBytes) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorValueTreeState& getValueTreeState() { return parameters; }
    autoshape::BandState getBandState() const;
    void setBandValue(size_t band, bool generated, float value);
    void resetShape();
    autoshape::Meter getMeters() const;
    bool isLearning() const;
    bool savePreset(const juce::File&);
    bool loadPreset(const juce::File&);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void publishEngine();
    void consumeEdits();
    void queueBandState(const autoshape::BandState&);
    bool restoreXml(const juce::XmlElement&);
    void stopLearning();
    void measureAudio(float inputLeft, float inputRight, float outputLeft, float outputRight);
    void publishMeters();
    juce::AudioProcessorValueTreeState parameters;
    autoshape::StereoEngine engine;
    struct PublishedBand
    {
        std::atomic<float> width {1}, generated {0};
        std::atomic<float> pendingWidth {-1}, pendingGenerated {-1};
        std::atomic<float> inputCorrelation {1}, outputCorrelation {1}, energy {0};
    };
    std::array<PublishedBand, autoshape::bandCount> published;
    std::array<std::atomic<float>*, autoshape::bandCount> targets {};
    std::atomic<float> inputPeak {0}, outputPeak {0};
    std::atomic<float>* widthParam = nullptr;
    std::atomic<float>* stereoizeParam = nullptr;
    std::atomic<float>* lowMonoParam = nullptr;
    std::atomic<float>* monoSlopeParam = nullptr;
    std::atomic<float>* transientParam = nullptr;
    std::atomic<float>* centerParam = nullptr;
    std::atomic<float>* adaptParam = nullptr;
    std::atomic<float>* characterParam = nullptr;
    std::atomic<float>* outputParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* learningParam = nullptr;
    juce::AudioBuffer<float> wetBuffer;
    juce::SmoothedValue<float> outputGain, wetMix, bypassMix;
    int scratchSize = 1;
    struct MeterBand
    {
        struct FilterState { double x1 = 0, x2 = 0, y1 = 0, y2 = 0; };
        struct Power
        {
            double ll = 0, rr = 0, lr = 0;
            float correlation() const;
        };
        double b0 = 0, b2 = 0, a1 = 0, a2 = 0;
        std::array<FilterState, 4> filters {};
        std::array<Power, 2> powers {}; // Original input, then final plugin output.
        void prepare(double sampleRate, double frequency);
        void process(const std::array<double, 4>& samples, double smoothing);
    };
    std::array<MeterBand, autoshape::bandCount> meterBands {};
    double meterSmoothing = 0;
    float meterPeakDecay = 0, measuredInputPeak = 0, finalPeak = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutoShapeAudioProcessor)
};
