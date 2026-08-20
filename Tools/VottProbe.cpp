#include <JuceHeader.h>
#include <iostream>

namespace
{
juce::String parameterId (juce::AudioProcessorParameter* parameter)
{
    if (auto* identified = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
        return identified->paramID;
    return {};
}

void dumpParameters (juce::AudioPluginInstance& plugin)
{
    std::cout << "PARAMETER_COUNT=" << plugin.getParameters().size() << "\n";
    for (int index = 0; index < plugin.getParameters().size(); ++index)
    {
        auto* p = plugin.getParameters()[index];
        std::cout << "PARAM\t" << index << "\t" << parameterId (p) << "\t"
                  << p->getName (128) << "\tdefault=" << p->getDefaultValue()
                  << "\tcurrent=" << p->getValue() << "\tsteps=" << p->getNumSteps()
                  << "\tdiscrete=" << (p->isDiscrete() ? 1 : 0)
                  << "\tautomatable=" << (p->isAutomatable() ? 1 : 0) << "\n";

        for (float normalised : { 0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 0.9f, 1.0f })
            std::cout << "TEXT\t" << index << "\t" << normalised << "\t"
                      << p->getText (normalised, 128) << "\n";
    }
}

juce::AudioProcessorParameter* findParameter (juce::AudioPluginInstance& plugin, const juce::String& name)
{
    for (auto* p : plugin.getParameters())
        if (p->getName (128).equalsIgnoreCase (name))
            return p;
    return nullptr;
}

void setParameter (juce::AudioPluginInstance& plugin, const juce::String& name, float normalised)
{
    if (auto* p = findParameter (plugin, name))
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));
}

void writeRawFloat (const juce::File& file, const std::vector<float>& data)
{
    file.deleteFile();
    if (auto stream = file.createOutputStream())
        stream->write (data.data(), data.size() * sizeof (float));
}

void prepareReferenceState (juce::AudioPluginInstance& plugin)
{
    setParameter (plugin, "Bypass", 0.0f);
    setParameter (plugin, "MS Mode On", 0.0f);
    setParameter (plugin, "Stereo Linking", 1.0f);
    setParameter (plugin, "Input Gain", 0.5f);
    setParameter (plugin, "Output Gain", 0.5f);
    setParameter (plugin, "Mix", 1.0f);
    setParameter (plugin, "Time", 0.5f);
    setParameter (plugin, "Amount", 0.0f);
    setParameter (plugin, "Expander", 0.0f);
    setParameter (plugin, "Upward", 0.0f);
    setParameter (plugin, "Downward", 0.0f);
    for (auto band : { "Body", "Warm", "Clear", "Presence", "Air" })
    {
        setParameter (plugin, juce::String (band) + " Threshold", 0.5f);
        setParameter (plugin, juce::String (band) + " Makeup", 0.5f);
        setParameter (plugin, juce::String (band) + " Mute", 0.0f);
        setParameter (plugin, juce::String (band) + " Solo", 0.0f);
        setParameter (plugin, juce::String (band) + " Bypass", 0.0f);
    }
    for (auto drift : { "Low Freq Drift", "Mid Freq Drift", "High Freq Drift", "Air Freq Drift" })
        setParameter (plugin, drift, 0.5f);
}

void measureBandImpulses (juce::AudioPluginInstance& plugin, const juce::File& outputDirectory)
{
    constexpr int blockSize = 512;
    constexpr int totalSamples = 65536;
    juce::MidiBuffer midi;
    const std::array<const char*, 5> bands { "Body", "Warm", "Clear", "Presence", "Air" };

    for (int pass = -1; pass < (int) bands.size(); ++pass)
    {
        plugin.releaseResources();
        plugin.prepareToPlay (48000.0, blockSize);
        prepareReferenceState (plugin);
        const juce::String band = pass < 0 ? juce::String ("Full") : juce::String (bands[(size_t) pass]);
        if (pass >= 0)
            setParameter (plugin, band + " Solo", 1.0f);
        plugin.reset();

        // VO-TT smooths band-listen states. Let the requested state settle
        // before the measurement impulse.
        for (int warmup = 0; warmup < 100; ++warmup)
        {
            juce::AudioBuffer<float> silence (2, blockSize);
            silence.clear();
            plugin.processBlock (silence, midi);
        }

        std::vector<float> result ((size_t) totalSamples, 0.0f);
        for (int offset = 0; offset < totalSamples; offset += blockSize)
        {
            juce::AudioBuffer<float> audio (2, blockSize);
            audio.clear();
            if (offset == 0)
                audio.setSample (0, 0, 1.0f), audio.setSample (1, 0, 1.0f);
            plugin.processBlock (audio, midi);
            std::copy_n (audio.getReadPointer (0), blockSize, result.begin() + offset);
        }
        writeRawFloat (outputDirectory.getChildFile (band.toLowerCase() + "-impulse-f32.raw"), result);
    }
}

