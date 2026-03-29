/*
  ==============================================================================

    JUCE plugin editor for Multiband Up/Down Compressor (OTT-style)

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <array>
#include <functional>

//==============================================================================
class S3xtaOTTAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                      private juce::Timer
{
public:
    S3xtaOTTAudioProcessorEditor (S3xtaOTTAudioProcessor&);
    ~S3xtaOTTAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class OttLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
                               const float rotaryStartAngle, const float rotaryEndAngle, juce::Slider& slider) override;
        void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    };

    class Divider : public juce::Component
    {
    public:
        void paint (juce::Graphics& g) override
        {
            g.setColour (juce::Colour (0xff2b2b2b));
            auto r = getLocalBounds().toFloat();
            g.drawLine (r.getCentreX(), r.getY(), r.getCentreX(), r.getBottom(), 1.0f);
        }
    };

    struct ParamKnob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    class BipolarGRMeterComponent : public juce::Component
    {
    public:
        BipolarGRMeterComponent();
        void setRange (float maxUpDbIn = 18.0f, float maxDownDbAbsIn = 24.0f);
        void setValuesDb (float upDb, float downDb);
        void paint (juce::Graphics& g) override;

    private:
        float maxUpDb = 18.0f;
        float maxDownDbAbs = 24.0f;
        float smoothUp = 0.0f;
        float smoothDown = 0.0f;
    };

    class StereoOutputMeterComponent : public juce::Component
    {
    public:
        StereoOutputMeterComponent();
        void setLevelsDb (float leftDb, float rightDb, bool clipLatch);
        std::function<void()> onClipClicked;
        void paint (juce::Graphics& g) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        float smoothL = -100.0f;
        float smoothR = -100.0f;
        bool clip = false;
        juce::Rectangle<int> clipBounds;
    };

    class AnalyzerComponent : public juce::Component
    {
    public:
        AnalyzerComponent (S3xtaOTTAudioProcessor& proc);
        void setPost (bool isPost) { post = isPost; }
        void updateData();
        void paint (juce::Graphics& g) override;

    private:
        S3xtaOTTAudioProcessor& processor;
        bool post = false;
        std::array<float, FFTDataGenerator::fftSize / 2> fftData {};
        bool hasData = false;
    };

    void timerCallback() override;
    void applyAdvancedLayout();
    void layoutAdvancedPanel (juce::Rectangle<int> area);

    S3xtaOTTAudioProcessor& audioProcessor;
    OttLookAndFeel lookAndFeel;

    ParamKnob amountKnob;
    ParamKnob timeKnob;
    ParamKnob mixKnob;
    ParamKnob outKnob;

    ParamKnob f1Knob;
    ParamKnob f2Knob;
    ParamKnob linkKnob;
    ParamKnob attackKnob;
    ParamKnob releaseKnob;
    ParamKnob lowGainKnob;
    ParamKnob midGainKnob;
    ParamKnob highGainKnob;

    juce::ToggleButton soloLow;
    juce::ToggleButton soloMid;
    juce::ToggleButton soloHigh;

    juce::TextButton preButton { "PRE" };

    juce::Label lowLabel;
    juce::Label midLabel;
    juce::Label highLabel;
    juce::Label outLabel;
    juce::Label titleLabel;

    Divider topDivider1;
    Divider topDivider2;
    juce::Component leftColumn;
    juce::Component centerColumn;
    juce::Component rightColumn;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloLowAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloMidAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloHighAttach;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outAttach;

    BipolarGRMeterComponent grLow;
    BipolarGRMeterComponent grMid;
    BipolarGRMeterComponent grHigh;
    StereoOutputMeterComponent outMeter;
    AnalyzerComponent analyzer;

    juce::Component advancedPanel;

    bool advancedVisible = false;
    bool analyzerPostMode = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (S3xtaOTTAudioProcessorEditor)
};
