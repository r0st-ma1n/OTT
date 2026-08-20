#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
constexpr auto background = 0xff050709;
constexpr auto panel = 0xff0b0d10;
constexpr auto panelDeep = 0xff07090c;
constexpr auto textMain = 0xffececf4;
constexpr auto textMuted = 0xff8f929d;
constexpr auto purple = 0xffaa80ff;
constexpr auto border = 0x17ffffff;

juce::String percentText (double value) { return juce::String (value, 1) + "%"; }
juce::String scalarText (double value) { return juce::String (value, 3); }
juce::String gainText (double value)
{
    return (value > 0.0 ? "+" : "") + juce::String (value, 2) + "dB";
}
}

void S3xtaOTTAudioProcessorEditor::FamilyLookAndFeel::drawRotarySlider (
    juce::Graphics& g, int x, int y, int width, int height, float position,
    float startAngle, float endAngle, juce::Slider& slider)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (5.0f);
    const float diameter = juce::jmin (bounds.getWidth(), bounds.getHeight());
    auto knob = juce::Rectangle<float> (diameter, diameter).withCentre (bounds.getCentre());
    const float radius = diameter * 0.5f;
    const float angle = startAngle + position * (endAngle - startAngle);
    juce::ignoreUnused (slider);

    juce::ColourGradient body (juce::Colour (0xff25282e), knob.getX(), knob.getY(),
                               juce::Colour (0xff0a0b0e), knob.getRight(), knob.getBottom(), false);
    g.setGradientFill (body);
    g.fillEllipse (knob);
    g.setColour (juce::Colour (0xff3a3d44));
    g.drawEllipse (knob, 1.2f);
    g.setColour (juce::Colour (0xff08090b));
    g.drawEllipse (knob.reduced (4.0f), 1.0f);

    juce::Path backgroundArc;
    backgroundArc.addCentredArc (knob.getCentreX(), knob.getCentreY(), radius + 1.0f, radius + 1.0f,
                                  0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colour (0xff202228));
    g.strokePath (backgroundArc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));

    juce::Path activeArc;
    activeArc.addCentredArc (knob.getCentreX(), knob.getCentreY(), radius + 1.0f, radius + 1.0f,
                              0.0f, startAngle, angle, true);
    g.setColour (juce::Colour (purple));
    g.strokePath (activeArc, juce::PathStrokeType (3.2f,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.2f, -radius * 0.70f, 2.4f, radius * 0.48f, 1.2f);
    g.setColour (juce::Colour (textMain));
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (knob.getCentreX(), knob.getCentreY()));
}

void S3xtaOTTAudioProcessorEditor::FamilyLookAndFeel::drawToggleButton (
    juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool down)
{
    auto r = button.getLocalBounds().toFloat().reduced (1.0f);
    const bool active = button.getToggleState();
    g.setColour (active ? juce::Colour (purple).withAlpha (down ? 0.45f : 0.25f)
                        : juce::Colour (0xff0a0c0f));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (active ? juce::Colour (purple) : juce::Colour (border).withAlpha (highlighted ? 0.22f : 0.09f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    g.setColour (active ? juce::Colour (textMain) : juce::Colour (textMuted));
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
}

void S3xtaOTTAudioProcessorEditor::VerticalMeter::setLevel (float newDb)
{
    targetDb = juce::jlimit (-100.0f, 6.0f, newDb);
    displayDb += (targetDb - displayDb) * (targetDb > displayDb ? 0.34f : 0.10f);
    repaint();
}

void S3xtaOTTAudioProcessorEditor::VerticalMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff030405));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (juce::Colour (border));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
    auto inner = r.reduced (3.0f);
    constexpr int segments = 22;
    const float normalised = juce::jlimit (0.0f, 1.0f, (displayDb + 60.0f) / 66.0f);
    const int active = juce::roundToInt (normalised * segments);
    const float segmentHeight = inner.getHeight() / (float) segments;
    for (int i = 0; i < segments; ++i)
    {
        auto segment = juce::Rectangle<float> (inner.getX(), inner.getBottom() - (i + 1) * segmentHeight + 1.0f,
                                                inner.getWidth(), segmentHeight - 2.0f);
        g.setColour (i < active ? juce::Colour (purple).withAlpha (0.72f + 0.28f * i / segments)
                                : juce::Colour (0xff111319));
        g.fillRect (segment);
    }
}

