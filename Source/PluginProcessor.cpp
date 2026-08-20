/*
  ==============================================================================

    JUCE plugin processor for Multiband Up/Down Compressor (OTT-style)

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

//==============================================================================
static float dbToGain (float db) { return juce::Decibels::decibelsToGain (db); }
static float gainToDb (float gain) { return juce::Decibels::gainToDecibels (gain, -120.0f); }
static float msToCoeff (float ms, double sampleRate)
{
    ms = juce::jmax (0.01f, ms);
    return std::exp (-1.0f / (0.001f * ms * static_cast<float> (sampleRate)));
}

// Scale a compressor by changing its ratio instead of multiplying the dB
// gain reduction/boost directly.  If baseSlope = 1 - 1 / R, strength scales
// (R - 1).  This preserves the calibrated sound at strength 1.0 while making
// Amount/Upward approach limiting action smoothly at extreme settings.
static float scaleCompressionSlope (float baseSlope, float strength)
{
    baseSlope = juce::jlimit (0.0f, 0.9999f, baseSlope);
    strength = juce::jmax (0.0f, strength);
    return (strength * baseSlope)
         / juce::jmax (1.0e-6f, 1.0f - baseSlope + strength * baseSlope);
}

static float interpolateMacroCap (float strength, const std::array<float, 5>& caps)
{
    strength = juce::jlimit (0.0f, 4.0f, strength);
    if (strength <= 0.5f) return juce::jmap (strength, 0.0f, 0.5f, 0.0f, caps[0]);
    if (strength <= 1.0f) return juce::jmap (strength, 0.5f, 1.0f, caps[0], caps[1]);
    if (strength <= 1.5f) return juce::jmap (strength, 1.0f, 1.5f, caps[1], caps[2]);
    if (strength <= 2.0f) return juce::jmap (strength, 1.5f, 2.0f, caps[2], caps[3]);
    return juce::jmap (strength, 2.0f, 4.0f, caps[3], caps[4]);
}
//==============================================================================
void EnvelopeFollower::prepare (double sampleRate)
{
    juce::ignoreUnused (sampleRate);
    reset();
}

float EnvelopeFollower::processSample (float input, float attackCoeff, float releaseCoeff)
{
    // Use a power detector for smoother OTT-style action than raw peak tracking.
    const float x = input * input;
    if (x > env)
        env = attackCoeff * env + (1.0f - attackCoeff) * x;
    else
        env = releaseCoeff * env + (1.0f - releaseCoeff) * x;

    return std::sqrt (juce::jmax (env, 0.0f));
}

//==============================================================================
void UpDownCompressorBand::prepare (double sampleRate, int channels, int bandIndex)
{
    sr = sampleRate;
    profileIndex = juce::jlimit (0, 4, bandIndex);
    numChannels = channels;
    envs.resize (static_cast<size_t> (channels));
    for (auto& env : envs)
        env.prepare (sampleRate);
    linkedEnv.prepare (sampleRate);
}

void UpDownCompressorBand::reset()
{
    linkedEnv.reset();
    for (auto& env : envs)
        env.reset();
}

void UpDownCompressorBand::process (juce::AudioBuffer<float>& buffer,
                                    float amount, float expander, float upward,
                                    float downward, float timeScale, float thresholdOffsetDb,
                                    float stereoLink, float bandTrimDb,
                                    std::atomic<float>& upMeterDb, std::atomic<float>& downMeterDb)
{
    constexpr std::array<float, 5> expanderThresholds { -42.0f, -38.0f, -34.0f, -34.0f, -30.0f };
    constexpr std::array<float, 5> upwardThresholds { -15.0f, -13.5f, -11.0f, -13.5f, -13.5f };
    constexpr std::array<float, 5> downwardThresholds { -8.0f, -9.5f, -7.0f, -7.0f, -7.0f };
    constexpr std::array<float, 5> expanderSlopes { 0.70f, 0.72f, 0.72f, 0.77f, 0.72f };
    constexpr std::array<float, 5> downwardSlopes { 0.89f, 0.80f, 0.20f, 0.71f, 0.88f };

    // Per-band cap curves measured from VO-TT 1.1.0 at macro strengths
    // 0.5, 1.0, 1.5, 2.0 and at the combined Amount*stage extreme (4.0).
    // Array order is Air, Presence, Clear, Warm, Body.
    constexpr std::array<std::array<float, 5>, 5> upwardCapsDb {{
        {{ 16.813f, 22.929f, 25.786f, 27.186f, 34.180f }},
        {{ 16.565f, 22.819f, 25.760f, 27.197f, 35.850f }},
        {{ 14.026f, 19.097f, 21.718f, 23.291f, 35.730f }},
        {{ 11.198f, 15.380f, 17.572f, 18.922f, 35.570f }},
        {{ 21.704f, 28.551f, 30.013f, 30.157f, 34.500f }}
    }};
    constexpr std::array<std::array<float, 5>, 5> expanderCapsDb {{
        {{ 3.058f, 6.060f, 8.964f, 11.773f, 17.000f }},
        {{ 4.209f, 8.263f, 12.202f, 15.331f, 17.000f }},
        {{ 2.324f, 4.603f, 6.768f, 8.827f, 17.000f }},
        {{ 1.342f, 2.628f, 3.799f, 4.863f, 17.000f }},
        {{ 11.459f, 16.762f, 16.762f, 16.762f, 17.000f }}
    }};
    // Clean-room step-response measurements against VO-TT 1.1.0 show a
    // roughly 10 ms attack and 300 ms release at Time = 1.000.  The Time
    // macro scales both constants directly.
    constexpr std::array<float, 5> baseAttackMs { 9.5f, 9.5f, 10.0f, 10.0f, 10.0f };
    constexpr std::array<float, 5> baseReleaseMs { 300.0f, 300.0f, 300.0f, 300.0f, 300.0f };
    const size_t profile = (size_t) profileIndex;
    const float expanderThresholdDb = expanderThresholds[profile] + thresholdOffsetDb;
    const float upwardThresholdDb = upwardThresholds[profile] + thresholdOffsetDb;
    const float downwardThresholdDb = downwardThresholds[profile] + thresholdOffsetDb;
    const float attackCoeff = msToCoeff (baseAttackMs[profile] * timeScale, sr);
    const float releaseScale = std::pow (juce::jmax (0.01f, timeScale), 0.75f);
    const float releaseCoeff = msToCoeff (baseReleaseMs[profile] * releaseScale, sr);
    constexpr std::array<float, 5> cleanMakeupDb { 0.0f, 0.0f, 0.0f, 0.656f, 1.108f };

    const int samples = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();

    float upMeter = 0.0f;
    float downMeter = 0.0f;

    for (int i = 0; i < samples; ++i)
    {
        float linkedLevel = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
            linkedLevel = juce::jmax (linkedLevel, std::abs (buffer.getReadPointer (ch)[i]));

        float linkedEnvVal = linkedEnv.processSample (linkedLevel, attackCoeff, releaseCoeff);

        for (int ch = 0; ch < channels; ++ch)
        {
            float x = buffer.getWritePointer (ch)[i];
            float env = envs[(size_t) ch].processSample (x, attackCoeff, releaseCoeff);
            float usedEnv = juce::jmap (juce::jlimit (0.0f, 1.0f, stereoLink), env, linkedEnvVal);

            const float levelDb = gainToDb (usedEnv + 1.0e-9f);
            float gDownDb = 0.0f;
            float gUpDb = 0.0f;

            // VO-TT's four regions: expander, upward compression, null range,
            // then downward compression. Amount scales all three active stages.
            auto softAbove = [] (float x, float knee)
            {
                if (x <= -0.5f * knee) return 0.0f;
                if (x >= 0.5f * knee) return x;
                const float shifted = x + 0.5f * knee;
                return shifted * shifted / (2.0f * knee);
            };

            float gExpandDb = 0.0f;
            const float expanderStrength = expander * amount;
            const float upwardStrength = upward * amount;
            const float downwardStrength = downward * amount;

            if (levelDb < expanderThresholdDb)
                gExpandDb = -juce::jmin (interpolateMacroCap (expanderStrength,
                                                              expanderCapsDb[profile]),
                                        softAbove (expanderThresholdDb - levelDb, 4.0f)
                                        * expanderSlopes[profile] * expanderStrength);
            if (levelDb < upwardThresholdDb)
            {
                const float upwardSlope = scaleCompressionSlope (0.626f, upwardStrength);
                gUpDb = juce::jmin (interpolateMacroCap (upwardStrength,
                                                         upwardCapsDb[profile]),
                                    softAbove (upwardThresholdDb - levelDb, 6.0f) * upwardSlope);
            }
            if (levelDb > downwardThresholdDb)
            {
                const float downwardSlope = scaleCompressionSlope (downwardSlopes[profile],
                                                                    downwardStrength);
                gDownDb = -softAbove (levelDb - downwardThresholdDb, 8.0f)
                          * downwardSlope;
            }

            float gTotalDb = juce::jlimit (-72.0f, 60.0f,
                                           gExpandDb + gDownDb + gUpDb
                                           + cleanMakeupDb[profile] * amount + bandTrimDb * amount);
            float y = x * dbToGain (gTotalDb);

            // VO-TT's gain computer has a very small, repeatable asymmetric
            // residue (about -92 dBc H2 at quiet levels, rising to about
            // -71 dBc near -6 dBFS).  A bounded quadratic term reproduces
            // that measured signature without becoming a clipper or changing
            // the fundamental transfer curve.  It is part of the wet stage,
            // so Amount = 0 and bypassed bands remain linear.
            const float bounded = juce::jlimit (-8.0f, 8.0f, y);
            constexpr std::array<float, 5> wetAsymmetry {
                0.00170f, 0.00260f, 0.00240f, 0.00220f, 0.00105f
            };
            y += wetAsymmetry[profile] * amount * bounded * bounded;
            buffer.getWritePointer (ch)[i] = y;

            upMeter = juce::jmax (upMeter, gUpDb);
            downMeter = juce::jmin (downMeter, gDownDb);
        }
    }

    upMeterDb.store (upMeter, std::memory_order_relaxed);
    downMeterDb.store (downMeter, std::memory_order_relaxed);
}

//==============================================================================
void LinkwitzRiley2::prepare (double sampleRate, int channels)
{
    juce::dsp::ProcessSpec spec { sampleRate, 4096, static_cast<juce::uint32> (channels) };
    for (auto& stage : lowStages)
    {
        stage.setType (juce::dsp::FirstOrderTPTFilterType::lowpass);
        stage.prepare (spec);
    }
    for (auto& stage : highStages)
    {
        stage.setType (juce::dsp::FirstOrderTPTFilterType::highpass);
        stage.prepare (spec);
    }
}

void LinkwitzRiley2::reset()
{
    for (auto& stage : lowStages) stage.reset();
    for (auto& stage : highStages) stage.reset();
}

void LinkwitzRiley2::setFrequency (float frequency)
{
    for (auto& stage : lowStages) stage.setCutoffFrequency (frequency);
    for (auto& stage : highStages) stage.setCutoffFrequency (frequency);
}

void LinkwitzRiley2::processSplit (int channel, float input, float& low, float& high)
{
    low = lowStages[1].processSample (channel, lowStages[0].processSample (channel, input));
    // LR2 branches meet with opposite polarity. Inverting the high branch
    // makes their sum an all-pass response instead of a notch at crossover.
    high = -highStages[1].processSample (channel, highStages[0].processSample (channel, input));
}

float LinkwitzRiley2::processAllpass (int channel, float input)
{
    float low = 0.0f, high = 0.0f;
    processSplit (channel, input, low, high);
    return low + high;
}

//==============================================================================
void Crossover5Band::prepare (double sampleRate, int channels)
{
    sr = sampleRate;

    for (auto& split : splits)
        split.prepare (sampleRate, channels);
    for (auto& bandCompensators : phaseCompensators)
        for (auto& compensator : bandCompensators)
            compensator.prepare (sampleRate, channels);
    for (auto& compensator : fullRangeCompensators)
        compensator.prepare (sampleRate, channels);
    setFrequencies (frequencies);
}

void Crossover5Band::reset()
{
    for (auto& split : splits)
        split.reset();
    for (auto& bandCompensators : phaseCompensators)
        for (auto& compensator : bandCompensators)
            compensator.reset();
    for (auto& compensator : fullRangeCompensators)
        compensator.reset();
}

void Crossover5Band::setFrequencies (const std::array<float, 4>& newFrequencies)
{
    frequencies = newFrequencies;
    for (size_t i = 0; i < splits.size(); ++i)
    {
        const float frequency = juce::jlimit (20.0f, (float) sr * 0.45f, frequencies[i]);
        splits[i].setFrequency (frequency);
        for (auto& bandCompensators : phaseCompensators)
            bandCompensators[i].setFrequency (frequency);
        fullRangeCompensators[i].setFrequency (frequency);
    }
}

void Crossover5Band::process (const juce::AudioBuffer<float>& input,
                              std::array<juce::AudioBuffer<float>, 5>& bands)
{
    const int channels = input.getNumChannels();
    const int samples = input.getNumSamples();

    for (int i = 0; i < samples; ++i)
    {
        for (int ch = 0; ch < channels; ++ch)
        {
            float remainder = input.getSample (ch, i);
            float low = 0.0f, high = 0.0f;
            for (size_t split = 0; split < splits.size(); ++split)
            {
                splits[split].processSplit (ch, remainder, low, high);
                bands[4 - split].setSample (ch, i, low); // Body ... Pres.
                remainder = high;
            }
            bands[0].setSample (ch, i, remainder); // Air

        }
    }
}

void Crossover5Band::applyPhaseCompensation (std::array<juce::AudioBuffer<float>, 5>& bands)
{
    const int channels = bands[0].getNumChannels();
    const int samples = bands[0].getNumSamples();
    for (int i = 0; i < samples; ++i)
        for (int ch = 0; ch < channels; ++ch)
            for (int band = 4; band >= 2; --band)
            {
                float value = bands[(size_t) band].getSample (ch, i);
                const int firstLaterSplit = 5 - band;
                for (int split = firstLaterSplit; split < 4; ++split)
                    value = phaseCompensators[(size_t) band][(size_t) split].processAllpass (ch, value);
                bands[(size_t) band].setSample (ch, i, value);
            }
}

void Crossover5Band::processFullRangeAllpass (const juce::AudioBuffer<float>& input,
                                              juce::AudioBuffer<float>& output)
{
    output.makeCopyOf (input, true);
    for (int sample = 0; sample < output.getNumSamples(); ++sample)
        for (int channel = 0; channel < output.getNumChannels(); ++channel)
        {
            float value = output.getSample (channel, sample);
            for (auto& compensator : fullRangeCompensators)
                value = compensator.processAllpass (channel, value);
            output.setSample (channel, sample, value);
        }
}

//==============================================================================
void FFTDataGenerator::prepare (double sampleRate)
{
    sr = sampleRate;
    juce::ignoreUnused (sr);
}

void FFTDataGenerator::produceFFTDataForRendering (const float* data, size_t numSamples)
{
    if (numSamples < fftSize)
        return;

    std::fill (fftBuffer.begin(), fftBuffer.end(), 0.0f);
    for (int i = 0; i < fftSize; ++i)
        fftBuffer[(size_t) i] = data[i];

    window.multiplyWithWindowingTable (fftBuffer.data(), fftSize);
    fft.performRealOnlyForwardTransform (fftBuffer.data());

    std::array<float, fftSize / 2> magnitudes {};
    for (int i = 0; i < fftSize / 2; ++i)
    {
        auto re = fftBuffer[(size_t) i * 2];
        auto im = fftBuffer[(size_t) i * 2 + 1];
        auto mag = std::sqrt (re * re + im * im);
        magnitudes[(size_t) i] = juce::Decibels::gainToDecibels (mag, -120.0f);
    }

    for (int i = 0; i < fftSize / 2; ++i)
        smoothBuffer[(size_t) i] = 0.8f * smoothBuffer[(size_t) i] + 0.2f * magnitudes[(size_t) i];

    const int next = 1 - readIndex.load();
    dataBuffers[next] = smoothBuffer;
    readIndex.store (next, std::memory_order_release);
    hasData.store (true, std::memory_order_release);
}

bool FFTDataGenerator::getLatestFFTData (std::array<float, fftSize / 2>& dest) const
{
    if (! hasData.load (std::memory_order_acquire))
        return false;

    const int idx = readIndex.load (std::memory_order_acquire);
    dest = dataBuffers[idx];
    return true;
}

//==============================================================================
void AnalyzerFIFO::prepare (double sampleRate)
{
    generator.prepare (sampleRate);
    fifoIndex = 0;
}

void AnalyzerFIFO::pushSamples (const float* data, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        fifo[(size_t) fifoIndex++] = data[i];
        if (fifoIndex == FFTDataGenerator::fftSize)
        {
            generator.produceFFTDataForRendering (fifo.data(), fifo.size());
            fifoIndex = 0;
        }
    }
}

bool AnalyzerFIFO::getLatestFFTData (std::array<float, FFTDataGenerator::fftSize / 2>& dest) const
{
    return generator.getLatestFFTData (dest);
}

//==============================================================================
S3xtaOTTAudioProcessor::S3xtaOTTAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
    , apvts (*this, nullptr, "PARAMS", createParameterLayout())
{}

S3xtaOTTAudioProcessor::~S3xtaOTTAudioProcessor() = default;

//==============================================================================
const juce::String S3xtaOTTAudioProcessor::getName() const { return JucePlugin_Name; }

bool S3xtaOTTAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool S3xtaOTTAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool S3xtaOTTAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double S3xtaOTTAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int S3xtaOTTAudioProcessor::getNumPrograms() { return 1; }
int S3xtaOTTAudioProcessor::getCurrentProgram() { return 0; }
void S3xtaOTTAudioProcessor::setCurrentProgram (int) {}
const juce::String S3xtaOTTAudioProcessor::getProgramName (int) { return {}; }
void S3xtaOTTAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
void S3xtaOTTAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int channels = juce::jmax (1, getTotalNumInputChannels());

    for (auto& band : bandBuffers)
        band.setSize (channels, samplesPerBlock);
    for (auto& band : dryBandBuffers)
        band.setSize (channels, samplesPerBlock);
    alignedDryBuffer.setSize (channels, samplesPerBlock);
    crossover.prepare (sampleRate, channels);
    crossover.reset();
    for (size_t band = 0; band < bandProcessors.size(); ++band)
    {
        bandProcessors[band].prepare (sampleRate, channels, (int) band);
        bandProcessors[band].reset();
    }

    preAnalyzer.prepare (sampleRate);
}

void S3xtaOTTAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool S3xtaOTTAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

//==============================================================================
void S3xtaOTTAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midiMessages);

    const int totalNumInputChannels  = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();

    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    preAnalyzer.pushSamples (buffer.getReadPointer (0), buffer.getNumSamples());
    if (buffer.getNumSamples() == 0)
        return;

    const float inputGain = juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("in_gain_db")->load());
    buffer.applyGain (inputGain);
    const float inputPeakL = buffer.getMagnitude (0, 0, buffer.getNumSamples());
    const float inputPeakR = totalNumInputChannels > 1 ? buffer.getMagnitude (1, 0, buffer.getNumSamples()) : inputPeakL;
    meters.inPeakL_dBFS.store (juce::Decibels::gainToDecibels (inputPeakL, -100.0f), std::memory_order_relaxed);
    meters.inPeakR_dBFS.store (juce::Decibels::gainToDecibels (inputPeakR, -100.0f), std::memory_order_relaxed);

    const bool midSideMode = totalNumInputChannels > 1
                          && apvts.getRawParameterValue ("ms_mode")->load() > 0.5f;
    if (midSideMode)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const float left = buffer.getSample (0, sample);
            const float right = buffer.getSample (1, sample);
            // VO-TT keeps the encoded Mid/Side channels at the original
            // per-channel amplitude.  Using the energy-normalised sqrt(1/2)
            // matrix drives a pure Mid or Side signal 3 dB harder into the
            // dynamics detector and changes the compression law.
            buffer.setSample (0, sample, (left + right) * 0.5f);
            buffer.setSample (1, sample, (left - right) * 0.5f);
        }

    for (auto& band : bandBuffers)
        band.setSize (totalNumInputChannels, buffer.getNumSamples(), false, false, true);
    for (auto& band : dryBandBuffers)
        band.setSize (totalNumInputChannels, buffer.getNumSamples(), false, false, true);

    // VO-TT 1.1.0 Clean keeps these crossover points fixed when its legacy
    // Freq Drift automation parameters are changed.
    const std::array<float, 4> crossoverFrequencies { 212.0f, 637.0f, 3012.0f, 5394.0f };
    crossover.setFrequencies (crossoverFrequencies);
    alignedDryBuffer.setSize (totalNumInputChannels, buffer.getNumSamples(), false, false, true);
    crossover.processFullRangeAllpass (buffer, alignedDryBuffer);
    crossover.process (buffer, bandBuffers);
    for (size_t band = 0; band < bandBuffers.size(); ++band)
        dryBandBuffers[band].makeCopyOf (bandBuffers[band], true);

    const float amount = apvts.getRawParameterValue ("amount")->load();
    const float expander = apvts.getRawParameterValue ("expander")->load();
    const float upward = apvts.getRawParameterValue ("upward")->load();
    const float downward = apvts.getRawParameterValue ("downward")->load();
    const float timeScale = apvts.getRawParameterValue ("time_ms")->load();
    const float stereoLink = apvts.getRawParameterValue ("stereo_link")->load() * 0.01f;
    const std::array<const char*, 5> gains { "tone_air_db", "tone_presence_db", "tone_clear_db",
                                             "tone_warm_db", "tone_body_db" };
    const std::array<const char*, 5> nulls { "null_air", "null_presence", "null_clear", "null_warm", "null_body" };
    const std::array<const char*, 5> solos { "solo_high", "solo_presence", "solo_mid", "solo_warm", "solo_low" };
    const std::array<const char*, 5> mutes { "mute_high", "mute_presence", "mute_mid", "mute_warm", "mute_low" };
    const std::array<const char*, 5> bypasses { "bypass_air", "bypass_presence", "bypass_clear",
                                                "bypass_warm", "bypass_body" };
    bool anySolo = false;
    for (auto id : solos)
        anySolo = anySolo || apvts.getRawParameterValue (id)->load() > 0.5f;

    const float mix = juce::jlimit (0.0f, 1.0f, apvts.getRawParameterValue ("mix")->load() * 0.01f);
    for (size_t band = 0; band < bandBuffers.size(); ++band)
    {
        const float trim = apvts.getRawParameterValue (gains[band])->load();
        const float thresholdOffsetDb = apvts.getRawParameterValue (nulls[band])->load();
        const bool bypass = apvts.getRawParameterValue (bypasses[band])->load() > 0.5f;
        if (! bypass)
            bandProcessors[band].process (bandBuffers[band], amount, expander, upward, downward,
                                          timeScale, thresholdOffsetDb, stereoLink, trim,
                                          meters.bandUpGRdB[band], meters.bandDownGRdB[band]);
        else
            bandBuffers[band].makeCopyOf (dryBandBuffers[band], true);

        const bool solo = apvts.getRawParameterValue (solos[band])->load() > 0.5f;
        const bool mute = apvts.getRawParameterValue (mutes[band])->load() > 0.5f;
        if (mute || (anySolo && ! solo))
            bandBuffers[band].clear();
    }

    crossover.applyPhaseCompensation (bandBuffers);
    buffer.clear();
    for (const auto& band : bandBuffers)
        for (int ch = 0; ch < totalNumOutputChannels; ++ch)
            buffer.addFrom (ch, 0, band, ch, 0, buffer.getNumSamples());
    buffer.applyGain (mix);
    for (int ch = 0; ch < totalNumOutputChannels; ++ch)
        buffer.addFrom (ch, 0, alignedDryBuffer, ch, 0, buffer.getNumSamples(), 1.0f - mix);

    if (midSideMode)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const float mid = buffer.getSample (0, sample);
            const float side = buffer.getSample (1, sample);
            buffer.setSample (0, sample, mid + side);
            buffer.setSample (1, sample, mid - side);
        }
    buffer.applyGain (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("out_gain_db")->load()));

    float peakL = 0.0f;
    float peakR = 0.0f;
    if (totalNumInputChannels > 0)
        peakL = buffer.getMagnitude (0, 0, buffer.getNumSamples());
    if (totalNumInputChannels > 1)
        peakR = buffer.getMagnitude (1, 0, buffer.getNumSamples());
    else
        peakR = peakL;

    const float peakLDb = juce::Decibels::gainToDecibels (peakL, -100.0f);
    const float peakRDb = juce::Decibels::gainToDecibels (peakR, -100.0f);
    meters.outPeakL_dBFS.store (peakLDb, std::memory_order_relaxed);
    meters.outPeakR_dBFS.store (peakRDb, std::memory_order_relaxed);
    const bool isClipping = (peakL >= 1.0f || peakR >= 1.0f);
    meters.clipped.store (isClipping, std::memory_order_relaxed);
}

//==============================================================================
bool S3xtaOTTAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* S3xtaOTTAudioProcessor::createEditor()
{
    return new S3xtaOTTAudioProcessorEditor (*this);
}

//==============================================================================
void S3xtaOTTAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("sqtt_schema", 2, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void S3xtaOTTAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto state = juce::ValueTree::fromXml (*xml);

        auto findValue = [&state] (const juce::String& id, float fallback)
        {
            if (state.hasProperty (id))
                return static_cast<float> ((double) state.getProperty (id));
            for (int i = 0; i < state.getNumChildren(); ++i)
            {
                auto child = state.getChild (i);
                if (child.getProperty ("id").toString() == id && child.hasProperty ("value"))
                    return static_cast<float> ((double) child.getProperty ("value"));
            }
            return fallback;
        };

        auto setValue = [&state] (const juce::String& id, float value)
        {
            if (state.hasProperty (id))
                state.setProperty (id, value, nullptr);
            for (int i = 0; i < state.getNumChildren(); ++i)
            {
                auto child = state.getChild (i);
                if (child.getProperty ("id").toString() == id && child.hasProperty ("value"))
                    child.setProperty ("value", value, nullptr);
            }
        };

        // Builds released before SQ-TT stored raw values in the former ranges.
        // Keep the parameter IDs (and therefore DAW automation) intact, while
        // translating persisted raw state to the VO-TT-compatible ranges.
        const bool hasSchema = state.hasProperty ("sqtt_schema");
        const bool looksLegacy = findValue ("amount", 1.0f) > 2.001f
                              || findValue ("time_ms", 1.0f) > 2.001f
                              || findValue ("null_air", 0.0f) > 30.001f;
        if (! hasSchema && looksLegacy)
        {
            setValue ("amount", juce::jlimit (0.0f, 2.0f, findValue ("amount", 100.0f) * 0.01f));

            const float oldTime = juce::jlimit (10.0f, 1000.0f, findValue ("time_ms", 100.0f));
            setValue ("time_ms", juce::jlimit (0.5f, 2.0f, oldTime * 0.01f));

            setValue ("mix", juce::jlimit (0.0f, 100.0f, findValue ("mix", 0.0f)));
            setValue ("in_gain_db", juce::jlimit (-24.0f, 24.0f, findValue ("in_gain_db", 0.0f)));
            setValue ("out_gain_db", juce::jlimit (-24.0f, 24.0f, findValue ("out_gain_db", 0.0f)));

            for (auto id : { "tone_air_db", "tone_presence_db", "tone_clear_db", "tone_warm_db", "tone_body_db" })
                setValue (id, juce::jlimit (-30.0f, 30.0f, findValue (id, 0.0f)));
            for (auto id : { "null_air", "null_presence", "null_clear", "null_warm", "null_body" })
                setValue (id, juce::jlimit (-30.0f, 30.0f, (findValue (id, 50.0f) - 50.0f) * 0.6f));
        }

        state.setProperty ("sqtt_schema", 2, nullptr);
        apvts.replaceState (state);
    }
}

//==============================================================================
bool S3xtaOTTAudioProcessor::getPreFFTData (std::array<float, FFTDataGenerator::fftSize / 2>& dest) const
{
    return preAnalyzer.getLatestFFTData (dest);
}

//==============================================================================
S3xtaOTTAudioProcessor::APVTS::ParameterLayout S3xtaOTTAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto formattedFloat = [&params] (const char* id, const char* name,
                                     juce::NormalisableRange<float> range, float defaultValue,
                                     std::function<juce::String (float)> formatter)
    {
        auto attributes = juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([formatter] (float value, int) { return formatter (value); })
            .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue(); });
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, range, defaultValue, attributes));
    };
    auto formattedBool = [&params] (const char* id, const char* name, bool defaultValue)
    {
        auto attributes = juce::AudioParameterBoolAttributes()
            .withStringFromValueFunction ([] (bool enabled, int)
                                          { return enabled ? "Enabled" : "Disabled"; })
            .withValueFromStringFunction ([] (const juce::String& text)
                                            { return text.equalsIgnoreCase ("Enabled"); });
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { id, 1 }, name, defaultValue, attributes));
    };
    const auto scalar3 = [] (float v) { return juce::String (v, 3); };
    const auto percent1 = [] (float v) { return juce::String (v, 1) + "%"; };
    const auto gain2 = [] (float v)
    {
        return (v > 0.0f ? "+" : "") + juce::String (v, 2) + "dB";
    };
    formattedFloat ("amount", "Amount", { 0.0f, 2.0f, 0.001f }, 1.0f, scalar3);
    formattedFloat ("time_ms", "Time", { 0.5f, 2.0f, 0.001f, 0.63092977f }, 1.0f, scalar3);
    formattedFloat ("mix", "Mix", { 0.0f, 100.0f, 0.1f }, 0.0f, percent1);
    formattedFloat ("out_gain_db", "Output Gain", { -24.0f, 24.0f, 0.01f }, 0.0f, gain2);
    params.push_back (std::make_unique<juce::AudioParameterBool> ("auto_gain", "Auto Gain", true));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("soft_clip", "Soft Clip", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("ceiling_db", "Ceiling",
        juce::NormalisableRange<float> (-12.0f, 0.0f, 0.01f), -0.5f));

    formattedBool ("solo_low", "Body Solo", false);
    formattedBool ("solo_mid", "Clear Solo", false);
    formattedBool ("solo_high", "Air Solo", false);
    formattedBool ("mute_low", "Body Mute", false);
    formattedBool ("mute_mid", "Clear Mute", false);
    formattedBool ("mute_high", "Air Mute", false);

    params.push_back (std::make_unique<juce::AudioParameterFloat> ("xover_f1_hz", "Xover F1",
        juce::NormalisableRange<float> (20.0f, 2000.0f, 0.01f, 0.35f), 120.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("xover_f2_hz", "Xover F2",
        juce::NormalisableRange<float> (200.0f, 18000.0f, 0.01f, 0.35f), 2500.0f));
    formattedFloat ("stereo_link", "Stereo Linking", { 0.0f, 100.0f, 0.1f }, 100.0f, percent1);
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("updown_balance", "Up/Down Balance",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 50.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("attack_ms", "Attack",
        juce::NormalisableRange<float> (0.1f, 50.0f, 0.01f, 0.35f), 1.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("release_ms", "Release",
        juce::NormalisableRange<float> (10.0f, 2000.0f, 0.01f, 0.35f), 180.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> ("low_gain_db", "Low Gain",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("mid_gain_db", "Mid Gain",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("high_gain_db", "High Gain",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f));

    formattedFloat ("in_gain_db", "Input Gain", { -24.0f, 24.0f, 0.01f }, 0.0f, gain2);
    formattedFloat ("expander", "Expander", { 0.0f, 2.0f, 0.001f }, 1.0f, scalar3);
    formattedFloat ("upward", "Upward", { 0.0f, 2.0f, 0.001f }, 1.0f, scalar3);
    formattedFloat ("downward", "Downward", { 0.0f, 2.0f, 0.001f }, 1.0f, scalar3);

    for (const auto& spec : { std::pair { "tone_air_db", "Air Makeup" },
                              std::pair { "tone_presence_db", "Presence Makeup" },
                              std::pair { "tone_clear_db", "Clear Makeup" },
                              std::pair { "tone_warm_db", "Warm Makeup" },
                              std::pair { "tone_body_db", "Body Makeup" } })
        formattedFloat (spec.first, spec.second, { -30.0f, 30.0f, 0.1f }, 0.0f, gain2);

    formattedBool ("solo_presence", "Presence Solo", false);
    formattedBool ("mute_presence", "Presence Mute", false);
    formattedBool ("solo_warm", "Warm Solo", false);
    formattedBool ("mute_warm", "Warm Mute", false);

    for (const auto& spec : { std::pair { "null_air", "Air Threshold" },
                              std::pair { "null_presence", "Presence Threshold" },
                              std::pair { "null_clear", "Clear Threshold" },
                              std::pair { "null_warm", "Warm Threshold" },
                              std::pair { "null_body", "Body Threshold" } })
        formattedFloat (spec.first, spec.second, { -30.0f, 30.0f, 0.001f }, 0.0f,
                        [] (float v) { return juce::String (v, v > 0.0f ? 2 : 3) + "dB"; });

    for (const auto& spec : { std::pair { "freq_drift_low", "Low Freq Drift" },
                              std::pair { "freq_drift_mid", "Mid Freq Drift" },
                              std::pair { "freq_drift_high", "High Freq Drift" },
                              std::pair { "freq_drift_air", "Air Freq Drift" } })
        formattedFloat (spec.first, spec.second, { -100.0f, 100.0f, 0.1f }, 0.0f, percent1);

    formattedBool ("bypass_body", "Body Bypass", false);
    formattedBool ("bypass_warm", "Warm Bypass", false);
    formattedBool ("bypass_clear", "Clear Bypass", false);
    formattedBool ("bypass_presence", "Presence Bypass", false);
    formattedBool ("bypass_air", "Air Bypass", false);
    formattedBool ("ms_mode", "MS Mode On", false);

    return { params.begin(), params.end() };
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new S3xtaOTTAudioProcessor();
}