float measureSineGainDb (juce::AudioPluginInstance& plugin, float frequency, float inputDb)
{
    constexpr int blockSize = 512;
    constexpr int blocks = 100;
    const float inputGain = juce::Decibels::decibelsToGain (inputDb);
    double outputEnergy = 0.0, inputEnergy = 0.0;
    int measuredSamples = 0;
    double phase = 0.0;
    const double phaseStep = juce::MathConstants<double>::twoPi * frequency / 48000.0;
    juce::MidiBuffer midi;
    plugin.reset();
    for (int block = 0; block < blocks; ++block)
    {
        juce::AudioBuffer<float> audio (2, blockSize);
        for (int i = 0; i < blockSize; ++i)
        {
            const float x = inputGain * std::sin ((float) phase);
            phase += phaseStep;
            audio.setSample (0, i, x);
            audio.setSample (1, i, x);
            if (block >= blocks / 2)
                inputEnergy += (double) x * x;
        }
        plugin.processBlock (audio, midi);
        if (block >= blocks / 2)
            for (int i = 0; i < blockSize; ++i)
            {
                const float y = audio.getSample (0, i);
                outputEnergy += (double) y * y;
                ++measuredSamples;
            }
    }
    juce::ignoreUnused (measuredSamples);
    return (float) (10.0 * std::log10 (juce::jmax (1.0e-30, outputEnergy / inputEnergy)));
}

void measureDynamics (juce::AudioPluginInstance& plugin)
{
    const std::array<const char*, 5> bands { "Body", "Warm", "Clear", "Presence", "Air" };
    const std::array<float, 5> frequencies { 70.0f, 260.0f, 1100.0f, 3600.0f, 11000.0f };
    const std::array<const char*, 6> modes { "Baseline", "AmountOnly", "AmountMax", "Expander", "Upward", "Downward" };
    plugin.releaseResources();
    plugin.prepareToPlay (48000.0, 512);

    for (size_t bandIndex = 0; bandIndex < bands.size(); ++bandIndex)
        for (auto mode : modes)
        {
            prepareReferenceState (plugin);
            setParameter (plugin, juce::String (bands[bandIndex]) + " Solo", 1.0f);
            if (juce::String (mode) != "Baseline")
            {
                setParameter (plugin, "Amount", juce::String (mode) == "AmountMax" ? 1.0f : 0.5f);
                if (juce::String (mode) != "AmountOnly" && juce::String (mode) != "AmountMax")
                    setParameter (plugin, mode, 0.5f);
            }
            for (float level = -72.0f; level <= 0.1f; level += 6.0f)
                std::cout << "CURVE\t" << bands[bandIndex] << "\t" << mode << "\t"
                          << frequencies[bandIndex] << "\t" << level << "\t"
                          << measureSineGainDb (plugin, frequencies[bandIndex], level) << "\n";
        }
}

void measureDefaultCurve (juce::AudioPluginInstance& plugin)
{
    plugin.releaseResources();
    plugin.prepareToPlay (48000.0, 512);
    prepareReferenceState (plugin);
    setParameter (plugin, "Amount", 0.5f);
    setParameter (plugin, "Expander", 0.5f);
    setParameter (plugin, "Upward", 0.5f);
    setParameter (plugin, "Downward", 0.5f);

    for (float level : { -36.0f, -18.0f, -6.0f })
        for (float frequency : { 31.5f, 63.0f, 125.0f, 250.0f, 500.0f, 1000.0f,
                                 2000.0f, 4000.0f, 8000.0f, 16000.0f })
            std::cout << "DEFAULT_CURVE\t" << level << "\t" << frequency << "\t"
                      << measureSineGainDb (plugin, frequency, level) << "\n";
}