S3xtaOTTAudioProcessorEditor::S3xtaOTTAudioProcessorEditor (S3xtaOTTAudioProcessor& processor)
    : AudioProcessorEditor (&processor), audioProcessor (processor)
{
    setLookAndFeel (&familyLookAndFeel);

    configureKnob (timeKnob, "Time", "time_ms", [] (double v) { return juce::String (v, 3); });
    configureKnob (amountKnob, "Amount", "amount", [] (double v) { return juce::String (v, 3); });
    configureKnob (mixKnob, "Mix", "mix", percentText);
    configureKnob (inKnob, "In", "in_gain_db", gainText);
    configureKnob (expanderKnob, "Expander", "expander", scalarText);
    configureKnob (upwardKnob, "Upward", "upward", scalarText);
    configureKnob (downwardKnob, "Downward", "downward", scalarText);
    configureKnob (outKnob, "Out", "out_gain_db", gainText);

    const std::array<const char*, 5> bandLabels { "Air", "Pres.", "Clear", "Warm", "Body" };
    const std::array<const char*, 5> bandIds { "tone_air_db", "tone_presence_db", "tone_clear_db",
                                               "tone_warm_db", "tone_body_db" };
    const std::array<const char*, 5> soloIds { "solo_high", "solo_presence", "solo_mid", "solo_warm", "solo_low" };
    const std::array<const char*, 5> muteIds { "mute_high", "mute_presence", "mute_mid", "mute_warm", "mute_low" };
    const std::array<const char*, 5> bypassIds { "bypass_air", "bypass_presence", "bypass_clear",
                                                 "bypass_warm", "bypass_body" };
    const std::array<const char*, 5> nullIds { "null_air", "null_presence", "null_clear", "null_warm", "null_body" };
    for (size_t i = 0; i < toneKnobs.size(); ++i)
    {
        configureKnob (toneKnobs[i], bandLabels[i], bandIds[i],
                       [] (double v) { return juce::String (v, 1); });
        toneKnobs[i].slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 42, 15);
        configureBandButton (soloButtons[i], "S");
        configureBandButton (muteButtons[i], "M");
        configureBandButton (bypassButtons[i], bandLabels[i]);
        toneKnobs[i].label.setVisible (false);
        soloAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            audioProcessor.getAPVTS(), soloIds[i], soloButtons[i]);
        muteAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            audioProcessor.getAPVTS(), muteIds[i], muteButtons[i]);
        bypassAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            audioProcessor.getAPVTS(), bypassIds[i], bypassButtons[i]);
        nullSliders[i].setSliderStyle (juce::Slider::LinearHorizontal);
        nullSliders[i].setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        nullSliders[i].setAlpha (0.01f); // painted by the family band display below
        nullAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            audioProcessor.getAPVTS(), nullIds[i], nullSliders[i]);
        addAndMakeVisible (nullSliders[i]);
    }

    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);
    logoImage = juce::ImageFileFormat::loadFrom (BinaryData::logo_1113_png,
                                                  static_cast<size_t> (BinaryData::logo_1113_pngSize));
    logoSourceBounds = findOpaqueBounds (logoImage);
    setResizable (false, false);
    // 400 x 687 is the actual plug-in content area measured from the supplied
    // 402 x 794 Cubase screenshot (the remaining pixels are host chrome).
    setSize (400, 687);
    startTimerHz (60);
}

