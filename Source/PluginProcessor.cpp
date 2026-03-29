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

//==============================================================================
void EnvelopeFollower::prepare (double sampleRate)
{
    sr = sampleRate;
    reset();
}

void EnvelopeFollower::setAttackRelease (float attackMs, float releaseMs)
{
    attackMs = juce::jmax (0.01f, attackMs);
    releaseMs = juce::jmax (0.01f, releaseMs);
    attackCoeff = std::exp (-1.0f / (0.001f * attackMs * static_cast<float> (sr)));
    releaseCoeff = std::exp (-1.0f / (0.001f * releaseMs * static_cast<float> (sr)));
}

float EnvelopeFollower::processSample (float input)
{
    const float x = std::abs (input);
    if (x > env)
        env = attackCoeff * env + (1.0f - attackCoeff) * x;
    else
        env = releaseCoeff * env + (1.0f - releaseCoeff) * x;

    return env;
}

//==============================================================================
void UpDownCompressorBand::prepare (double sampleRate, int channels)
{
    sr = sampleRate;
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

void UpDownCompressorBand::setDetectorParams (float attackMs, float releaseMs, float stereoLink01)
{
    stereoLink = juce::jlimit (0.0f, 1.0f, stereoLink01);
    linkedEnv.setAttackRelease (attackMs, releaseMs);
    for (auto& env : envs)
        env.setAttackRelease (attackMs, releaseMs);
}

void UpDownCompressorBand::setAmount (float newDrive)
{
    drive = juce::jlimit (0.0f, 2.8f, newDrive);
}

void UpDownCompressorBand::process (juce::AudioBuffer<float>& buffer, float bandTrimDb,
                                    std::atomic<float>& upMeterDb, std::atomic<float>& downMeterDb)
{
    constexpr float thresholdDb = -30.0f;
    constexpr float downRatio = 10.0f;
    constexpr float upRatio = 0.25f;
    constexpr float maxUpDb = 30.0f;

    const float downScale = drive;
    const float upScale = drive;

    const int samples = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();

    float upMeter = 0.0f;
    float downMeter = 0.0f;

    for (int i = 0; i < samples; ++i)
    {
        float linkedLevel = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
            linkedLevel = juce::jmax (linkedLevel, std::abs (buffer.getReadPointer (ch)[i]));

        float linkedEnvVal = linkedEnv.processSample (linkedLevel);

        for (int ch = 0; ch < channels; ++ch)
        {
            float x = buffer.getWritePointer (ch)[i];
            float env = envs[(size_t) ch].processSample (x);
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
                              juce::AudioBuffer<float>& high)
{
    const int channels = input.getNumChannels();
    const int samples = input.getNumSamples();

    for (int ch = 0; ch < channels; ++ch)
    {
        const float* in = input.getReadPointer (ch);
        float* lowOut = low.getWritePointer (ch);
        float* midOut = mid.getWritePointer (ch);
        float* highOut = high.getWritePointer (ch);

        for (int i = 0; i < samples; ++i)
        {
            float lowSample = 0.0f;
            float highSample = 0.0f;
            split1.processSample (ch, in[i], lowSample, highSample);

            float midSample = 0.0f;
            float highSample2 = 0.0f;
            split2.processSample (ch, highSample, midSample, highSample2);

            lowOut[i] = lowSample;
            midOut[i] = midSample;
            highOut[i] = highSample2;
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
    postAnalyzer.prepare (sampleRate);

    amountSmoothed.reset (sampleRate, 0.03);
    mixSmoothed.reset (sampleRate, 0.03);
    outSmoothed.reset (sampleRate, 0.03);
    f1Smoothed.reset (sampleRate, 0.05);
    f2Smoothed.reset (sampleRate, 0.05);

    soloLowSmoothed.reset (sampleRate, 0.008);
    soloMidSmoothed.reset (sampleRate, 0.008);
    soloHighSmoothed.reset (sampleRate, 0.008);

    updateSmoothedTargets();
    updateSoloTargets();
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
    updateCrossoverFrequencies();
    updateSoloTargets();

    const float amount = amountSmoothed.getNextValue();
    const float mix = mixSmoothed.getNextValue() * 0.01f;
    const float outGain = dbToGain (outSmoothed.getNextValue());

    const float timeMs = apvts.getRawParameterValue ("time_ms")->load();
    const float attackMs = apvts.getRawParameterValue ("attack_ms")->load();
    const float releaseMs = apvts.getRawParameterValue ("release_ms")->load();
    const float stereoLink = apvts.getRawParameterValue ("stereo_link")->load() * 0.01f;

    const float macro = timeMs / 95.0f;
    const float attack = juce::jlimit (0.05f, 35.0f, attackMs * macro);
    const float release = juce::jlimit (5.0f, 1200.0f, releaseMs * macro);

    const float drive = juce::jlimit (0.0f, 2.8f, std::pow (amount, 2.0f) * 1.35f);

    const float lowTrim = apvts.getRawParameterValue ("low_gain_db")->load();
    const float midTrim = apvts.getRawParameterValue ("mid_gain_db")->load();
    const float highTrim = apvts.getRawParameterValue ("high_gain_db")->load();

    for (int ch = 0; ch < totalNumInputChannels; ++ch)
        juce::FloatVectorOperations::copy (dryBuffer.getWritePointer (ch), buffer.getReadPointer (ch), buffer.getNumSamples());

    preAnalyzer.pushSamples (buffer.getReadPointer (0), buffer.getNumSamples());

    crossover.process (buffer, lowBuffer, midBuffer, highBuffer);

    lowComp.setDetectorParams (attack, release, stereoLink);
    midComp.setDetectorParams (attack, release, stereoLink);
    highComp.setDetectorParams (attack, release, stereoLink);

    lowComp.setAmount (drive);
    midComp.setAmount (drive);
    highComp.setAmount (drive);

    lowComp.process (lowBuffer, lowTrim, meters.bandUpGRdB[0], meters.bandDownGRdB[0]);
    midComp.process (midBuffer, midTrim, meters.bandUpGRdB[1], meters.bandDownGRdB[1]);
    highComp.process (highBuffer, highTrim, meters.bandUpGRdB[2], meters.bandDownGRdB[2]);

    const float soloLow = soloLowSmoothed.getNextValue();
    const float soloMid = soloMidSmoothed.getNextValue();
    const float soloHigh = soloHighSmoothed.getNextValue();

    for (int ch = 0; ch < totalNumInputChannels; ++ch)
    {
        auto* low = lowBuffer.getReadPointer (ch);
        auto* mid = midBuffer.getReadPointer (ch);
        auto* high = highBuffer.getReadPointer (ch);
        auto* dst = buffer.getWritePointer (ch);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            dst[i] = low[i] * soloLow + mid[i] * soloMid + high[i] * soloHigh;
    }

    for (int ch = 0; ch < totalNumInputChannels; ++ch)
    {
        auto* wet = buffer.getWritePointer (ch);
        auto* dry = dryBuffer.getReadPointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            wet[i] = dry[i] * (1.0f - mix) + wet[i] * mix;
    }

    buffer.applyGain (outGain);

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
    if (peakL >= 1.0f || peakR >= 1.0f)
        meters.clipped.store (true, std::memory_order_relaxed);

    postAnalyzer.pushSamples (buffer.getReadPointer (0), buffer.getNumSamples());
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

bool S3xtaOTTAudioProcessor::getPostFFTData (std::array<float, FFTDataGenerator::fftSize / 2>& dest) const
{
    return postAnalyzer.getLatestFFTData (dest);
}

//==============================================================================
void S3xtaOTTAudioProcessor::updateSmoothedTargets()
{
    const float amount = apvts.getRawParameterValue ("amount")->load() * 0.01f;
    const float mix = apvts.getRawParameterValue ("mix")->load();
    const float outDb = apvts.getRawParameterValue ("out_gain_db")->load();
    const float f1 = apvts.getRawParameterValue ("xover_f1_hz")->load();
    const float f2 = apvts.getRawParameterValue ("xover_f2_hz")->load();

    amountSmoothed.setCurrentAndTargetValue (amount);
    mixSmoothed.setCurrentAndTargetValue (mix);
    outSmoothed.setCurrentAndTargetValue (outDb);
    f1Smoothed.setCurrentAndTargetValue (f1);
    f2Smoothed.setCurrentAndTargetValue (f2);
}

void S3xtaOTTAudioProcessor::updateCrossoverFrequencies()
{
    const float f1 = f1Smoothed.getNextValue();
    const float f2 = f2Smoothed.getNextValue();
    crossover.setFrequencies (f1, f2);
}

void S3xtaOTTAudioProcessor::updateSoloTargets()
{
    const bool soloLow = apvts.getRawParameterValue ("solo_low")->load() > 0.5f;
    const bool soloMid = apvts.getRawParameterValue ("solo_mid")->load() > 0.5f;
    const bool soloHigh = apvts.getRawParameterValue ("solo_high")->load() > 0.5f;

    float lowTarget = 1.0f;
    float midTarget = 1.0f;
    float highTarget = 1.0f;

    if (soloLow || soloMid || soloHigh)
    {
        lowTarget = soloLow ? 1.0f : 0.0f;
        midTarget = (! soloLow && soloMid) ? 1.0f : 0.0f;
        highTarget = (! soloLow && ! soloMid && soloHigh) ? 1.0f : 0.0f;
    }

    soloLowSmoothed.setCurrentAndTargetValue (lowTarget);
    soloMidSmoothed.setCurrentAndTargetValue (midTarget);
    soloHighSmoothed.setCurrentAndTargetValue (highTarget);
}

//==============================================================================
S3xtaOTTAudioProcessor::APVTS::ParameterLayout S3xtaOTTAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> ("amount", "Amount",
        juce::NormalisableRange<float> (0.0f, 150.0f, 0.01f, 0.6f), 50.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("time_ms", "Time",
        juce::NormalisableRange<float> (1.0f, 500.0f, 0.01f, 0.35f), 110.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("mix", "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 50.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("out_gain_db", "Out Gain",
        juce::NormalisableRange<float> (-18.0f, 18.0f, 0.01f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterBool> ("solo_low", "Solo Low", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("solo_mid", "Solo Mid", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("solo_high", "Solo High", false));

    params.push_back (std::make_unique<juce::AudioParameterFloat> ("xover_f1_hz", "Xover F1",
        juce::NormalisableRange<float> (60.0f, 500.0f, 0.01f, 0.4f), 120.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("xover_f2_hz", "Xover F2",
        juce::NormalisableRange<float> (800.0f, 8000.0f, 0.01f, 0.4f), 2000.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("stereo_link", "Stereo Link",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 100.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("attack_ms", "Attack",
        juce::NormalisableRange<float> (0.1f, 50.0f, 0.01f, 0.35f), 1.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("release_ms", "Release",
        juce::NormalisableRange<float> (10.0f, 2000.0f, 0.01f, 0.35f), 150.0f));

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