void measureControlAudit (juce::AudioPluginInstance& plugin)
{
    const std::array<const char*, 5> bands { "Body", "Warm", "Clear", "Presence", "Air" };
    const std::array<float, 5> frequencies { 70.0f, 260.0f, 1100.0f, 3600.0f, 11000.0f };
    const std::array<float, 4> macroSettings { 0.25f, 0.5f, 0.75f, 1.0f };
    const std::array<float, 5> bipolarSettings { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    const std::array<float, 4> levels { -60.0f, -36.0f, -18.0f, -6.0f };
    plugin.releaseResources();
    plugin.prepareToPlay (48000.0, 512);

    auto soloBand = [&] (const juce::String& selected)
    {
        for (auto band : bands)
            setParameter (plugin, juce::String (band) + " Solo", juce::String (band) == selected ? 1.0f : 0.0f);
    };
    auto emit = [&] (const juce::String& control, float setting, const juce::String& band,
                     float level, float frequency)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        std::cout << "AUDIT\t" << control << "\t" << setting << "\t" << band << "\t"
                  << level << "\t" << measureSineGainDb (plugin, frequency, level) << "\n";
    };

    for (auto stage : { "Expander", "Upward", "Downward" })
        for (float setting : macroSettings)
            for (size_t bandIndex = 0; bandIndex < bands.size(); ++bandIndex)
            {
                prepareReferenceState (plugin);
                setParameter (plugin, "Amount", 0.5f);
                setParameter (plugin, stage, setting);
                soloBand (bands[bandIndex]);
                for (float level : levels)
                    emit (stage, setting, bands[bandIndex], level, frequencies[bandIndex]);
            }

    for (float setting : macroSettings)
        for (size_t bandIndex = 0; bandIndex < bands.size(); ++bandIndex)
        {
            prepareReferenceState (plugin);
            setParameter (plugin, "Amount", setting);
            setParameter (plugin, "Expander", 0.5f);
            setParameter (plugin, "Upward", 0.5f);
            setParameter (plugin, "Downward", 0.5f);
            soloBand (bands[bandIndex]);
            for (float level : levels)
                emit ("Amount", setting, bands[bandIndex], level, frequencies[bandIndex]);
        }

    for (float setting : bipolarSettings)
        for (size_t bandIndex = 0; bandIndex < bands.size(); ++bandIndex)
        {
            prepareReferenceState (plugin);
            setParameter (plugin, "Amount", 0.5f);
            setParameter (plugin, "Expander", 0.5f);
            setParameter (plugin, "Upward", 0.5f);
            setParameter (plugin, "Downward", 0.5f);
            setParameter (plugin, juce::String (bands[bandIndex]) + " Threshold", setting);
            soloBand (bands[bandIndex]);
            for (float level : { -48.0f, -24.0f, -6.0f })
                emit ("Threshold", setting, bands[bandIndex], level, frequencies[bandIndex]);
        }

    for (float setting : bipolarSettings)
        for (size_t bandIndex = 0; bandIndex < bands.size(); ++bandIndex)
        {
            prepareReferenceState (plugin);
            setParameter (plugin, juce::String (bands[bandIndex]) + " Makeup", setting);
            soloBand (bands[bandIndex]);
            emit ("Makeup", setting, bands[bandIndex], -24.0f, frequencies[bandIndex]);
        }

    for (float setting : bipolarSettings)
        for (size_t bandIndex = 0; bandIndex < bands.size(); ++bandIndex)
        {
            prepareReferenceState (plugin);
            setParameter (plugin, "Amount", 0.5f);
            setParameter (plugin, "Expander", 0.5f);
            setParameter (plugin, "Upward", 0.5f);
            setParameter (plugin, "Downward", 0.5f);
            setParameter (plugin, "Mix", setting);
            soloBand (bands[bandIndex]);
            for (float level : { -48.0f, -24.0f, -6.0f })
                emit ("Mix", setting, bands[bandIndex], level, frequencies[bandIndex]);
        }

    for (auto gainName : { "Input Gain", "Output Gain" })
        for (float setting : bipolarSettings)
        {
            prepareReferenceState (plugin);
            setParameter (plugin, gainName, setting);
            emit (gainName, setting, "Full", -30.0f, 1000.0f);
        }

    for (size_t bandIndex = 0; bandIndex < bands.size(); ++bandIndex)
        for (float level : { -36.0f, -6.0f })
        {
            prepareReferenceState (plugin);
            setParameter (plugin, "Amount", 0.5f);
            setParameter (plugin, "Expander", 0.5f);
            setParameter (plugin, "Upward", 0.5f);
            setParameter (plugin, "Downward", 0.5f);
            setParameter (plugin, juce::String (bands[bandIndex]) + " Bypass", 1.0f);
            soloBand (bands[bandIndex]);
            emit ("Bypass", 1.0f, bands[bandIndex], level, frequencies[bandIndex]);
        }
}

