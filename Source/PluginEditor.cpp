/*
  ==============================================================================

    JUCE plugin editor for Multiband Up/Down Compressor (OTT-style)

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

//==============================================================================
static void styleKnob (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 20);
    s.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff58caeb));
    s.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff3b4354));
    s.setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xffd7d9df));
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
}

static void styleLabel (juce::Label& l)
{
    l.setJustificationType (juce::Justification::centred);
    l.setColour (juce::Label::textColourId, juce::Colour (0xffc9ced8));
}

//==============================================================================
void S3xtaOTTAudioProcessorEditor::OttLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                                                      float sliderPos, const float rotaryStartAngle,
                                                                      const float rotaryEndAngle, juce::Slider& slider)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (8.0f);
    auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 2.0f;
    auto centre = bounds.getCentre();
    auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);

    juce::Path knobShadow;
    knobShadow.addEllipse (bounds);
    juce::DropShadow (juce::Colour (0xaa000000), 14, { 0, 6 }).drawForPath (g, knobShadow);

    juce::ColourGradient knobGrad (juce::Colour (0xff313848), centre.x, centre.y - radius,
                                   juce::Colour (0xff1c212c), centre.x, centre.y + radius, false);
    g.setGradientFill (knobGrad);
    g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);

    juce::Path trackArc;
    trackArc.addCentredArc (centre.x, centre.y, radius + 8.0f, radius + 8.0f, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (juce::Colour (0x333f4b64));
    g.strokePath (trackArc, juce::PathStrokeType (2.0f));

    juce::Path valueArc;
    valueArc.addCentredArc (centre.x, centre.y, radius + 8.0f, radius + 8.0f, 0.0f, rotaryStartAngle, angle, true);
    g.setColour (accent);
    g.strokePath (valueArc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path ticks;
    const int tickCount = 34;
    for (int i = 0; i <= tickCount; ++i)
    {
        float a = rotaryStartAngle + (float) i / (float) tickCount * (rotaryEndAngle - rotaryStartAngle);
        float r1 = radius + 13.0f;
        float r2 = radius + 17.0f;
        ticks.startNewSubPath (centre.x + std::cos (a) * r1, centre.y + std::sin (a) * r1);
        ticks.lineTo (centre.x + std::cos (a) * r2, centre.y + std::sin (a) * r2);
    }
    g.setColour (juce::Colour (0x335c667d));
    g.strokePath (ticks, juce::PathStrokeType (1.0f));

    juce::Path pointerTrack;
    pointerTrack.startNewSubPath (centre.x, centre.y);
    pointerTrack.lineTo (centre.x, centre.y - radius + 8.0f);
    g.setColour (juce::Colour (0xfff0f2f5));
    g.strokePath (pointerTrack, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                  juce::AffineTransform::rotation (angle, centre.x, centre.y));

    g.setColour (juce::Colour (0x66a4afc7));
    g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.0f);
}

void S3xtaOTTAudioProcessorEditor::OttLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                                                     bool, bool)
{
    auto r = button.getLocalBounds().toFloat();
    auto active = button.getToggleState();

    g.setColour (active ? juce::Colour (0xff2a2a2a) : juce::Colour (0xff1a1a1a));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (active ? juce::Colour (0xfff1c15b) : juce::Colour (0xff3a3a3a));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);

    g.setColour (active ? juce::Colour (0xfff1c15b) : juce::Colour (0xffbdbdbd));
    g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
}

//==============================================================================
static void styleSegmentButton (juce::TextButton& b)
{
    b.setColour (juce::TextButton::buttonColourId, juce::Colour (0x33262d3a));
    b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0x66455166));
    b.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffaeb5c2));
    b.setColour (juce::TextButton::textColourOnId, juce::Colour (0xffe7ebf3));
}

//==============================================================================
S3xtaOTTAudioProcessorEditor::BipolarGRMeterComponent::BipolarGRMeterComponent() = default;

void S3xtaOTTAudioProcessorEditor::BipolarGRMeterComponent::setRange (float maxUpDbIn, float maxDownDbAbsIn)
{
    maxUpDb = juce::jmax (1.0f, maxUpDbIn);
    maxDownDbAbs = juce::jmax (1.0f, maxDownDbAbsIn);
}

void S3xtaOTTAudioProcessorEditor::BipolarGRMeterComponent::setValuesDb (float upDb, float downDb)
{
    const float targetUp = juce::jlimit (0.0f, maxUpDb, upDb);
    const float targetDown = juce::jlimit (-maxDownDbAbs, 0.0f, downDb);
    constexpr float response = 0.15f * 1.30f; // 130% of original GR response speed
    smoothUp = (1.0f - response) * smoothUp + response * targetUp;
    smoothDown = (1.0f - response) * smoothDown + response * targetDown;
}

void S3xtaOTTAudioProcessorEditor::BipolarGRMeterComponent::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    juce::ColourGradient bg (juce::Colour (0xff141a28), r.getX(), r.getY(),
                             juce::Colour (0xff0d111c), r.getRight(), r.getBottom(), false);
    bg.addColour (0.5, juce::Colour (0xff1b2234));
    g.setGradientFill (bg);
    g.fillRoundedRectangle (r, 4.0f);

    auto bar = r.reduced (4.0f, 3.0f);
    const float upNorm = juce::jlimit (0.0f, 1.0f, smoothUp / maxUpDb);
    const float downNorm = juce::jlimit (0.0f, 1.0f, std::abs (smoothDown) / maxDownDbAbs);
    const float midY = bar.getCentreY();
    const float upH = (midY - bar.getY()) * upNorm;
    const float downH = (bar.getBottom() - midY) * downNorm;
    const float barX = bar.getX();
    const float barW = bar.getWidth();

    juce::ColourGradient upGrad (juce::Colour (0xff67e2ff), barX, midY - upH,
                                 juce::Colour (0xff2ea3d7), barX, midY, false);
    g.setGradientFill (upGrad);
    g.fillRoundedRectangle (juce::Rectangle<float> (barX, midY - upH, barW, upH), 1.8f);
    g.setColour (juce::Colour (0x334cd6ff));
    g.fillRoundedRectangle (juce::Rectangle<float> (barX, midY - upH, barW, upH), 1.8f);

    juce::ColourGradient downGrad (juce::Colour (0xfff08bc8), barX, midY,
                                   juce::Colour (0xffa34a86), barX, midY + downH, false);
    g.setGradientFill (downGrad);
    g.fillRoundedRectangle (juce::Rectangle<float> (barX, midY, barW, downH), 1.8f);
    g.setColour (juce::Colour (0x33ff81cb));
    g.fillRoundedRectangle (juce::Rectangle<float> (barX, midY, barW, downH), 1.8f);

    g.setColour (juce::Colour (0x66a8b3ca));
    g.drawLine (barX, midY, barX + barW, midY, 1.0f);
    g.setColour (juce::Colour (0x55d3d9e6));
    g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
}

//==============================================================================
S3xtaOTTAudioProcessorEditor::StereoOutputMeterComponent::StereoOutputMeterComponent() = default;

void S3xtaOTTAudioProcessorEditor::StereoOutputMeterComponent::setLevelsDb (float leftDb, float rightDb, bool clipLatch)
{
    const float targetL = juce::jlimit (-100.0f, 0.0f, leftDb);
    const float targetR = juce::jlimit (-100.0f, 0.0f, rightDb);
    smoothL = 0.85f * smoothL + 0.15f * targetL;
    smoothR = 0.85f * smoothR + 0.15f * targetR;
    clip = clipLatch;
}

void S3xtaOTTAudioProcessorEditor::StereoOutputMeterComponent::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    juce::ColourGradient bg (juce::Colour (0xff141a27), r.getX(), r.getY(),
                             juce::Colour (0xff0c101a), r.getRight(), r.getBottom(), false);
    g.setGradientFill (bg);
    g.fillRoundedRectangle (r, 5.0f);

    auto meterArea = r.reduced (6.0f, 18.0f);
    auto leftArea = meterArea.removeFromLeft (meterArea.getWidth() * 0.5f - 3.0f);
    auto rightArea = meterArea;

    auto dbToNorm = [](float db)
    {
        const float minDb = -60.0f;
        const float maxDb = 0.0f;
        if (db <= minDb)
            return 0.0f;
        return juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
    };

    auto drawBar = [&g, &dbToNorm](juce::Rectangle<float> area, float db)
    {
        float norm = dbToNorm (db);
        auto h = area.getHeight() * norm;
        juce::ColourGradient barGrad (juce::Colour (0xff7ce8ff), area.getX(), area.getBottom() - h,
                                      juce::Colour (0xff5f74ff), area.getX(), area.getBottom(), false);
        g.setGradientFill (barGrad);
        g.fillRoundedRectangle (juce::Rectangle<float> (area.getX(), area.getBottom() - h, area.getWidth(), h), 1.6f);
    };

    drawBar (leftArea, smoothL);
    drawBar (rightArea, smoothR);

    clipBounds = juce::Rectangle<int> ((int) r.getCentreX() - 18, (int) r.getBottom() - 20, 36, 14);
    g.setColour (clip ? juce::Colour (0xffd96aa5) : juce::Colour (0xff202739));
    g.fillRoundedRectangle (clipBounds.toFloat(), 4.0f);
    g.setColour (clip ? juce::Colour (0xffffd6ea) : juce::Colour (0xff9ea8bf));
    g.drawRoundedRectangle (clipBounds.toFloat(), 4.0f, 1.0f);
    g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
    g.drawText ("CLIP", clipBounds, juce::Justification::centred);
    g.setColour (juce::Colour (0x55d3dae8));
    g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
}

void S3xtaOTTAudioProcessorEditor::StereoOutputMeterComponent::mouseUp (const juce::MouseEvent& e)
{
    if (clipBounds.contains (e.getPosition()))
        if (onClipClicked)
            onClipClicked();
}

//==============================================================================
S3xtaOTTAudioProcessorEditor::AnalyzerComponent::AnalyzerComponent (S3xtaOTTAudioProcessor& proc)
    : processor (proc)
{
}

void S3xtaOTTAudioProcessorEditor::AnalyzerComponent::updateData()
{
    bool ok = false;
    if (post)
        ok = processor.getPostFFTData (fftData);
    else
        ok = processor.getPreFFTData (fftData);

    hasData = ok;
}

void S3xtaOTTAudioProcessorEditor::AnalyzerComponent::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    juce::ColourGradient bg (juce::Colour (0xff111421), r.getX(), r.getY(),
                             juce::Colour (0xff0b0d14), r.getRight(), r.getBottom(), false);
    bg.addColour (0.5, juce::Colour (0xff171b2a));
    g.setGradientFill (bg);
    g.fillRoundedRectangle (r, 6.0f);

    if (! hasData)
        return;

    auto* apvts = &processor.getAPVTS();
    const float f1 = apvts->getRawParameterValue ("xover_f1_hz")->load();
    const float f2 = apvts->getRawParameterValue ("xover_f2_hz")->load();

    auto xForHz = [r](float hz)
    {
        const float minHz = 20.0f;
        const float maxHz = 20000.0f;
        float norm = (std::log10 (hz) - std::log10 (minHz)) / (std::log10 (maxHz) - std::log10 (minHz));
        return r.getX() + juce::jlimit (0.0f, 1.0f, norm) * r.getWidth();
    };

    const int gridLines = 5;
    g.setColour (juce::Colour (0x332a354a));
    for (int i = 1; i < gridLines; ++i)
    {
        float y = r.getY() + (r.getHeight() / (float) gridLines) * i;
        g.drawLine (r.getX() + 6.0f, y, r.getRight() - 6.0f, y, 1.0f);
    }

    g.setColour (juce::Colour (0x1f2f3e57));
    for (int i = 1; i < 7; ++i)
    {
        float x = r.getX() + (r.getWidth() / 7.0f) * (float) i;
        g.drawLine (x, r.getY() + 10.0f, x, r.getBottom() - 34.0f, 0.8f);
    }

    auto x1 = xForHz (f1);
    auto x2 = xForHz (f2);
    g.setColour (juce::Colour (0x66a9b8d8));
    g.drawLine (x1, r.getY() + 10.0f, x1, r.getBottom() - 34.0f, 1.0f);
    g.drawLine (x2, r.getY() + 10.0f, x2, r.getBottom() - 34.0f, 1.0f);

    g.setColour (juce::Colour (0xff9ca6bd));
    g.setFont (juce::Font (juce::FontOptions (11.5f)));
    g.drawText (juce::String (f1, 0) + " Hz", (int) x1 - 36, (int) r.getBottom() - 24, 72, 16, juce::Justification::centred);
    g.drawText ((f2 >= 1000.0f ? juce::String (f2 / 1000.0f, 2) + " kHz" : juce::String (f2, 0) + " Hz"),
                (int) x2 - 36, (int) r.getBottom() - 24, 72, 16, juce::Justification::centred);

    juce::Path p;
    const float minDb = -120.0f;
    const float maxDb = 0.0f;

    for (int i = 0; i < (int) fftData.size(); ++i)
    {
        float normX = (float) i / (float) (fftData.size() - 1);
        float db = juce::jlimit (minDb, maxDb, fftData[(size_t) i]);
        float normY = juce::jmap (db, minDb, maxDb, 1.0f, 0.0f);

        float x = r.getX() + normX * r.getWidth();
        float y = r.getY() + normY * r.getHeight();

        if (i == 0)
            p.startNewSubPath (x, y);
        else
            p.lineTo (x, y);
    }

    juce::Path glow = p;
    g.setColour (juce::Colour (0x3343b9ff));
    g.strokePath (glow, juce::PathStrokeType (6.0f));

    juce::ColourGradient lineGrad (juce::Colour (0xff9fa6bd), r.getX(), 0.0f,
                                   juce::Colour (0xffd173ae), r.getRight(), 0.0f, false);
    lineGrad.addColour (0.25, juce::Colour (0xff8cb3ff));
    g.setGradientFill (lineGrad);
    g.strokePath (p, juce::PathStrokeType (1.8f));

    auto drawDbLabel = [&g, r](int db, float y)
    {
        g.setColour (juce::Colour (0xff9ea6ba));
        g.drawText (juce::String (db), (int) r.getX() + 6, (int) y - 8, 42, 16, juce::Justification::centredLeft);
    };

    drawDbLabel (-10, r.getY() + r.getHeight() * 0.23f);
    drawDbLabel (-20, r.getY() + r.getHeight() * 0.40f);
    drawDbLabel (-30, r.getY() + r.getHeight() * 0.57f);

    g.setColour (juce::Colour (0xffc55d9d));
    g.setFont (juce::Font (juce::FontOptions (12.5f, juce::Font::bold)));
    g.drawText ("DOWNWARD", (int) r.getRight() - 160, (int) r.getY() + 8, 140, 18, juce::Justification::centred);

    g.setColour (juce::Colour (0xff9eb2c9));
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText ("UPWARD", (int) r.getX() + 16, (int) r.getY() + 8, 120, 18, juce::Justification::centredLeft);
}

//==============================================================================
S3xtaOTTAudioProcessorEditor::S3xtaOTTAudioProcessorEditor (S3xtaOTTAudioProcessor& p)
    : AudioProcessorEditor (&p)
    , audioProcessor (p)
    , grLow ()
    , grMid ()
    , grHigh ()
    , outMeter ()
    , analyzer (p)
{
    setLookAndFeel (&lookAndFeel);

    styleKnob (amountKnob.slider);
    styleKnob (timeKnob.slider);
    styleKnob (mixKnob.slider);
    styleKnob (outKnob.slider);

    amountKnob.slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff4fd0ec));
    timeKnob.slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff4fd0ec));
    mixKnob.slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xffcfd5e2));
    outKnob.slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xffd67ab3));

    styleLabel (amountKnob.label); amountKnob.label.setText ("AMOUNT", juce::dontSendNotification);
    styleLabel (timeKnob.label); timeKnob.label.setText ("TIME", juce::dontSendNotification);
    styleLabel (mixKnob.label); mixKnob.label.setText ("MIX", juce::dontSendNotification);
    styleLabel (outKnob.label); outKnob.label.setText ("OUT", juce::dontSendNotification);

    amountKnob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.getAPVTS(), "amount", amountKnob.slider);
    timeKnob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.getAPVTS(), "time_ms", timeKnob.slider);
    mixKnob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.getAPVTS(), "mix", mixKnob.slider);
    outKnob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.getAPVTS(), "out_gain_db", outKnob.slider);

    for (auto* c : { &amountKnob, &timeKnob, &mixKnob, &outKnob })
    {
        addAndMakeVisible (c->slider);
        addAndMakeVisible (c->label);
    }

    soloLow.setButtonText ("LOW");
    soloMid.setButtonText ("MID");
    soloHigh.setButtonText ("HIGH");
    soloLow.setColour (juce::ToggleButton::textColourId, juce::Colour (0xffbdbdbd));
    soloMid.setColour (juce::ToggleButton::textColourId, juce::Colour (0xffbdbdbd));
    soloHigh.setColour (juce::ToggleButton::textColourId, juce::Colour (0xffbdbdbd));
    soloLow.setColour (juce::ToggleButton::tickColourId, juce::Colour (0xffe6e6e6));
    soloMid.setColour (juce::ToggleButton::tickColourId, juce::Colour (0xffe6e6e6));
    soloHigh.setColour (juce::ToggleButton::tickColourId, juce::Colour (0xffe6e6e6));
    soloLow.setColour (juce::ToggleButton::tickDisabledColourId, juce::Colour (0xff2f2f2f));
    soloMid.setColour (juce::ToggleButton::tickDisabledColourId, juce::Colour (0xff2f2f2f));
    soloHigh.setColour (juce::ToggleButton::tickDisabledColourId, juce::Colour (0xff2f2f2f));
    soloLow.setColour (juce::ToggleButton::textColourId, juce::Colour (0xffbdbdbd));
    soloMid.setColour (juce::ToggleButton::textColourId, juce::Colour (0xffbdbdbd));
    soloHigh.setColour (juce::ToggleButton::textColourId, juce::Colour (0xffbdbdbd));

    auto setSoloColour = [](juce::ToggleButton& b)
    {
        auto active = b.getToggleState();
        b.setColour (juce::ToggleButton::textColourId, active ? juce::Colour (0xfff1c15b) : juce::Colour (0xffbdbdbd));
    };
    auto refreshSoloColours = [this, setSoloColour]
    {
        setSoloColour (soloLow);
        setSoloColour (soloMid);
        setSoloColour (soloHigh);
    };
    auto makeExclusiveSolo = [this] (juce::ToggleButton& activeButton)
    {
        if (! activeButton.getToggleState())
            return;

        if (&activeButton != &soloLow)
            soloLow.setToggleState (false, juce::sendNotificationSync);
        if (&activeButton != &soloMid)
            soloMid.setToggleState (false, juce::sendNotificationSync);
        if (&activeButton != &soloHigh)
            soloHigh.setToggleState (false, juce::sendNotificationSync);
    };

    soloLow.onClick = [this, refreshSoloColours, makeExclusiveSolo]
    {
        makeExclusiveSolo (soloLow);
        refreshSoloColours();
    };
    soloMid.onClick = [this, refreshSoloColours, makeExclusiveSolo]
    {
        makeExclusiveSolo (soloMid);
        refreshSoloColours();
    };
    soloHigh.onClick = [this, refreshSoloColours, makeExclusiveSolo]
    {
        makeExclusiveSolo (soloHigh);
        refreshSoloColours();
    };

    refreshSoloColours();

    soloLowAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        audioProcessor.getAPVTS(), "solo_low", soloLow);
    soloMidAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        audioProcessor.getAPVTS(), "solo_mid", soloMid);
    soloHighAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        audioProcessor.getAPVTS(), "solo_high", soloHigh);

    addAndMakeVisible (soloLow);
    addAndMakeVisible (soloMid);
    addAndMakeVisible (soloHigh);

    addAndMakeVisible (grLow);
    addAndMakeVisible (grMid);
    addAndMakeVisible (grHigh);
    addAndMakeVisible (outMeter);
    addAndMakeVisible (analyzer);

    preButton.setClickingTogglesState (true);
    styleSegmentButton (preButton);
    preButton.setButtonText ("PRE");
    preButton.setToggleState (false, juce::dontSendNotification);
    preButton.onClick = [this]
    {
        analyzerPostMode = preButton.getToggleState();
        analyzer.setPost (analyzerPostMode);
        preButton.setButtonText (analyzerPostMode ? "POST" : "PRE");
        preButton.setToggleState (analyzerPostMode, juce::dontSendNotification);
    };

    addAndMakeVisible (preButton);
    preButton.setVisible (false);

    styleLabel (lowLabel);
    lowLabel.setText ("LOW", juce::dontSendNotification);
    styleLabel (midLabel);
    midLabel.setText ("MID", juce::dontSendNotification);
    styleLabel (highLabel);
    highLabel.setText ("HIGH", juce::dontSendNotification);
    const auto midBandColour = juce::Colour (0xffb6bfd1);
    lowLabel.setColour (juce::Label::textColourId, midBandColour);
    midLabel.setColour (juce::Label::textColourId, midBandColour);
    highLabel.setColour (juce::Label::textColourId, midBandColour);
    styleLabel (outLabel);
    outLabel.setText ("OUT", juce::dontSendNotification);
    styleLabel (titleLabel);
    titleLabel.setText ("OTT", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (32.0f, juce::Font::bold)));
    lowLabel.setFont (juce::Font (juce::FontOptions (17.0f)));
    midLabel.setFont (juce::Font (juce::FontOptions (17.0f)));
    highLabel.setFont (juce::Font (juce::FontOptions (17.0f)));

    addAndMakeVisible (lowLabel);
    addAndMakeVisible (midLabel);
    addAndMakeVisible (highLabel);
    addAndMakeVisible (outLabel);
    addAndMakeVisible (titleLabel);
    addAndMakeVisible (topDivider1);
    addAndMakeVisible (topDivider2);
    addAndMakeVisible (leftColumn);
    addAndMakeVisible (centerColumn);
    addAndMakeVisible (rightColumn);
    leftColumn.setInterceptsMouseClicks (false, false);
    centerColumn.setInterceptsMouseClicks (false, false);
    rightColumn.setInterceptsMouseClicks (false, false);

    outMeter.onClipClicked = [this]
    {
        audioProcessor.resetClip();
    };

    styleKnob (f1Knob.slider); styleLabel (f1Knob.label); f1Knob.label.setText ("F1", juce::dontSendNotification);
    styleKnob (f2Knob.slider); styleLabel (f2Knob.label); f2Knob.label.setText ("F2", juce::dontSendNotification);
    styleKnob (linkKnob.slider); styleLabel (linkKnob.label); linkKnob.label.setText ("LINK", juce::dontSendNotification);
    styleKnob (attackKnob.slider); styleLabel (attackKnob.label); attackKnob.label.setText ("ATTACK", juce::dontSendNotification);
    styleKnob (releaseKnob.slider); styleLabel (releaseKnob.label); releaseKnob.label.setText ("RELEASE", juce::dontSendNotification);
    styleKnob (lowGainKnob.slider); styleLabel (lowGainKnob.label); lowGainKnob.label.setText ("LOW G", juce::dontSendNotification);
    styleKnob (midGainKnob.slider); styleLabel (midGainKnob.label); midGainKnob.label.setText ("MID G", juce::dontSendNotification);
    styleKnob (highGainKnob.slider); styleLabel (highGainKnob.label); highGainKnob.label.setText ("HIGH G", juce::dontSendNotification);

    f1Knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.getAPVTS(), "xover_f1_hz", f1Knob.slider);
    f2Knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.getAPVTS(), "xover_f2_hz", f2Knob.slider);
    linkKnob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.getAPVTS(), "stereo_link", linkKnob.slider);
    attackKnob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.getAPVTS(), "attack_ms", attackKnob.slider);
    releaseKnob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.getAPVTS(), "release_ms", releaseKnob.slider);
    lowGainKnob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.getAPVTS(), "low_gain_db", lowGainKnob.slider);
    midGainKnob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.getAPVTS(), "mid_gain_db", midGainKnob.slider);
    highGainKnob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.getAPVTS(), "high_gain_db", highGainKnob.slider);

    for (auto* c : { &f1Knob, &f2Knob, &linkKnob, &attackKnob, &releaseKnob,
                     &lowGainKnob, &midGainKnob, &highGainKnob })
    {
        advancedPanel.addAndMakeVisible (c->slider);
        advancedPanel.addAndMakeVisible (c->label);
    }

    addAndMakeVisible (advancedPanel);
    advancedPanel.setVisible (false);

    setSize (708, 456);
    startTimerHz (50);
}

S3xtaOTTAudioProcessorEditor::~S3xtaOTTAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

//==============================================================================
void S3xtaOTTAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff080b14));

    const float uiScale = juce::jlimit (0.60f, 1.0f, (float) getWidth() / 1180.0f);
    auto frame = getLocalBounds().toFloat().reduced (8.0f * uiScale + 4.0f);
    juce::DropShadow (juce::Colour (0x8a000000), (int) (20.0f * uiScale + 6.0f), { 0, (int) (8.0f * uiScale + 2.0f) }).drawForRectangle (g, frame.toNearestInt());

    juce::ColourGradient frameGrad (juce::Colour (0xff1a202f), frame.getX(), frame.getY(),
                                    juce::Colour (0xff0c1019), frame.getRight(), frame.getBottom(), false);
    frameGrad.addColour (0.42, juce::Colour (0xff212736));
    g.setGradientFill (frameGrad);
    g.fillRoundedRectangle (frame, 6.0f + 2.0f * uiScale);

    auto topPanelBottom = (float) amountKnob.slider.getBottom() + (16.0f + 12.0f * uiScale);
    auto topPanel = frame.withBottom (juce::jmin (frame.getBottom() - (96.0f + 64.0f * uiScale), topPanelBottom));
    juce::ColourGradient topGrad (juce::Colour (0x55343b4f), topPanel.getX(), topPanel.getY(),
                                  juce::Colour (0x22323845), topPanel.getRight(), topPanel.getBottom(), false);
    g.setGradientFill (topGrad);
    g.fillRoundedRectangle (topPanel, 6.0f + 2.0f * uiScale);

    g.setColour (juce::Colour (0x44d3d8e2));
    g.drawRoundedRectangle (frame, 6.0f + 2.0f * uiScale, 1.0f);

    g.setColour (juce::Colour (0x334f5669));
    g.drawLine (frame.getX(), topPanel.getBottom(), frame.getRight(), topPanel.getBottom(), 1.0f);

    auto leftLabelsBand = lowLabel.getBounds().toFloat().getUnion (highLabel.getBounds().toFloat()).expanded (8.0f, 4.0f);
    g.setColour (juce::Colour (0x1c121622));
    g.fillRoundedRectangle (leftLabelsBand, 4.0f);
    g.setColour (juce::Colour (0x34515e77));
    g.drawRoundedRectangle (leftLabelsBand, 4.0f, 1.0f);
}

void S3xtaOTTAudioProcessorEditor::resized()
{
    const int w = getWidth();
    const int h = getHeight();
    titleLabel.setFont (juce::Font (juce::FontOptions ((float) juce::jmax (24, h / 13), juce::Font::bold)));
    const float bandFont = (float) juce::jmax (13, h / 28);
    lowLabel.setFont (juce::Font (juce::FontOptions (bandFont)));
    midLabel.setFont (juce::Font (juce::FontOptions (bandFont)));
    highLabel.setFont (juce::Font (juce::FontOptions (bandFont)));
    const int pad = juce::jmax (14, h / 28);
    auto bounds = getLocalBounds().reduced (pad);
    auto advArea = advancedVisible ? bounds.removeFromBottom (juce::jmax (110, h / 4)) : juce::Rectangle<int>();
    auto topRow = bounds.removeFromTop (juce::jmax (170, h * 42 / 100));
    auto mainRow = bounds.reduced (0, juce::jmax (4, h / 90));

    // Top panel: AMOUNT | TIME | MIX | OUT with dividers
    {
        auto topArea = topRow.reduced (juce::jmax (10, w / 70), juce::jmax (8, h / 60));
        juce::FlexBox fb;
        fb.flexDirection = juce::FlexBox::Direction::row;
        fb.justifyContent = juce::FlexBox::JustifyContent::flexStart;
        fb.alignItems = juce::FlexBox::AlignItems::stretch;

        fb.items.add (juce::FlexItem (amountKnob.slider).withFlex (1.0f));
        fb.items.add (juce::FlexItem (topDivider1).withWidth (10.0f));
        fb.items.add (juce::FlexItem (timeKnob.slider).withFlex (1.0f));
        fb.items.add (juce::FlexItem (topDivider2).withWidth (10.0f));
        fb.items.add (juce::FlexItem (mixKnob.slider).withFlex (1.0f));
        fb.items.add (juce::FlexItem (outKnob.slider).withFlex (1.0f));

        fb.performLayout (topArea);

        auto layoutKnob = [h](ParamKnob& knob, juce::Rectangle<int> r)
        {
            auto labelArea = r.removeFromTop (juce::jmax (18, h / 26));
            knob.label.setBounds (labelArea);
            auto knobArea = r.reduced (juce::jmax (8, h / 50), juce::jmax (2, h / 220));
            knobArea = knobArea.reduced (knobArea.getWidth() / 10, knobArea.getHeight() / 10); // ~20% smaller
            knob.slider.setBounds (knobArea);
        };

        layoutKnob (amountKnob, amountKnob.slider.getBounds());
        layoutKnob (timeKnob, timeKnob.slider.getBounds());
        layoutKnob (mixKnob, mixKnob.slider.getBounds());
        layoutKnob (outKnob, outKnob.slider.getBounds());

        const int dividerInset = juce::jmax (10, h / 35);
        topDivider1.setBounds (topDivider1.getBounds().withHeight (topArea.getHeight() - 2 * dividerInset).withY (topArea.getY() + dividerInset));
        topDivider2.setBounds (topDivider2.getBounds().withHeight (topArea.getHeight() - 2 * dividerInset).withY (topArea.getY() + dividerInset));
    }

    // Main 3-column layout
    {
        juce::FlexBox fb;
        fb.flexDirection = juce::FlexBox::Direction::row;
        fb.justifyContent = juce::FlexBox::JustifyContent::spaceBetween;
        fb.alignItems = juce::FlexBox::AlignItems::stretch;

        fb.items.add (juce::FlexItem (leftColumn).withWidth ((float) juce::jmax (130, w * 19 / 100)));
        fb.items.add (juce::FlexItem (centerColumn).withFlex (1.0f));
        fb.items.add (juce::FlexItem (rightColumn).withWidth ((float) juce::jmax (84, w * 11 / 100)));
        fb.performLayout (mainRow);
    }

    // Left column: 3 band strips (LOW/MID/HIGH)
    {
        auto leftArea = leftColumn.getBounds().reduced (juce::jmax (3, w / 220), juce::jmax (6, h / 70));
        const int bandGap = juce::jmax (4, w / 190);
        const int labelHeight = juce::jmax (14, h / 30);
        const int soloHeight = juce::jmax (16, h / 28);

        auto layoutBand = [labelHeight, soloHeight, h](juce::Rectangle<int> r, juce::Label& label,
                                                    BipolarGRMeterComponent& meter, juce::ToggleButton& solo)
        {
            auto labelArea = r.removeFromTop (labelHeight);
            label.setBounds (labelArea);

            auto meterArea = r.removeFromTop (juce::jmax (0, r.getHeight() - soloHeight));
            meter.setBounds (meterArea.reduced (juce::jmax (3, h / 140), juce::jmax (3, h / 140)));
            solo.setBounds (r.removeFromTop (soloHeight).reduced (2, 2));
        };

        const int bandWidth = (leftArea.getWidth() - (2 * bandGap)) / 3;
        auto lowArea = leftArea.removeFromLeft (bandWidth);
        leftArea.removeFromLeft (bandGap);
        auto midArea = leftArea.removeFromLeft (bandWidth);
        leftArea.removeFromLeft (bandGap);
        auto highArea = leftArea;

        layoutBand (lowArea, lowLabel, grLow, soloLow);
        layoutBand (midArea, midLabel, grMid, soloMid);
        layoutBand (highArea, highLabel, grHigh, soloHigh);
    }

    // Center column: PRE/POST toggle + analyzer
    {
        auto centerArea = centerColumn.getBounds().reduced (juce::jmax (3, w / 240), juce::jmax (6, h / 70));
        auto header = centerArea.removeFromTop (juce::jmax (18, h / 26));
        auto prePostArea = header.removeFromLeft (juce::jmax (68, w / 11)).reduced (2, 2);
        preButton.setBounds (prePostArea);
        analyzer.setBounds (centerArea);
    }

    // Right column: Output meter + clip
    {
        auto rightArea = rightColumn.getBounds().reduced (juce::jmax (4, w / 180), juce::jmax (6, h / 70));
        auto labelArea = rightArea.removeFromTop (juce::jmax (14, h / 34));
        outLabel.setBounds (labelArea);
        outMeter.setBounds (rightArea);
    }

    auto topLabelY = topRow.getY() - juce::jmax (1, h / 320);
    const int titleW = juce::jmax (130, w * 22 / 100);
    const int titleH = juce::jmax (34, h / 9);
    titleLabel.setBounds (getLocalBounds().withY (topLabelY).withHeight (titleH).withWidth (titleW).withX (w / 2 - titleW / 2));

    if (advancedVisible)
    {
        layoutAdvancedPanel (advArea);
    }
}

void S3xtaOTTAudioProcessorEditor::applyAdvancedLayout()
{
    if (advancedVisible)
        setSize (708, 552);
    else
        setSize (708, 456);
}

void S3xtaOTTAudioProcessorEditor::layoutAdvancedPanel (juce::Rectangle<int> area)
{
    advancedPanel.setBounds (area);
    auto row1 = area.removeFromTop (juce::jmax (72, area.getHeight() * 45 / 100));
    auto row2 = area;

    auto cell = row1.getWidth() / 5;
    auto layoutKnobLocal = [](ParamKnob& knob, juce::Rectangle<int> r)
    {
        knob.label.setBounds (r.removeFromTop (18));
        auto knobArea = r.reduced (8, 6);
        knobArea = knobArea.reduced (knobArea.getWidth() / 10, knobArea.getHeight() / 10); // ~20% smaller
        knob.slider.setBounds (knobArea);
    };

    layoutKnobLocal (f1Knob, row1.removeFromLeft (cell));
    layoutKnobLocal (f2Knob, row1.removeFromLeft (cell));
    layoutKnobLocal (linkKnob, row1.removeFromLeft (cell));
    layoutKnobLocal (attackKnob, row1.removeFromLeft (cell));
    layoutKnobLocal (releaseKnob, row1.removeFromLeft (cell));

    auto cell2 = row2.getWidth() / 3;
    layoutKnobLocal (lowGainKnob, row2.removeFromLeft (cell2));
    layoutKnobLocal (midGainKnob, row2.removeFromLeft (cell2));
    layoutKnobLocal (highGainKnob, row2.removeFromLeft (cell2));
}

void S3xtaOTTAudioProcessorEditor::timerCallback()
{
    auto updateSolo = [](juce::ToggleButton& b)
    {
        const auto active = b.getToggleState();
        b.setColour (juce::ToggleButton::textColourId, active ? juce::Colour (0xfff1c15b) : juce::Colour (0xffbdbdbd));
    };

    updateSolo (soloLow);
    updateSolo (soloMid);
    updateSolo (soloHigh);

    analyzer.updateData();
    analyzer.repaint();
    const auto& meters = audioProcessor.getMeterState();
    grLow.setValuesDb (meters.bandUpGRdB[0].load (std::memory_order_relaxed),
                       meters.bandDownGRdB[0].load (std::memory_order_relaxed));
    grMid.setValuesDb (meters.bandUpGRdB[1].load (std::memory_order_relaxed),
                       meters.bandDownGRdB[1].load (std::memory_order_relaxed));
    grHigh.setValuesDb (meters.bandUpGRdB[2].load (std::memory_order_relaxed),
                        meters.bandDownGRdB[2].load (std::memory_order_relaxed));

    outMeter.setLevelsDb (meters.outPeakL_dBFS.load (std::memory_order_relaxed),
                          meters.outPeakR_dBFS.load (std::memory_order_relaxed),
                          meters.clipped.load (std::memory_order_relaxed));

    grLow.repaint();
    grMid.repaint();
    grHigh.repaint();
    outMeter.repaint();
    preButton.repaint();
}
