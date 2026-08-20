#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <array>

class S3xtaOTTAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    explicit S3xtaOTTAudioProcessorEditor (S3xtaOTTAudioProcessor&);
    ~S3xtaOTTAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class FamilyLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        void drawRotarySlider (juce::Graphics&, int, int, int, int, float,
                               float, float, juce::Slider&) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool, bool) override;
    };

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    class VerticalMeter final : public juce::Component
    {
    public:
        void setLevel (float newDb);
        void paint (juce::Graphics&) override;

    private:
        float targetDb = -100.0f;
        float displayDb = -100.0f;
    };

    void timerCallback() override;
    void configureKnob (Knob&, const juce::String& label, const juce::String& parameterId,
                        std::function<juce::String (double)> formatter);
    void configureBandButton (juce::ToggleButton&, const juce::String& text);
    void drawBandBar (juce::Graphics&, int index, juce::Rectangle<float> bounds);
    static juce::Rectangle<int> findOpaqueBounds (const juce::Image&);

    S3xtaOTTAudioProcessor& audioProcessor;
    FamilyLookAndFeel familyLookAndFeel;

    Knob timeKnob, amountKnob, mixKnob;
    Knob inKnob, expanderKnob, upwardKnob, downwardKnob, outKnob;
    std::array<Knob, 5> toneKnobs;
    std::array<juce::Slider, 5> nullSliders;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 5> nullAttachments;
    std::array<juce::ToggleButton, 5> soloButtons;
    std::array<juce::ToggleButton, 5> muteButtons;
    std::array<juce::ToggleButton, 5> bypassButtons;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, 5> soloAttachments;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, 5> muteAttachments;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, 5> bypassAttachments;
    std::array<juce::Rectangle<float>, 5> bandBarBounds;

    VerticalMeter inputMeter, outputMeter;
    juce::Image logoImage;
    juce::Rectangle<int> logoSourceBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (S3xtaOTTAudioProcessorEditor)
};