void measureStereoAudit (juce::AudioPluginInstance& plugin)
{
    constexpr int blockSize = 512;
    constexpr int blocks = 120;
    juce::MidiBuffer midi;
    for (float msMode : { 0.0f, 1.0f })
        for (float link : { 0.0f, 0.5f, 1.0f })
            for (float frequency : { 70.0f, 1100.0f, 11000.0f })
                for (int polarity : { 1, -1 })
                {
                    plugin.releaseResources();
                    plugin.prepareToPlay (48000.0, blockSize);
                    prepareReferenceState (plugin);
                    setParameter (plugin, "Amount", 0.5f);
                    setParameter (plugin, "Expander", 0.5f);
                    setParameter (plugin, "Upward", 0.5f);
                    setParameter (plugin, "Downward", 0.5f);
                    setParameter (plugin, "Stereo Linking", link);
                    setParameter (plugin, "MS Mode On", msMode);
                    plugin.reset();
                    const float leftGain = juce::Decibels::decibelsToGain (-6.0f);
                    const float rightGain = juce::Decibels::decibelsToGain (polarity > 0 ? -36.0f : -6.0f);
                    double inEnergy[2] {}, outEnergy[2] {};
                    double phase = 0.0;
                    const double phaseStep = juce::MathConstants<double>::twoPi * frequency / 48000.0;
                    for (int block = 0; block < blocks; ++block)
                    {
                        juce::AudioBuffer<float> audio (2, blockSize);
                        for (int sample = 0; sample < blockSize; ++sample)
                        {
                            const float sine = std::sin ((float) phase);
                            phase += phaseStep;
                            const float left = leftGain * sine;
                            const float right = rightGain * sine * (float) polarity;
                            audio.setSample (0, sample, left);
                            audio.setSample (1, sample, right);
                            if (block >= blocks / 2)
                                inEnergy[0] += left * left, inEnergy[1] += right * right;
                        }
                        plugin.processBlock (audio, midi);
                        if (block >= blocks / 2)
                            for (int sample = 0; sample < blockSize; ++sample)
                                for (int channel = 0; channel < 2; ++channel)
                                {
                                    const double output = audio.getSample (channel, sample);
                                    outEnergy[channel] += output * output;
                                }
                    }
                    std::cout << "STEREO\t" << msMode << "\t" << link << "\t" << frequency
                              << "\t" << polarity << "\t"
                              << 10.0 * std::log10 (juce::jmax (1.0e-30, outEnergy[0] / inEnergy[0])) << "\t"
                              << 10.0 * std::log10 (juce::jmax (1.0e-30, outEnergy[1] / inEnergy[1])) << "\n";
                }
}

struct HarmonicResult
{
    std::array<double, 10> amplitudes {};
};

HarmonicResult measureHarmonics (juce::AudioPluginInstance& plugin, float frequency, float inputDb)
{
    constexpr int blockSize = 512;
    constexpr int warmupSamples = 96000;
    constexpr int measuredSamples = 48000;
    constexpr int totalSamples = warmupSamples + measuredSamples;
    const double phaseStep = juce::MathConstants<double>::twoPi * frequency / 48000.0;
    const float inputGain = juce::Decibels::decibelsToGain (inputDb);
    std::array<double, 10> real {}, imag {};
    juce::MidiBuffer midi;
    plugin.reset();

    for (int offset = 0; offset < totalSamples; offset += blockSize)
    {
        const int validSamples = juce::jmin (blockSize, totalSamples - offset);
        juce::AudioBuffer<float> audio (2, blockSize);
        audio.clear();
        for (int sample = 0; sample < validSamples; ++sample)
        {
            const double phase = phaseStep * (double) (offset + sample);
            const float x = inputGain * std::sin ((float) phase);
            audio.setSample (0, sample, x);
            audio.setSample (1, sample, x);
        }
        plugin.processBlock (audio, midi);

        for (int sample = 0; sample < validSamples; ++sample)
        {
            const int position = offset + sample;
            if (position < warmupSamples)
                continue;
            const double phase = phaseStep * (double) position;
            const double y = audio.getSample (0, sample);
            for (int harmonic = 1; harmonic <= 10; ++harmonic)
            {
                real[(size_t) harmonic - 1] += y * std::cos (phase * harmonic);
                imag[(size_t) harmonic - 1] -= y * std::sin (phase * harmonic);
            }
        }
    }

    HarmonicResult result;
    for (size_t harmonic = 0; harmonic < result.amplitudes.size(); ++harmonic)
        result.amplitudes[harmonic] = 2.0 * std::hypot (real[harmonic], imag[harmonic])
                                    / (double) measuredSamples;
    return result;
}

