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
    std::array<std::atomic<float>, 5> bandUpGRdB {};
    std::array<std::atomic<float>, 5> bandDownGRdB {};
    std::atomic<float> outPeakL_dBFS { -100.0f };
    std::atomic<float> outPeakR_dBFS { -100.0f };
    std::atomic<float> inPeakL_dBFS { -100.0f };
    std::atomic<float> inPeakR_dBFS { -100.0f };
    std::atomic<bool> clipped { false };
};

//==============================================================================
class EnvelopeFollower
{
public:
    void prepare (double sampleRate);
    float processSample (float input, float attackCoeff, float releaseCoeff);

    void reset() { env = 0.0f; }

private:
    float env = 0.0f;
};

//==============================================================================
class UpDownCompressorBand
{
public:
    void prepare (double sampleRate, int channels, int bandIndex);
    void reset();

    void process (juce::AudioBuffer<float>& buffer, float amount, float expander,
                  float upward, float downward, float timeScale, float thresholdOffsetDb,
                  float stereoLink, float bandTrimDb,
                  std::atomic<float>& upMeterDb, std::atomic<float>& downMeterDb);

private:
    int numChannels = 2;
    double sr = 44100.0;
    int profileIndex = 0;

    EnvelopeFollower linkedEnv;
    std::vector<EnvelopeFollower> envs;
};

//==============================================================================
class LinkwitzRiley2
{
public:
    void prepare (double sampleRate, int channels);
    void reset();
    void setFrequency (float frequency);
    void processSplit (int channel, float input, float& low, float& high);
    float processAllpass (int channel, float input);

private:
    std::array<juce::dsp::FirstOrderTPTFilter<float>, 2> lowStages;
    std::array<juce::dsp::FirstOrderTPTFilter<float>, 2> highStages;
};

//==============================================================================
class Crossover5Band
{
public:
    void prepare (double sampleRate, int channels);
    void reset();
    void setFrequencies (const std::array<float, 4>& frequencies);
    void process (const juce::AudioBuffer<float>& input,
                  std::array<juce::AudioBuffer<float>, 5>& bands);
    void applyPhaseCompensation (std::array<juce::AudioBuffer<float>, 5>& bands);
    void processFullRangeAllpass (const juce::AudioBuffer<float>& input,
                                  juce::AudioBuffer<float>& output);

private:
    double sr = 44100.0;
    std::array<float, 4> frequencies { 212.0f, 637.0f, 3012.0f, 5394.0f };
    std::array<LinkwitzRiley2, 4> splits;
    std::array<std::array<LinkwitzRiley2, 4>, 5> phaseCompensators;
    std::array<LinkwitzRiley2, 4> fullRangeCompensators;
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
    bool getPreFFTData (std::array<float, FFTDataGenerator::fftSize / 2>& dest) const;

    static APVTS::ParameterLayout createParameterLayout();

private:
    //==============================================================================
    APVTS apvts;
    MeterState meters;

    std::array<juce::AudioBuffer<float>, 5> bandBuffers;
    std::array<juce::AudioBuffer<float>, 5> dryBandBuffers;
    juce::AudioBuffer<float> alignedDryBuffer;
    Crossover5Band crossover;
    std::array<UpDownCompressorBand, 5> bandProcessors;

    AnalyzerFIFO preAnalyzer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (S3xtaOTTAudioProcessor)
};
