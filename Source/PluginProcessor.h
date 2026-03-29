/*
  ==============================================================================

    JUCE plugin processor for Multiband Up/Down Compressor (OTT-style)

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>

//==============================================================================
struct MeterState
{
    std::array<std::atomic<float>, 3> bandUpGRdB { { 0.0f, 0.0f, 0.0f } };
    std::array<std::atomic<float>, 3> bandDownGRdB { { 0.0f, 0.0f, 0.0f } };
    std::atomic<float> outPeakL_dBFS { -100.0f };
    std::atomic<float> outPeakR_dBFS { -100.0f };
    std::atomic<bool> clipped { false };
};

//==============================================================================
class EnvelopeFollower
{
public:
    void prepare (double sampleRate);
    void setAttackRelease (float attackMs, float releaseMs);
    float processSample (float input);

    void reset() { env = 0.0f; }

private:
    double sr = 44100.0;
    float env = 0.0f;
    float attackCoeff = 0.0f;
    float releaseCoeff = 0.0f;
};

//==============================================================================
class UpDownCompressorBand
{
public:
    void prepare (double sampleRate, int channels);
    void reset();

    void setDetectorParams (float attackMs, float releaseMs, float stereoLink01);
    void setAmount (float drive);
    void process (juce::AudioBuffer<float>& buffer, float bandTrimDb,
                  std::atomic<float>& upMeterDb, std::atomic<float>& downMeterDb);

private:
    double sr = 44100.0;
    int numChannels = 2;

    float drive = 0.5f;
    float stereoLink = 1.0f;

    EnvelopeFollower linkedEnv;
    std::vector<EnvelopeFollower> envs;
};

//==============================================================================
class Crossover3Band
{
public:
    void prepare (double sampleRate, int channels);
    void reset();
    void setFrequencies (float f1, float f2);

    void process (const juce::AudioBuffer<float>& input,
                  juce::AudioBuffer<float>& low,
                  juce::AudioBuffer<float>& mid,
                  juce::AudioBuffer<float>& high);

private:
    double sr = 44100.0;
    float f1Hz = 120.0f;
    float f2Hz = 2000.0f;

    juce::dsp::LinkwitzRileyFilter<float> split1;
    juce::dsp::LinkwitzRileyFilter<float> split2;
};

//==============================================================================
class FFTDataGenerator
{
public:
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;

    void prepare (double sampleRate);
    void produceFFTDataForRendering (const float* data, size_t numSamples);
    bool getLatestFFTData (std::array<float, fftSize / 2>& dest) const;

private:
    double sr = 44100.0;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { fftSize, juce::dsp::WindowingFunction<float>::hann };

    std::array<float, fftSize * 2> fftBuffer {};
    std::array<float, fftSize / 2> dataBuffers[2] {};
    std::array<float, fftSize / 2> smoothBuffer {};
    std::atomic<int> readIndex { 0 };
    std::atomic<bool> hasData { false };
};

//==============================================================================
class AnalyzerFIFO
{
public:
    void prepare (double sampleRate);
    void pushSamples (const float* data, int numSamples);
    bool getLatestFFTData (std::array<float, FFTDataGenerator::fftSize / 2>& dest) const;

private:
    FFTDataGenerator generator;
    std::array<float, FFTDataGenerator::fftSize> fifo {};
    int fifoIndex = 0;
};

//==============================================================================
class S3xtaOTTAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    S3xtaOTTAudioProcessor();
    ~S3xtaOTTAudioProcessor() override;

    using APVTS = juce::AudioProcessorValueTreeState;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    APVTS& getAPVTS() { return apvts; }
    MeterState& getMeterState() { return meters; }
    const MeterState& getMeterState() const { return meters; }
    void resetClip() { meters.clipped.store (false, std::memory_order_relaxed); }

    bool getPreFFTData (std::array<float, FFTDataGenerator::fftSize / 2>& dest) const;
    bool getPostFFTData (std::array<float, FFTDataGenerator::fftSize / 2>& dest) const;

    static APVTS::ParameterLayout createParameterLayout();

private:
    //==============================================================================
    void updateSmoothedTargets();
    void updateCrossoverFrequencies();
    void updateSoloTargets();

    //==============================================================================
    APVTS apvts;
    MeterState meters;

    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> lowBuffer;
    juce::AudioBuffer<float> midBuffer;
    juce::AudioBuffer<float> highBuffer;

    Crossover3Band crossover;
    UpDownCompressorBand lowComp;
    UpDownCompressorBand midComp;
    UpDownCompressorBand highComp;

    AnalyzerFIFO preAnalyzer;
    AnalyzerFIFO postAnalyzer;

    int maxBlockSize = 0;
    double lastSampleRate = 44100.0;

    juce::LinearSmoothedValue<float> amountSmoothed;
    juce::LinearSmoothedValue<float> mixSmoothed;
    juce::LinearSmoothedValue<float> outSmoothed;
    juce::LinearSmoothedValue<float> f1Smoothed;
    juce::LinearSmoothedValue<float> f2Smoothed;
    juce::LinearSmoothedValue<float> soloLowSmoothed;
    juce::LinearSmoothedValue<float> soloMidSmoothed;
    juce::LinearSmoothedValue<float> soloHighSmoothed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (S3xtaOTTAudioProcessor)
};