void measureHarmonicAudit (juce::AudioPluginInstance& plugin)
{
    const std::array<const char*, 5> bands { "Body", "Warm", "Clear", "Presence", "Air" };
    const std::array<float, 5> frequencies { 70.0f, 260.0f, 1100.0f, 3600.0f, 11000.0f };

    auto prepareDefault = [&]
    {
        plugin.releaseResources();
        plugin.prepareToPlay (48000.0, 512);
        prepareReferenceState (plugin);
        setParameter (plugin, "Amount", 0.5f);
        setParameter (plugin, "Expander", 0.5f);
        setParameter (plugin, "Upward", 0.5f);
        setParameter (plugin, "Downward", 0.5f);
    };

    auto emit = [&] (const juce::String& control, float setting, float frequency, float level)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        const auto result = measureHarmonics (plugin, frequency, level);
        const double fundamental = juce::jmax (1.0e-30, result.amplitudes[0]);
        double thdPower = 0.0;
        for (size_t index = 1; index < result.amplitudes.size(); ++index)
            thdPower += result.amplitudes[index] * result.amplitudes[index];
        const double thdDb = 20.0 * std::log10 (juce::jmax (1.0e-30, std::sqrt (thdPower) / fundamental));
        std::cout << "HARMONIC_SUMMARY\t" << control << "\t" << setting << "\t"
                  << frequency << "\t" << level << "\t" << thdDb << "\n";
        for (size_t index = 0; index < result.amplitudes.size(); ++index)
        {
            const double dbfs = 20.0 * std::log10 (juce::jmax (1.0e-30, result.amplitudes[index]));
            const double relativeDb = 20.0 * std::log10 (
                juce::jmax (1.0e-30, result.amplitudes[index] / fundamental));
            std::cout << "HARMONIC\t" << control << "\t" << setting << "\t"
                      << frequency << "\t" << level << "\t" << (index + 1) << "\t"
                      << dbfs << "\t" << relativeDb << "\n";
        }
    };

    for (size_t band = 0; band < bands.size(); ++band)
        for (float level : { -36.0f, -18.0f, -6.0f })
        {
            prepareDefault();
            emit (juce::String ("Default/") + bands[band], 0.5f, frequencies[band], level);
        }

    for (auto control : { "Time", "Amount", "Mix", "Input Gain", "Output Gain",
                          "Expander", "Upward", "Downward" })
        for (float setting : { 0.0f, 0.5f, 1.0f })
        {
            prepareDefault();
            setParameter (plugin, control, setting);
            emit (control, setting, 1100.0f,
                  juce::String (control).contains ("Gain") ? -30.0f : -18.0f);
        }

    for (size_t band = 0; band < bands.size(); ++band)
        for (auto suffix : { "Threshold", "Makeup" })
            for (float setting : { 0.0f, 0.5f, 1.0f })
            {
                prepareDefault();
                setParameter (plugin, juce::String (bands[band]) + " " + suffix, setting);
                setParameter (plugin, juce::String (bands[band]) + " Solo", 1.0f);
                emit (juce::String (bands[band]) + " " + suffix, setting,
                      frequencies[band], -18.0f);
            }

    for (size_t band = 0; band < bands.size(); ++band)
        for (auto suffix : { "Solo", "Mute", "Bypass" })
        {
            prepareDefault();
            setParameter (plugin, juce::String (bands[band]) + " " + suffix, 1.0f);
            emit (juce::String (bands[band]) + " " + suffix, 1.0f,
                  frequencies[band], -18.0f);
        }
}

struct SignalStats
{
    double peak = 0.0;
    double rms = 0.0;
    double dc = 0.0;
};