S3xtaOTTAudioProcessorEditor::~S3xtaOTTAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void S3xtaOTTAudioProcessorEditor::configureKnob (
    Knob& knob, const juce::String& label, const juce::String& parameterId,
    std::function<juce::String (double)> formatter)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.22f,
                                     juce::MathConstants<float>::pi * 2.78f, true);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxAbove, false, 74, 21);
    knob.slider.setColour (juce::Slider::textBoxTextColourId, juce::Colour (textMain));
    knob.slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff090b0e));
    knob.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (border));
    knob.slider.textFromValueFunction = std::move (formatter);
    knob.label.setText (label, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setColour (juce::Label::textColourId, juce::Colour (textMain));
    knob.label.setFont (juce::Font (juce::FontOptions (13.0f)));
    knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.getAPVTS(), parameterId, knob.slider);
    addAndMakeVisible (knob.slider);
    addAndMakeVisible (knob.label);
}

void S3xtaOTTAudioProcessorEditor::configureBandButton (juce::ToggleButton& button, const juce::String& text)
{
    button.setButtonText (text);
    addAndMakeVisible (button);
}

juce::Rectangle<int> S3xtaOTTAudioProcessorEditor::findOpaqueBounds (const juce::Image& image)
{
    if (! image.isValid()) return {};
    int minX = image.getWidth(), minY = image.getHeight(), maxX = -1, maxY = -1;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
            if (image.getPixelAt (x, y).getAlpha() > 20)
            {
                minX = juce::jmin (minX, x); minY = juce::jmin (minY, y);
                maxX = juce::jmax (maxX, x); maxY = juce::jmax (maxY, y);
            }
    return maxX >= minX ? juce::Rectangle<int> (minX, minY, maxX - minX + 1, maxY - minY + 1)
                        : juce::Rectangle<int>();
}

void S3xtaOTTAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (background));
    auto outer = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (juce::Colour (panelDeep));
    g.fillRoundedRectangle (outer, 8.0f);
    g.setColour (juce::Colour (border));
    g.drawRoundedRectangle (outer, 8.0f, 1.0f);

    if (logoImage.isValid() && ! logoSourceBounds.isEmpty())
    {
        g.setOpacity (1.0f);
        g.drawImage (logoImage, 171, 4, 58, 54,
                     logoSourceBounds.getX(), logoSourceBounds.getY(),
                     logoSourceBounds.getWidth(), logoSourceBounds.getHeight());
    }

    g.setColour (juce::Colour (purple).withAlpha (0.24f));
    g.drawLine (120.0f, 68.0f, 158.0f, 68.0f, 1.0f);
    g.drawLine (242.0f, 68.0f, 280.0f, 68.0f, 1.0f);
    g.setColour (juce::Colour (textMain));
    g.setFont (juce::Font (juce::FontOptions (27.0f)));
    g.drawText ("S Q - T T", 142, 53, 116, 31, juce::Justification::centred);

    auto main = juce::Rectangle<float> (2.0f, 104.0f, 396.0f, 275.0f);
    g.setColour (juce::Colour (panel));
    g.fillRoundedRectangle (main, 6.0f);
    g.setColour (juce::Colour (border));
    g.drawRoundedRectangle (main, 6.0f, 1.0f);

    const std::array<const char*, 7> meterLabels { "-inf", "-12", "-6", "-3", "0", "+3", "+6" };
    g.setFont (juce::Font (juce::FontOptions (7.5f)));
    g.setColour (juce::Colour (textMuted));
    for (size_t i = 0; i < meterLabels.size(); ++i)
    {
        const int y = 128 + (int) i * 19;
        g.drawText (meterLabels[i], 13, y, 24, 12, juce::Justification::centredRight);
        g.drawText (meterLabels[i], 363, y, 24, 12, juce::Justification::centredLeft);
    }

    auto tone = juce::Rectangle<float> (2.0f, 382.0f, 396.0f, 293.0f);
    g.setColour (juce::Colour (panel));
    g.fillRoundedRectangle (tone, 6.0f);
    g.setColour (juce::Colour (border));
    g.drawRoundedRectangle (tone, 6.0f, 1.0f);
    for (size_t i = 0; i < bandBarBounds.size(); ++i)
    {
        drawBandBar (g, (int) i, bandBarBounds[i]);
        if (i + 1 < bandBarBounds.size())
        {
            g.setColour (juce::Colour (border).withAlpha (0.06f));
            g.drawLine (10.0f, bandBarBounds[i].getBottom() + 7.0f,
                        390.0f, bandBarBounds[i].getBottom() + 7.0f, 1.0f);
        }
    }
}

