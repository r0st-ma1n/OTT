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
static float amountToDrive (float amount01)
{
    amount01 = juce::jlimit (0.0f, 1.5f, amount01);

    // Keep the first half controllable and make the top end more exciting.
    const float base = std::pow (amount01, 1.35f) * 1.7f;
    const float excitement = std::pow (juce::jmax (0.0f, amount01 - 0.72f) / 0.78f, 2.0f) * 0.65f;
    return juce::jlimit (0.0f, 2.8f, base + excitement);
}

static float amountToAutoGainCompDb (float amount01, float mix01)
{
    amount01 = juce::jlimit (0.0f, 1.5f, amount01);
    mix01 = juce::jlimit (0.0f, 1.0f, mix01);

    const float compDb = -(2.2f * amount01 + 1.8f * amount01 * amount01);
    return compDb * mix01;
}

static float applySoftClipSample (float x, float ceilingGain)
{
    if (ceilingGain <= 0.0f)
        return x;

    const float normalised = x / ceilingGain;
    return std::tanh (normalised) * ceilingGain;
}

static void timeToAttackRelease (float timeMs, float attackMsBase, float releaseMsBase,
                                 float& attackMsOut, float& releaseMsOut)
{
    const float tNorm = juce::jlimit (0.0f, 1.0f, (timeMs - 1.0f) / 499.0f);

    // More useful control in the fast region, broader spread in the slow region.
    const float attackMacro = 0.45f + std::pow (tNorm, 0.62f) * 1.85f;
    const float releaseMacro = 0.55f + std::pow (tNorm, 0.78f) * 2.75f;

    attackMsOut = juce::jlimit (0.05f, 35.0f, attackMsBase * attackMacro);
    releaseMsOut = juce::jlimit (8.0f, 1600.0f, releaseMsBase * releaseMacro);
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
void UpDownCompressorBand::prepare (double sampleRate, int channels)
{
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
                                    const float* driveValues,
                                    const float* balanceValues,
                                    const float* attackCoeffValues,
                                    const float* releaseCoeffValues,
                                    const float* stereoLinkValues,
                                    const float* bandTrimValues,
                                    std::atomic<float>& upMeterDb, std::atomic<float>& downMeterDb)
{
    constexpr float thresholdDb = -24.0f;
    constexpr float downRatio = 12.0f;
    constexpr float upRatio = 0.35f;
    constexpr float maxUpDb = 24.0f;

    const int samples = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();

    float upMeter = 0.0f;
    float downMeter = 0.0f;

    for (int i = 0; i < samples; ++i)
    {
        float linkedLevel = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
            linkedLevel = juce::jmax (linkedLevel, std::abs (buffer.getReadPointer (ch)[i]));

        const float attackCoeff = attackCoeffValues[i];
        const float releaseCoeff = releaseCoeffValues[i];
        const float stereoLink = stereoLinkValues[i];
        const float drive = driveValues[i];
        const float balance = balanceValues[i];
        const float bandTrimDb = bandTrimValues[i];
        const float downBias = juce::jmap (balance, 0.0f, 1.0f, 1.45f, 0.75f);
        const float upBias = juce::jmap (balance, 0.0f, 1.0f, 0.75f, 1.45f);
        const float downScale = juce::jlimit (0.0f, 3.4f, drive * 0.92f * downBias);
        const float upScale = juce::jlimit (0.0f, 4.2f, drive * 1.18f * upBias);

        float linkedEnvVal = linkedEnv.processSample (linkedLevel, attackCoeff, releaseCoeff);

        for (int ch = 0; ch < channels; ++ch)
        {
            float x = buffer.getWritePointer (ch)[i];
            float env = envs[(size_t) ch].processSample (x, attackCoeff, releaseCoeff);
            float usedEnv = juce::jmap (stereoLink, env, linkedEnvVal);

            const float levelDb = gainToDb (usedEnv + 1.0e-9f);
            float gDownDb = 0.0f;
            float gUpDb = 0.0f;

            if (levelDb > thresholdDb)
            {
                const float compressed = thresholdDb + (levelDb - thresholdDb) / downRatio;
                gDownDb = (compressed - levelDb) * downScale;
            }

            if (levelDb < thresholdDb)
            {
                const float expanded = thresholdDb + (levelDb - thresholdDb) * upRatio;
                gUpDb = (expanded - levelDb) * upScale;
                gUpDb = juce::jlimit (0.0f, maxUpDb, gUpDb);
            }

            float gTotalDb = juce::jlimit (-48.0f, 30.0f, gDownDb + gUpDb + bandTrimDb);
            buffer.getWritePointer (ch)[i] = x * dbToGain (gTotalDb);

            upMeter = juce::jmax (upMeter, gUpDb);
            downMeter = juce::jmin (downMeter, gDownDb);
        }
    }

    upMeterDb.store (upMeter, std::memory_order_relaxed);
    downMeterDb.store (downMeter, std::memory_order_relaxed);
}

//==============================================================================
void Crossover3Band::prepare (double sampleRate, int channels)
{
    sr = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = 4096;
    spec.numChannels = static_cast<juce::uint32> (channels);

    split1.prepare (spec);
    split2.prepare (spec);
    setFrequencies (f1Hz, f2Hz);
}

void Crossover3Band::reset()
{
    split1.reset();
    split2.reset();
}

void Crossover3Band::setFrequencies (float f1, float f2)
{
    f1Hz = f1;
    f2Hz = f2;
    split1.setCutoffFrequency (f1Hz);
    split2.setCutoffFrequency (f2Hz);
}

void Crossover3Band::process (const juce::AudioBuffer<float>& input,
                              juce::AudioBuffer<float>& low,
                              juce::AudioBuffer<float>& mid,
                              juce::AudioBuffer<float>& high,
                              const float* f1Values,
                              const float* f2Values)
{
    const int channels = input.getNumChannels();
    const int samples = input.getNumSamples();

    for (int i = 0; i < samples; ++i)
    {
        setFrequencies (f1Values[i], f2Values[i]);

        for (int ch = 0; ch < channels; ++ch)
        {
            const float in = input.getReadPointer (ch)[i];
            float lowSample = 0.0f;
            float highSample = 0.0f;
            split1.processSample (ch, in, lowSample, highSample);

            float midSample = 0.0f;
            float highSample2 = 0.0f;
            split2.processSample (ch, highSample, midSample, highSample2);

            low.getWritePointer (ch)[i] = lowSample;
            mid.getWritePointer (ch)[i] = midSample;
            high.getWritePointer (ch)[i] = highSample2;
        }
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
{
}

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
    lastSampleRate = sampleRate;
    maxBlockSize = samplesPerBlock;
    const int channels = juce::jmax (1, getTotalNumInputChannels());

    dryBuffer.setSize (channels, samplesPerBlock);
    lowBuffer.setSize (channels, samplesPerBlock);
    midBuffer.setSize (channels, samplesPerBlock);
    highBuffer.setSize (channels, samplesPerBlock);

    crossover.prepare (sampleRate, channels);
    lowComp.prepare (sampleRate, channels);
    midComp.prepare (sampleRate, channels);
    highComp.prepare (sampleRate, channels);

    preAnalyzer.prepare (sampleRate);

    amountSmoothed.reset (sampleRate, 0.03);
    mixSmoothed.reset (sampleRate, 0.03);
    outSmoothed.reset (sampleRate, 0.03);
    timeSmoothed.reset (sampleRate, 0.04);
    f1Smoothed.reset (sampleRate, 0.05);
    f2Smoothed.reset (sampleRate, 0.05);
    stereoLinkSmoothed.reset (sampleRate, 0.04);
    balanceSmoothed.reset (sampleRate, 0.04);
    attackMsSmoothed.reset (sampleRate, 0.04);
    releaseMsSmoothed.reset (sampleRate, 0.04);
    lowTrimSmoothed.reset (sampleRate, 0.03);
    midTrimSmoothed.reset (sampleRate, 0.03);
    highTrimSmoothed.reset (sampleRate, 0.03);
    autoGainBlendSmoothed.reset (sampleRate, 0.04);
    softClipBlendSmoothed.reset (sampleRate, 0.02);
    ceilingSmoothed.reset (sampleRate, 0.03);
    muteLowSmoothed.reset (sampleRate, 0.008);
    muteMidSmoothed.reset (sampleRate, 0.008);
    muteHighSmoothed.reset (sampleRate, 0.008);

    soloLowSmoothed.reset (sampleRate, 0.008);
    soloMidSmoothed.reset (sampleRate, 0.008);
    soloHighSmoothed.reset (sampleRate, 0.008);

    amountSmoothed.setCurrentAndTargetValue (apvts.getRawParameterValue ("amount")->load() * 0.01f);
    mixSmoothed.setCurrentAndTargetValue (apvts.getRawParameterValue ("mix")->load());
    outSmoothed.setCurrentAndTargetValue (apvts.getRawParameterValue ("out_gain_db")->load());
    timeSmoothed.setCurrentAndTargetValue (apvts.getRawParameterValue ("time_ms")->load());
    f1Smoothed.setCurrentAndTargetValue (apvts.getRawParameterValue ("xover_f1_hz")->load());
    f2Smoothed.setCurrentAndTargetValue (apvts.getRawParameterValue ("xover_f2_hz")->load());
    stereoLinkSmoothed.setCurrentAndTargetValue (apvts.getRawParameterValue ("stereo_link")->load() * 0.01f);
    balanceSmoothed.setCurrentAndTargetValue (apvts.getRawParameterValue ("updown_balance")->load() * 0.01f);
    attackMsSmoothed.setCurrentAndTargetValue (apvts.getRawParameterValue ("attack_ms")->load());
    releaseMsSmoothed.setCurrentAndTargetValue (apvts.getRawParameterValue ("release_ms")->load());
    lowTrimSmoothed.setCurrentAndTargetValue (apvts.getRawParameterValue ("low_gain_db")->load());
    midTrimSmoothed.setCurrentAndTargetValue (apvts.getRawParameterValue ("mid_gain_db")->load());
    highTrimSmoothed.setCurrentAndTargetValue (apvts.getRawParameterValue ("high_gain_db")->load());
    autoGainBlendSmoothed.setCurrentAndTargetValue (apvts.getRawParameterValue ("auto_gain")->load() > 0.5f ? 1.0f : 0.0f);
    softClipBlendSmoothed.setCurrentAndTargetValue (apvts.getRawParameterValue ("soft_clip")->load() > 0.5f ? 1.0f : 0.0f);
    ceilingSmoothed.setCurrentAndTargetValue (apvts.getRawParameterValue ("ceiling_db")->load());

    updateBandListenTargets (true);
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

    if (buffer.getNumSamples() > maxBlockSize)
    {
        maxBlockSize = buffer.getNumSamples();
        dryBuffer.setSize (totalNumInputChannels, maxBlockSize);
        lowBuffer.setSize (totalNumInputChannels, maxBlockSize);
        midBuffer.setSize (totalNumInputChannels, maxBlockSize);
        highBuffer.setSize (totalNumInputChannels, maxBlockSize);
    }

    updateSmoothedTargets();
    updateSoloTargets();
    ensureSmoothingBufferSize (buffer.getNumSamples());

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float amount = amountSmoothed.getNextValue();
        const float mix = mixSmoothed.getNextValue() * 0.01f;
        const float outDb = outSmoothed.getNextValue();
        const float autoGainBlend = autoGainBlendSmoothed.getNextValue();
        const float softClipBlend = softClipBlendSmoothed.getNextValue();
        const float ceilingDb = ceilingSmoothed.getNextValue();
        const float timeMs = timeSmoothed.getNextValue();
        const float attackMs = attackMsSmoothed.getNextValue();
        const float releaseMs = releaseMsSmoothed.getNextValue();
        float attack = 0.0f;
        float release = 0.0f;

        driveValues[(size_t) i] = amountToDrive (amount);
        mixValues[(size_t) i] = mix;
        const float autoGainCompDb = amountToAutoGainCompDb (amount, mix);
        outGainValues[(size_t) i] = dbToGain (outDb + autoGainCompDb * autoGainBlend);
        f1Values[(size_t) i] = f1Smoothed.getNextValue();
        f2Values[(size_t) i] = f2Smoothed.getNextValue();
        stereoLinkValues[(size_t) i] = stereoLinkSmoothed.getNextValue();
        balanceValues[(size_t) i] = balanceSmoothed.getNextValue();

        timeToAttackRelease (timeMs, attackMs, releaseMs, attack, release);
        attackCoeffValues[(size_t) i] = msToCoeff (attack, lastSampleRate);
        releaseCoeffValues[(size_t) i] = msToCoeff (release, lastSampleRate);

        lowTrimValues[(size_t) i] = lowTrimSmoothed.getNextValue();
        midTrimValues[(size_t) i] = midTrimSmoothed.getNextValue();
        highTrimValues[(size_t) i] = highTrimSmoothed.getNextValue();
        ceilingValues[(size_t) i] = dbToGain (ceilingDb);
        softClipBlendValues[(size_t) i] = softClipBlend;
        muteLowValues[(size_t) i] = muteLowSmoothed.getNextValue();
        muteMidValues[(size_t) i] = muteMidSmoothed.getNextValue();
        muteHighValues[(size_t) i] = muteHighSmoothed.getNextValue();
        soloLowValues[(size_t) i] = soloLowSmoothed.getNextValue();
        soloMidValues[(size_t) i] = soloMidSmoothed.getNextValue();
        soloHighValues[(size_t) i] = soloHighSmoothed.getNextValue();
    }

    for (int ch = 0; ch < totalNumInputChannels; ++ch)
        juce::FloatVectorOperations::copy (dryBuffer.getWritePointer (ch), buffer.getReadPointer (ch), buffer.getNumSamples());

    preAnalyzer.pushSamples (buffer.getReadPointer (0), buffer.getNumSamples());

    crossover.process (buffer, lowBuffer, midBuffer, highBuffer,
                       f1Values.data(), f2Values.data());

    lowComp.process (lowBuffer, driveValues.data(), balanceValues.data(), attackCoeffValues.data(), releaseCoeffValues.data(),
                     stereoLinkValues.data(), lowTrimValues.data(), meters.bandUpGRdB[0], meters.bandDownGRdB[0]);
    midComp.process (midBuffer, driveValues.data(), balanceValues.data(), attackCoeffValues.data(), releaseCoeffValues.data(),
                     stereoLinkValues.data(), midTrimValues.data(), meters.bandUpGRdB[1], meters.bandDownGRdB[1]);
    highComp.process (highBuffer, driveValues.data(), balanceValues.data(), attackCoeffValues.data(), releaseCoeffValues.data(),
                      stereoLinkValues.data(), highTrimValues.data(), meters.bandUpGRdB[2], meters.bandDownGRdB[2]);

    for (int ch = 0; ch < totalNumInputChannels; ++ch)
    {
        auto* low = lowBuffer.getReadPointer (ch);
        auto* mid = midBuffer.getReadPointer (ch);
        auto* high = highBuffer.getReadPointer (ch);
        auto* dst = buffer.getWritePointer (ch);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            dst[i] = low[i] * soloLowValues[(size_t) i] * muteLowValues[(size_t) i]
                   + mid[i] * soloMidValues[(size_t) i] * muteMidValues[(size_t) i]
                   + high[i] * soloHighValues[(size_t) i] * muteHighValues[(size_t) i];
    }

    for (int ch = 0; ch < totalNumInputChannels; ++ch)
    {
        auto* wet = buffer.getWritePointer (ch);
        auto* dry = dryBuffer.getReadPointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            wet[i] = dry[i] * (1.0f - mixValues[(size_t) i]) + wet[i] * mixValues[(size_t) i];
    }

    for (int ch = 0; ch < totalNumInputChannels; ++ch)
    {
        auto* wet = buffer.getWritePointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float gained = wet[i] * outGainValues[(size_t) i];
            const float clipped = applySoftClipSample (gained, ceilingValues[(size_t) i]);
            wet[i] = juce::jmap (softClipBlendValues[(size_t) i], gained, clipped);
        }
    }

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
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void S3xtaOTTAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
bool S3xtaOTTAudioProcessor::getPreFFTData (std::array<float, FFTDataGenerator::fftSize / 2>& dest) const
{
    return preAnalyzer.getLatestFFTData (dest);
}

void S3xtaOTTAudioProcessor::updateSmoothedTargets()
{
    const float amount = apvts.getRawParameterValue ("amount")->load() * 0.01f;
    const float mix = apvts.getRawParameterValue ("mix")->load();
    const float outDb = apvts.getRawParameterValue ("out_gain_db")->load();
    const float timeMs = apvts.getRawParameterValue ("time_ms")->load();
    const float f1 = apvts.getRawParameterValue ("xover_f1_hz")->load();
    const float f2 = apvts.getRawParameterValue ("xover_f2_hz")->load();
    const float stereoLink = apvts.getRawParameterValue ("stereo_link")->load() * 0.01f;
    const float balance = apvts.getRawParameterValue ("updown_balance")->load() * 0.01f;
    const float attackMs = apvts.getRawParameterValue ("attack_ms")->load();
    const float releaseMs = apvts.getRawParameterValue ("release_ms")->load();
    const float lowTrim = apvts.getRawParameterValue ("low_gain_db")->load();
    const float midTrim = apvts.getRawParameterValue ("mid_gain_db")->load();
    const float highTrim = apvts.getRawParameterValue ("high_gain_db")->load();
    const float autoGainBlend = apvts.getRawParameterValue ("auto_gain")->load() > 0.5f ? 1.0f : 0.0f;
    const float softClipBlend = apvts.getRawParameterValue ("soft_clip")->load() > 0.5f ? 1.0f : 0.0f;
    const float ceilingDb = apvts.getRawParameterValue ("ceiling_db")->load();

    amountSmoothed.setTargetValue (amount);
    mixSmoothed.setTargetValue (mix);
    outSmoothed.setTargetValue (outDb);
    timeSmoothed.setTargetValue (timeMs);
    f1Smoothed.setTargetValue (f1);
    f2Smoothed.setTargetValue (f2);
    stereoLinkSmoothed.setTargetValue (stereoLink);
    balanceSmoothed.setTargetValue (balance);
    attackMsSmoothed.setTargetValue (attackMs);
    releaseMsSmoothed.setTargetValue (releaseMs);
    lowTrimSmoothed.setTargetValue (lowTrim);
    midTrimSmoothed.setTargetValue (midTrim);
    highTrimSmoothed.setTargetValue (highTrim);
    autoGainBlendSmoothed.setTargetValue (autoGainBlend);
    softClipBlendSmoothed.setTargetValue (softClipBlend);
    ceilingSmoothed.setTargetValue (ceilingDb);
}

void S3xtaOTTAudioProcessor::updateSoloTargets()
{
    updateBandListenTargets (false);
}

void S3xtaOTTAudioProcessor::updateBandListenTargets (bool useCurrentValues)
{
    const bool soloLow = apvts.getRawParameterValue ("solo_low")->load() > 0.5f;
    const bool soloMid = apvts.getRawParameterValue ("solo_mid")->load() > 0.5f;
    const bool soloHigh = apvts.getRawParameterValue ("solo_high")->load() > 0.5f;
    const bool muteLow = apvts.getRawParameterValue ("mute_low")->load() > 0.5f;
    const bool muteMid = apvts.getRawParameterValue ("mute_mid")->load() > 0.5f;
    const bool muteHigh = apvts.getRawParameterValue ("mute_high")->load() > 0.5f;

    float soloLowTarget = 1.0f;
    float soloMidTarget = 1.0f;
    float soloHighTarget = 1.0f;

    if (soloLow || soloMid || soloHigh)
    {
        soloLowTarget = soloLow ? 1.0f : 0.0f;
        soloMidTarget = (! soloLow && soloMid) ? 1.0f : 0.0f;
        soloHighTarget = (! soloLow && ! soloMid && soloHigh) ? 1.0f : 0.0f;
    }

    const float muteLowTarget = muteLow ? 0.0f : 1.0f;
    const float muteMidTarget = muteMid ? 0.0f : 1.0f;
    const float muteHighTarget = muteHigh ? 0.0f : 1.0f;

    if (useCurrentValues)
    {
        soloLowSmoothed.setCurrentAndTargetValue (soloLowTarget);
        soloMidSmoothed.setCurrentAndTargetValue (soloMidTarget);
        soloHighSmoothed.setCurrentAndTargetValue (soloHighTarget);
        muteLowSmoothed.setCurrentAndTargetValue (muteLowTarget);
        muteMidSmoothed.setCurrentAndTargetValue (muteMidTarget);
        muteHighSmoothed.setCurrentAndTargetValue (muteHighTarget);
        return;
    }

    soloLowSmoothed.setTargetValue (soloLowTarget);
    soloMidSmoothed.setTargetValue (soloMidTarget);
    soloHighSmoothed.setTargetValue (soloHighTarget);
    muteLowSmoothed.setTargetValue (muteLowTarget);
    muteMidSmoothed.setTargetValue (muteMidTarget);
    muteHighSmoothed.setTargetValue (muteHighTarget);
}

void S3xtaOTTAudioProcessor::ensureSmoothingBufferSize (int numSamples)
{
    const auto requiredSize = static_cast<size_t> (numSamples);
    driveValues.resize (requiredSize);
    mixValues.resize (requiredSize);
    outGainValues.resize (requiredSize);
    f1Values.resize (requiredSize);
    f2Values.resize (requiredSize);
    stereoLinkValues.resize (requiredSize);
    balanceValues.resize (requiredSize);
    attackCoeffValues.resize (requiredSize);
    releaseCoeffValues.resize (requiredSize);
    lowTrimValues.resize (requiredSize);
    midTrimValues.resize (requiredSize);
    highTrimValues.resize (requiredSize);
    ceilingValues.resize (requiredSize);
    softClipBlendValues.resize (requiredSize);
    muteLowValues.resize (requiredSize);
    muteMidValues.resize (requiredSize);
    muteHighValues.resize (requiredSize);
    soloLowValues.resize (requiredSize);
    soloMidValues.resize (requiredSize);
    soloHighValues.resize (requiredSize);
}

//==============================================================================
S3xtaOTTAudioProcessor::APVTS::ParameterLayout S3xtaOTTAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> ("amount", "Amount",
        juce::NormalisableRange<float> (0.0f, 150.0f, 0.01f, 0.58f), 65.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("time_ms", "Time",
        juce::NormalisableRange<float> (1.0f, 500.0f, 0.01f, 0.33f), 85.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("mix", "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 100.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("out_gain_db", "Out Gain",
        juce::NormalisableRange<float> (-18.0f, 18.0f, 0.01f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("auto_gain", "Auto Gain", true));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("soft_clip", "Soft Clip", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("ceiling_db", "Ceiling",
        juce::NormalisableRange<float> (-12.0f, 0.0f, 0.01f), -0.5f));

    params.push_back (std::make_unique<juce::AudioParameterBool> ("solo_low", "Solo Low", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("solo_mid", "Solo Mid", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("solo_high", "Solo High", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("mute_low", "Mute Low", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("mute_mid", "Mute Mid", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("mute_high", "Mute High", false));

    params.push_back (std::make_unique<juce::AudioParameterFloat> ("xover_f1_hz", "Xover F1",
        juce::NormalisableRange<float> (60.0f, 500.0f, 0.01f, 0.4f), 120.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("xover_f2_hz", "Xover F2",
        juce::NormalisableRange<float> (800.0f, 8000.0f, 0.01f, 0.4f), 2500.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("stereo_link", "Stereo Link",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 85.0f));
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

    return { params.begin(), params.end() };
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new S3xtaOTTAudioProcessor();
}