SignalStats measureSteadySineStats (juce::AudioPluginInstance& plugin, float frequency, float inputDb)
{
    constexpr int blockSize = 512;
    constexpr int warmupSamples = 96000;
    constexpr int measuredSamples = 48000;
    constexpr int totalSamples = warmupSamples + measuredSamples;
    const double phaseStep = juce::MathConstants<double>::twoPi * frequency / 48000.0;
    const float inputGain = juce::Decibels::decibelsToGain (inputDb);
    double peak = 0.0, energy = 0.0, sum = 0.0;
    juce::MidiBuffer midi;
    plugin.reset();
    for (int offset = 0; offset < totalSamples; offset += blockSize)
    {
        const int validSamples = juce::jmin (blockSize, totalSamples - offset);
        juce::AudioBuffer<float> audio (2, blockSize);
        audio.clear();
        for (int sample = 0; sample < validSamples; ++sample)
        {
            const float x = inputGain * std::sin ((float) (phaseStep * (double) (offset + sample)));
            audio.setSample (0, sample, x);
            audio.setSample (1, sample, x);
        }
        plugin.processBlock (audio, midi);
        for (int sample = 0; sample < validSamples; ++sample)
        {
            if (offset + sample < warmupSamples)
                continue;
            const double y = audio.getSample (0, sample);
            peak = juce::jmax (peak, std::abs (y));
            energy += y * y;
            sum += y;
        }
    }
    return { peak, std::sqrt (energy / measuredSamples), sum / measuredSamples };
}

void measureExtremeOutputAudit (juce::AudioPluginInstance& plugin)
{
    auto prepare = [&]
    {
        plugin.releaseResources();
        plugin.prepareToPlay (48000.0, 512);
        prepareReferenceState (plugin);
    };
    auto emit = [&] (const juce::String& mode, float setting, float inputDb)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        const auto stats = measureSteadySineStats (plugin, 1000.0f, inputDb);
        std::cout << "EXTREME\t" << mode << "\t" << setting << "\t" << inputDb << "\t"
                  << juce::Decibels::gainToDecibels (stats.peak, -300.0) << "\t"
                  << juce::Decibels::gainToDecibels (stats.rms, -300.0) << "\t"
                  << stats.dc << "\n";
    };

    for (float setting : { 0.5f, 0.75f, 1.0f })
        for (float level : { -30.0f, -18.0f, -12.0f, -6.0f, 0.0f, 6.0f })
        {
            prepare();
            setParameter (plugin, "Mix", 0.0f);
            setParameter (plugin, "Output Gain", setting);
            emit ("DryOutGain", setting, level);
        }

    for (float setting : { 0.5f, 0.75f, 1.0f })
        for (float level : { -30.0f, -18.0f, -12.0f, -6.0f, 0.0f, 6.0f })
        {
            prepare();
            setParameter (plugin, "Mix", 0.0f);
            setParameter (plugin, "Input Gain", setting);
            emit ("DryInGain", setting, level);
        }

    for (float level : { -72.0f, -60.0f, -48.0f, -36.0f, -24.0f, -12.0f, -6.0f, 0.0f, 6.0f })
    {
        prepare();
        setParameter (plugin, "Amount", 0.5f);
        setParameter (plugin, "Expander", 0.5f);
        setParameter (plugin, "Upward", 0.5f);
        setParameter (plugin, "Downward", 0.5f);
        emit ("WetDefault", 0.5f, level);
    }

    for (float level : { -72.0f, -60.0f, -48.0f, -36.0f, -24.0f, -12.0f, -6.0f, 0.0f, 6.0f })
    {
        prepare();
        setParameter (plugin, "Amount", 1.0f);
        setParameter (plugin, "Expander", 1.0f);
        setParameter (plugin, "Upward", 1.0f);
        setParameter (plugin, "Downward", 1.0f);
        emit ("WetDynamicsExtreme", 1.0f, level);
    }

    for (float level : { -72.0f, -60.0f, -48.0f, -36.0f, -24.0f, -12.0f, -6.0f, 0.0f, 6.0f })
    {
        prepare();
        setParameter (plugin, "Amount", 1.0f);
        setParameter (plugin, "Expander", 1.0f);
        setParameter (plugin, "Upward", 1.0f);
        setParameter (plugin, "Downward", 1.0f);
        setParameter (plugin, "Input Gain", 1.0f);
        setParameter (plugin, "Output Gain", 1.0f);
        for (auto band : { "Body", "Warm", "Clear", "Presence", "Air" })
            setParameter (plugin, juce::String (band) + " Makeup", 1.0f);
        emit ("WetExtreme", 1.0f, level);
    }
}

