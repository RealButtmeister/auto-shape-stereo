#pragma once

#include "PluginProcessor.h"

class AutoShapeAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    explicit AutoShapeAudioProcessorEditor (AutoShapeAudioProcessor&);
    ~AutoShapeAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Implementation;
    std::unique_ptr<Implementation> ui;
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoShapeAudioProcessorEditor)
};
