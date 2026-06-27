#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout
HighPrecisionEQAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // カットオフ周波数: 20 〜 500 Hz
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "cutoffHz", 1 },
        "Cutoff Frequency",
        juce::NormalisableRange<float>(1.0f, 500.0f, 0.1f, 0.25f),
        80.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")
    ));

    // スロープ選択: 12 〜 96 dB/oct
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "slopeDbOct", 1 },
        "Slope",
        juce::NormalisableRange<float>(12.0f, 96.0f, 12.0f),
        24.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB/oct")
    ));

    // カットゲイン: 0 〜 -10 dB
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "gainDb", 1 },
        "Gain",
        juce::NormalisableRange<float>(-10.0f, 0.0f, 0.1f),
        -10.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")
    ));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ "lowcut_enable", 1 },
        "LowCut Enable",
        true
    ));

    for (int i = 1; i <= 4; ++i) {
        juce::String idSuffix = juce::String(i);
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ "bell_freq_" + idSuffix, 1 },
            "Bell " + idSuffix + " Freq",
            juce::NormalisableRange<float>(1.0f, 25000.0f, 0.1f, 0.3f),
            1000.0f * static_cast<float>(i),
            juce::AudioParameterFloatAttributes().withLabel("Hz")
        ));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ "bell_gain_" + idSuffix, 1 },
            "Bell " + idSuffix + " Gain",
            juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel("dB")
        ));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ "bell_q_" + idSuffix, 1 },
            "Bell " + idSuffix + " Q",
            juce::NormalisableRange<float>(0.1f, 120.0f, 0.1f, 0.3f),
            10.0f
        ));
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{ "bell_enable_" + idSuffix, 1 },
            "Bell " + idSuffix + " Enable",
            false
        ));
    }

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ "listenDiff", 1 },
        "Listen Diff",
        false
    ));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ "analyzer_hold", 1 },
        "Analyzer Hold",
        false
    ));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ "bypass", 1 },
        "Bypass",
        false
    ));

    // ハイカットパラメータの追加
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "highcut_freq", 1 },
        "HighCut Frequency",
        juce::NormalisableRange<float>(1.0f, 25000.0f, 0.1f, 0.3f),
        20000.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")
    ));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "highcut_slope", 1 },
        "HighCut Slope",
        juce::NormalisableRange<float>(12.0f, 96.0f, 12.0f),
        24.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB/oct")
    ));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ "highcut_enable", 1 },
        "HighCut Enable",
        false
    ));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "highcut_gainDb", 1 },
        "HighCut Gain",
        juce::NormalisableRange<float>(-10.0f, 0.0f, 0.1f),
        -10.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")
    ));

    return { params.begin(), params.end() };
}

HighPrecisionEQAudioProcessor::HighPrecisionEQAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    setLatencySamples(0);
}

HighPrecisionEQAudioProcessor::~HighPrecisionEQAudioProcessor()
{
}

void HighPrecisionEQAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize  = samplesPerBlock;

    minimumPhaseEQ.prepare(sampleRate, samplesPerBlock);
    soloFilter.prepare(sampleRate);
    setLatencySamples(0);

    snapshotQueue.reset(8);
    
    {
        double targetSr = sampleRate;
        size_t preTrigger = static_cast<size_t>(0.100 * targetSr);  // 100ms
        size_t postTrigger = static_cast<size_t>(2.000 * targetSr); // 2.0s
        size_t totalSamples = preTrigger + postTrigger;
        for (size_t i = 0; i <= snapshotQueue.capacity(); ++i)
        {
            snapshotQueue.getElement(i).resize(totalSamples);
        }
    }
    
    waveformCatcher.prepareToPlay(sampleRate, samplesPerBlock);
    
    analyzerDSP.prepare(sampleRate);
    
    dryBuffer.setSize(2, samplesPerBlock);
    
    int numChannels = getTotalNumInputChannels();
    if (numChannels <= 0) numChannels = 2;
    dryDelayBuffer.setSize(numChannels, 1);
    dryDelayBuffer.clear();
    dryDelayWriteIdx = 0;

    // GUIを開く前、またはDAW停止中にprepareToPlayが呼ばれた場合でも
    // 確実にEQカーブが初期化されるよう、ここで一度パラメータを強制同期する
    std::array<MinimumPhaseEQ::BellParam, 4> bells;
    for (int i = 0; i < 4; ++i) {
        juce::String idSuffix = juce::String(i + 1);
        bells[static_cast<size_t>(i)].freq = static_cast<double>(apvts.getRawParameterValue("bell_freq_" + idSuffix)->load());
        bells[static_cast<size_t>(i)].gain = static_cast<double>(apvts.getRawParameterValue("bell_gain_" + idSuffix)->load());
        bells[static_cast<size_t>(i)].q = static_cast<double>(apvts.getRawParameterValue("bell_q_" + idSuffix)->load());
        bells[static_cast<size_t>(i)].active = apvts.getRawParameterValue("bell_enable_" + idSuffix)->load() > 0.5f;
    }

    minimumPhaseEQ.updateParameters(
        static_cast<double>(apvts.getRawParameterValue("cutoffHz")->load()),
        std::clamp(static_cast<int>(std::round(apvts.getRawParameterValue("slopeDbOct")->load() / 12.0f)), 1, 8),
        apvts.getRawParameterValue("lowcut_enable")->load() > 0.5f,
        static_cast<double>(apvts.getRawParameterValue("gainDb")->load()),
        static_cast<double>(apvts.getRawParameterValue("highcut_freq")->load()),
        std::clamp(static_cast<int>(std::round(apvts.getRawParameterValue("highcut_slope")->load() / 12.0f)), 1, 8),
        apvts.getRawParameterValue("highcut_enable")->load() > 0.5f,
        static_cast<double>(apvts.getRawParameterValue("highcut_gainDb")->load()),
        bells
    );
}

void HighPrecisionEQAudioProcessor::releaseResources()
{
    minimumPhaseEQ.reset();
    soloFilter.reset();
}

void HighPrecisionEQAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    float cutoffHz = apvts.getRawParameterValue("cutoffHz")->load();
    float slopeVal = apvts.getRawParameterValue("slopeDbOct")->load();
    float gainDb = apvts.getRawParameterValue("gainDb")->load();

    int order = static_cast<int>(std::round(slopeVal / 6.0f));
    order = std::clamp(order, 1, 16);

    bool lowcut_enable = apvts.getRawParameterValue("lowcut_enable")->load() > 0.5f;

    float hcFreq = apvts.getRawParameterValue("highcut_freq")->load();
    float hcSlope = apvts.getRawParameterValue("highcut_slope")->load();
    int hcOrder = static_cast<int>(std::round(hcSlope / 6.0f));
    hcOrder = std::clamp(hcOrder, 1, 16);
    bool hcEnable = apvts.getRawParameterValue("highcut_enable")->load() > 0.5f;
    float hcGainDb = apvts.getRawParameterValue("highcut_gainDb")->load();

    bool listenDiff = apvts.getRawParameterValue("listenDiff")->load() > 0.5f;
    bool bypass = apvts.getRawParameterValue("bypass")->load() > 0.5f;
    bool holdEnable = apvts.getRawParameterValue("analyzer_hold")->load() > 0.5f;
    
    analyzerDSP.setHold(holdEnable);

    int numChannels = buffer.getNumChannels();
    int numSamples = buffer.getNumSamples();
    if (dryBuffer.getNumChannels() < numChannels)
        dryBuffer.setSize(numChannels, currentBlockSize, false, false, true);
    
    for (int ch = 0; ch < numChannels; ++ch)
        dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

    float maxVal = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch) {
        auto* r = buffer.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i) {
            maxVal = std::max(maxVal, std::abs(r[i]));
        }
    }
    
    bool inputIsSilent = (maxVal <= 1e-6f);
    if (inputIsSilent)
    {
        silenceSamplesCounter += numSamples;
        if (silenceSamplesCounter > static_cast<int>(1.5 * currentSampleRate))
        {
            isSuspended = true;
        }
    }
    else
    {
        silenceSamplesCounter = 0;
        if (isSuspended)
        {
            isSuspended = false;
        }
    }

    std::array<MinimumPhaseEQ::BellParam, 4> bells;
    for (int i = 0; i < 4; ++i) {
        juce::String idSuffix = juce::String(i + 1);
        bells[static_cast<size_t>(i)].freq = static_cast<double>(apvts.getRawParameterValue("bell_freq_" + idSuffix)->load());
        bells[static_cast<size_t>(i)].gain = static_cast<double>(apvts.getRawParameterValue("bell_gain_" + idSuffix)->load());
        bells[static_cast<size_t>(i)].q = static_cast<double>(apvts.getRawParameterValue("bell_q_" + idSuffix)->load());
        bells[static_cast<size_t>(i)].active = apvts.getRawParameterValue("bell_enable_" + idSuffix)->load() > 0.5f;
    }

    if (!isSuspended)
    {
        minimumPhaseEQ.updateParameters(
            static_cast<double>(cutoffHz), order, lowcut_enable, static_cast<double>(gainDb),
            static_cast<double>(hcFreq), hcOrder, hcEnable, static_cast<double>(hcGainDb),
            bells
        );
        minimumPhaseEQ.process(buffer);
    }

    bool isSoloActive = soloMode.load();
    if (isSoloActive)
    {
        if (!wasSoloActive)
        {
            soloFilter.reset();
            wasSoloActive = true;
        }
        soloFilter.updateParameters(soloFreq.load(), soloQ.load());
        soloFilter.process(buffer);
    }
    else
    {
        wasSoloActive = false;
    }

    if (numChannels > 0)
    {
        int delayLen = dryDelayBuffer.getNumSamples();
        if (dryDelayBuffer.getNumChannels() < numChannels)
            dryDelayBuffer.setSize(numChannels, delayLen, true, true, true);

        for (int i = 0; i < numSamples; ++i)
        {
            float monoDelayedDry = 0.0f;
            float monoWet = 0.0f;
            int readIdx = (dryDelayWriteIdx + 1) % delayLen;
            
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float dry = dryBuffer.getSample(ch, i);
                float wet = buffer.getSample(ch, i);
                float delayedDry = dry;
                
                if (delayLen > 1) {
                    dryDelayBuffer.setSample(ch, dryDelayWriteIdx, dry);
                    delayedDry = dryDelayBuffer.getSample(ch, readIdx);
                }
                
                if (isSoloActive) {
                    buffer.setSample(ch, i, wet);
                } else if (bypass) {
                    buffer.setSample(ch, i, delayedDry);
                } else if (listenDiff) {
                    buffer.setSample(ch, i, delayedDry - wet);
                }
                
                if (ch == 0) {
                    monoDelayedDry = delayedDry;
                    monoWet = wet;
                }
            }
            
            if (delayLen > 1) {
                dryDelayWriteIdx = (dryDelayWriteIdx + 1) % delayLen;
            }

            waveformCatcher.processSample(monoDelayedDry, monoWet);
        }
        
        if (!isSuspended)
        {
            analyzerDSP.pushAudio(buffer.getReadPointer(0), numSamples);
        }
    }
}

juce::AudioProcessorEditor* HighPrecisionEQAudioProcessor::createEditor()
{
    return new HighPrecisionEQAudioProcessorEditor(*this);
}

void HighPrecisionEQAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    xml->setAttribute("currentPaletteIdx", currentPaletteIdx);
    copyXmlToBinary(*xml, destData);
}

void HighPrecisionEQAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
    {
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
        currentPaletteIdx = xmlState->getIntAttribute("currentPaletteIdx", 0);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HighPrecisionEQAudioProcessor();
}