void measureTimeSteps (juce::AudioPluginInstance& plugin, const juce::File& outputDirectory)
{
    constexpr int blockSize = 512;
    constexpr int totalSamples = 144000;
    constexpr float frequency = 11000.0f;
    juce::MidiBuffer midi;

    for (const auto setting : { std::pair { 0.0f, "0500" },
                                std::pair { 0.5f, "1000" },
                                std::pair { 1.0f, "2000" } })
    {
        plugin.releaseResources();
        plugin.prepareToPlay (48000.0, blockSize);
        prepareReferenceState (plugin);
        setParameter (plugin, "Amount", 0.5f);
        setParameter (plugin, "Downward", 0.5f);
        setParameter (plugin, "Time", setting.first);
        setParameter (plugin, "Air Solo", 1.0f);
        plugin.reset();

        std::vector<float> result ((size_t) totalSamples, 0.0f);
        double phase = 0.0;
        const double phaseStep = juce::MathConstants<double>::twoPi * frequency / 48000.0;
        for (int offset = 0; offset < totalSamples; offset += blockSize)
        {
            const int validSamples = juce::jmin (blockSize, totalSamples - offset);
            juce::AudioBuffer<float> audio (2, blockSize);
            audio.clear();
            for (int i = 0; i < validSamples; ++i)
            {
                const int position = offset + i;
                const float levelDb = position < 24000 || position >= 72000 ? -24.0f : 0.0f;
                const float x = juce::Decibels::decibelsToGain (levelDb) * std::sin ((float) phase);
                phase += phaseStep;
                audio.setSample (0, i, x);
                audio.setSample (1, i, x);
            }
            plugin.processBlock (audio, midi);
            std::copy_n (audio.getReadPointer (0), validSamples, result.begin() + offset);
        }
        writeRawFloat (outputDirectory.getChildFile (
            juce::String ("time-") + setting.second + "-air-down-step-f32.raw"), result);
    }
}

void measureMixImpulses (juce::AudioPluginInstance& plugin, const juce::File& outputDirectory)
{
    constexpr int blockSize = 512;
    constexpr int totalSamples = 65536;
    juce::MidiBuffer midi;
    for (const auto setting : { std::pair { 0.0f, "000" },
                                std::pair { 0.5f, "050" },
                                std::pair { 1.0f, "100" } })
    {
        plugin.releaseResources();
        plugin.prepareToPlay (48000.0, blockSize);
        prepareReferenceState (plugin);
        setParameter (plugin, "Mix", setting.first);
        plugin.reset();
        for (int warmup = 0; warmup < 100; ++warmup)
        {
            juce::AudioBuffer<float> silence (2, blockSize); silence.clear();
            plugin.processBlock (silence, midi);
        }
        std::vector<float> result ((size_t) totalSamples, 0.0f);
        for (int offset = 0; offset < totalSamples; offset += blockSize)
        {
            juce::AudioBuffer<float> audio (2, blockSize); audio.clear();
            if (offset == 0) audio.setSample (0, 0, 1.0f), audio.setSample (1, 0, 1.0f);
            plugin.processBlock (audio, midi);
            std::copy_n (audio.getReadPointer (0), blockSize, result.begin() + offset);
        }
        writeRawFloat (outputDirectory.getChildFile (
            juce::String ("mix-") + setting.second + "-amount-zero-impulse-f32.raw"), result);
    }
}