void S3xtaOTTAudioProcessorEditor::drawBandBar (
    juce::Graphics& g, int index, juce::Rectangle<float> bounds)
{
    g.setColour (juce::Colour (0xff050608));
    g.fillRoundedRectangle (bounds, 3.0f);
    const float amount = juce::jlimit (0.0f, 1.0f,
        ((float) nullSliders[(size_t) index].getValue() + 30.0f) / 60.0f);
    auto active = bounds.reduced (2.0f);
    active.setWidth (active.getWidth() * amount);
    juce::ColourGradient fill (juce::Colour (purple).withAlpha (0.78f), active.getX(), active.getY(),
                               juce::Colour (purple).withAlpha (0.24f), active.getRight(), active.getBottom(), false);
    g.setGradientFill (fill);
    g.fillRoundedRectangle (active, 2.0f);
    g.setColour (juce::Colour (border));
    g.drawRoundedRectangle (bounds, 3.0f, 1.0f);
}

void S3xtaOTTAudioProcessorEditor::resized()
{
    inputMeter.setBounds (40, 136, 12, 132);
    outputMeter.setBounds (348, 136, 12, 132);

    auto placeKnob = [] (Knob& knob, juce::Rectangle<int> slider, juce::Rectangle<int> label)
    {
        knob.slider.setBounds (slider);
        knob.label.setBounds (label);
    };
    placeKnob (timeKnob, { 80, 125, 66, 98 }, { 82, 224, 62, 18 });
    placeKnob (amountKnob, { 156, 120, 88, 126 }, { 158, 248, 84, 20 });
    placeKnob (mixKnob, { 254, 125, 66, 98 }, { 256, 224, 62, 18 });

    const std::array<Knob*, 5> lower { &inKnob, &expanderKnob, &upwardKnob, &downwardKnob, &outKnob };
    for (size_t i = 0; i < lower.size(); ++i)
    {
        const int x = 8 + (int) i * 77;
        placeKnob (*lower[i], { x + 7, 282, 62, 67 }, { x, 350, 76, 18 });
    }

    for (size_t i = 0; i < toneKnobs.size(); ++i)
    {
        const int y = 390 + (int) i * 56;
        bypassButtons[i].setBounds (8, y + 10, 46, 22);
        soloButtons[i].setBounds (58, y, 20, 20);
        muteButtons[i].setBounds (58, y + 22, 20, 20);
        bandBarBounds[i] = juce::Rectangle<float> (84.0f, (float) y + 3.0f, 258.0f, 39.0f);
        nullSliders[i].setBounds (bandBarBounds[i].getSmallestIntegerContainer());
        toneKnobs[i].slider.setBounds (342, y - 6, 56, 55);
    }
}

void S3xtaOTTAudioProcessorEditor::timerCallback()
{
    const auto& meters = audioProcessor.getMeterState();
    inputMeter.setLevel (juce::jmax (meters.inPeakL_dBFS.load (std::memory_order_relaxed),
                                     meters.inPeakR_dBFS.load (std::memory_order_relaxed)));
    outputMeter.setLevel (juce::jmax (meters.outPeakL_dBFS.load (std::memory_order_relaxed),
                                      meters.outPeakR_dBFS.load (std::memory_order_relaxed)));
    for (const auto& bounds : bandBarBounds)
        repaint (bounds.getSmallestIntegerContainer().expanded (2));
}