void measureDrifts (juce::AudioPluginInstance& plugin, const juce::File& outputDirectory)
{
    constexpr int blockSize = 512, totalSamples = 65536;
    const std::array<const char*, 4> drifts { "Low Freq Drift", "Mid Freq Drift",
                                              "High Freq Drift", "Air Freq Drift" };
    const std::array<const char*, 5> bands { "Body", "Warm", "Clear", "Presence", "Air" };
    juce::MidiBuffer midi;
    for (size_t driftIndex = 0; driftIndex < drifts.size(); ++driftIndex)
        for (float setting : { 0.0f, 1.0f })
            for (auto band : bands)
            {
                plugin.releaseResources(); plugin.prepareToPlay (48000.0, blockSize);
                prepareReferenceState (plugin);
                setParameter (plugin, drifts[driftIndex], setting);
                setParameter (plugin, juce::String (band) + " Solo", 1.0f);
                plugin.reset();
                for (int warmup = 0; warmup < 100; ++warmup)
                {
                    juce::AudioBuffer<float> silence (2, blockSize); silence.clear();
                    plugin.processBlock (silence, midi);
                }
                std::vector<float> result ((size_t) totalSamples, 0.0f);
                for (int offset = 0; offset < totalSamples; offset += blockSize)
                {
                    juce::AudioBuffer<float> audio (2, blockSize); audio.clear();
                    if (offset == 0) audio.setSample (0, 0, 1.0f), audio.setSample (1, 0, 1.0f);
                    plugin.processBlock (audio, midi);
                    std::copy_n (audio.getReadPointer (0), blockSize, result.begin() + offset);
                }
                auto key = juce::String ("drift-") + juce::String ((int) driftIndex) + "-"
                         + (setting < 0.5f ? "min-" : "max-") + juce::String (band).toLowerCase()
                         + "-impulse-f32.raw";
                writeRawFloat (outputDirectory.getChildFile (key), result);
            }
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    const juce::String path = argc > 1 ? juce::String::fromUTF8 (argv[1])
                                      : juce::String (R"(C:\Program Files\Common Files\VST3\ThreeBodyTech\VOTT(64).vst3)");
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    format.findAllTypesForFile (descriptions, path);
    std::cout << "DESCRIPTIONS=" << descriptions.size() << "\n";
    if (descriptions.isEmpty())
        return 2;

    auto description = *descriptions[0];
    std::cout << "PLUGIN\tname=" << description.name << "\tmanufacturer="
              << description.manufacturerName << "\tversion=" << description.version
              << "\tuid=" << description.uniqueId << "\n";
    if (auto xml = description.createXml())
        std::cout << "DESCRIPTION_XML=" << xml->toString (juce::XmlElement::TextFormat().singleLine()) << "\n";
    if (argc > 2 && juce::String::fromUTF8 (argv[2]) == "--description-only")
        return 0;

    juce::String error;
    auto plugin = format.createInstanceFromDescription (description, 48000.0, 512, error);
    if (plugin == nullptr)
    {
        std::cerr << "CREATE_ERROR=" << error << "\n";
        return 3;
    }

    if (argc > 3 && juce::String::fromUTF8 (argv[2]) == "--load-state")
    {
        juce::MemoryBlock stateData;
        juce::File stateFile (juce::String::fromUTF8 (argv[3]));
        stateFile.loadFileAsData (stateData);
        plugin->setStateInformation (stateData.getData(), (int) stateData.getSize());
    }

    if (argc > 3 && juce::String::fromUTF8 (argv[2]) == "--save-state")
    {
        juce::MemoryBlock stateData;
        plugin->getStateInformation (stateData);
        juce::File stateFile (juce::String::fromUTF8 (argv[3]));
        stateFile.deleteFile();
        if (auto stream = stateFile.createOutputStream())
            stream->write (stateData.getData(), stateData.getSize());
    }

    dumpParameters (*plugin);
    if (argc > 2 && juce::String::fromUTF8 (argv[2]) == "--audit-only")
    {
        measureControlAudit (*plugin);
        measureStereoAudit (*plugin);
        return 0;
    }
    if (argc > 2 && juce::String::fromUTF8 (argv[2]) == "--harmonic-only")
    {
        measureHarmonicAudit (*plugin);
        return 0;
    }
    if (argc > 2 && juce::String::fromUTF8 (argv[2]) == "--extreme-only")
    {
        measureExtremeOutputAudit (*plugin);
        return 0;
    }
    const bool isReferenceVott = description.name.containsIgnoreCase ("VOTT");
    const bool isSqtt = description.name.containsIgnoreCase ("SQ-TT");
    auto outputDirectory = juce::File::getCurrentWorkingDirectory().getChildFile (
        isSqtt ? "tmp/sqtt-probe" : "tmp/vott-probe");
    outputDirectory.createDirectory();
    if (isReferenceVott || isSqtt)
    {
        measureBandImpulses (*plugin, outputDirectory);
        measureDynamics (*plugin);
        measureDefaultCurve (*plugin);
        measureTimeSteps (*plugin, outputDirectory);
        measureMixImpulses (*plugin, outputDirectory);
        measureDrifts (*plugin, outputDirectory);
    }
    else
    {
        plugin->prepareToPlay (48000.0, 512);
        setParameter (*plugin, "Depth", 0.0f);
        setParameter (*plugin, "Amount", 0.0f);
        setParameter (*plugin, "Mix", 1.0f);
        plugin->reset();
        juce::MidiBuffer midi;
        for (int block = 0; block < 100; ++block)
        {
            juce::AudioBuffer<float> silence (2, 512); silence.clear();
            plugin->processBlock (silence, midi);
        }
        std::vector<float> result (65536, 0.0f);
        for (int offset = 0; offset < (int) result.size(); offset += 512)
        {
            juce::AudioBuffer<float> audio (2, 512); audio.clear();
            if (offset == 0) audio.setSample (0, 0, 1.0f), audio.setSample (1, 0, 1.0f);
            plugin->processBlock (audio, midi);
            std::copy_n (audio.getReadPointer (0), 512, result.begin() + offset);
        }
        writeRawFloat (outputDirectory.getChildFile ("sqtt-full-impulse-f32.raw"), result);
    }
    return 0;
}
