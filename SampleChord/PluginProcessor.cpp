#include "PluginProcessor.h"
#include "PluginEditor.h"

// ============================================================================
//  パラメータレイアウト定義
// ============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
LowCutPoliceAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // カットオフ周波数: 20 〜 200 Hz（対数スケール）
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "cutoffHz", 1 },
        "Cutoff Frequency",
        juce::NormalisableRange<float>(20.0f, 500.0f, 0.1f, 0.4f), // skew=0.4 for log feel
        80.0f,
        juce::AudioParameterFloatAttributes{}
            .withLabel("Hz")
    ));

    // スロープ選択: 12 〜 96 dB/oct (12dB刻み)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "slopeDbOct", 1 },
        "Slope",
        juce::NormalisableRange<float>(12.0f, 96.0f, 12.0f),
        24.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB/oct")
    ));

    // カットゲイン: 0 〜 -10 dB（カットのみ）
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "gainDb", 1 },
        "Gain",
        juce::NormalisableRange<float>(-10.0f, 0.0f, 0.1f),
        -10.0f,
        juce::AudioParameterFloatAttributes{}
            .withLabel("dB")
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
            juce::NormalisableRange<float>(20.0f, 20000.0f, 0.1f, 0.3f),
            1000.0f * static_cast<float>(i),
            juce::AudioParameterFloatAttributes().withLabel("Hz")
        ));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ "bell_gain_" + idSuffix, 1 },
            "Bell " + idSuffix + " Gain",
            juce::NormalisableRange<float>(-18.0f, 18.0f, 0.1f),
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
        juce::ParameterID{ "bypass", 1 },
        "Bypass",
        false
    ));

    return { params.begin(), params.end() };
}

// ============================================================================
//  コンストラクタ / デストラクタ
// ============================================================================
LowCutPoliceAudioProcessor::LowCutPoliceAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    // 初期化時から正確なレイテンシーを申告しておくことで、PluginDoctor等のホストが正確に位相補正できるようにします。
    setLatencySamples(zeroPhaseFilter.getLatencySamples());
}

LowCutPoliceAudioProcessor::~LowCutPoliceAudioProcessor()
{
}

// ============================================================================
//  prepareToPlay
// ============================================================================
void LowCutPoliceAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize  = samplesPerBlock;

    zeroPhaseFilter.prepare(sampleRate, samplesPerBlock);
    for (auto& bf : bellFilters) {
        bf.prepare(sampleRate, samplesPerBlock);
    }
    soloFilter.prepare(sampleRate);
    int latency = zeroPhaseFilter.getLatencySamples();
    setLatencySamples(latency);

    // Allocate queue/catcher resources
    snapshotQueue.reset(8);
    
    // Pre-allocate slots to avoid heap allocation in audio thread
    {
        double targetSr = sampleRate;
        size_t preTrigger = static_cast<size_t>(0.010 * targetSr);
        size_t postTrigger = static_cast<size_t>(0.040 * targetSr);
        size_t totalSamples = preTrigger + postTrigger;
        for (size_t i = 0; i <= snapshotQueue.capacity(); ++i)
        {
            snapshotQueue.getElement(i).resize(totalSamples);
        }
    }
    
    waveformCatcher.prepareToPlay(sampleRate, samplesPerBlock);
    
    analyzerDSP.prepare(sampleRate);
    
    dryBuffer.setSize(2, samplesPerBlock);
    
    // アナライザ同期および差分リスニング用のステレオ遅延バッファ
    int numChannels = getTotalNumInputChannels();
    if (numChannels <= 0) numChannels = 2;
    dryDelayBuffer.setSize(numChannels, latency + 1);
    dryDelayBuffer.clear();
    dryDelayWriteIdx = 0;
}

// ============================================================================
//  releaseResources
// ============================================================================
void LowCutPoliceAudioProcessor::releaseResources()
{
    zeroPhaseFilter.reset();
    for (auto& bf : bellFilters) {
        bf.reset();
    }
    soloFilter.reset();
}

// ============================================================================
//  processBlock
// ============================================================================
void LowCutPoliceAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // パラメータ値の取得
    float cutoffHz = apvts.getRawParameterValue("cutoffHz")->load();
    float slopeVal = apvts.getRawParameterValue("slopeDbOct")->load();
    float gainDb = apvts.getRawParameterValue("gainDb")->load();

    // スロープ値（12 〜 96 dB/oct）をフィルタの次数（1 〜 8）にマッピング
    int order = static_cast<int>(std::round(slopeVal / 12.0f));
    order = std::clamp(order, 1, 8);

    bool listenDiff = apvts.getRawParameterValue("listenDiff")->load() > 0.5f;
    bool bypass = apvts.getRawParameterValue("bypass")->load() > 0.5f;

    // 1. Dry信号のコピー（WaveformCatcher用および差分リスニング用）
    int numChannels = buffer.getNumChannels();
    int numSamples = buffer.getNumSamples();
    if (dryBuffer.getNumChannels() < numChannels)
        dryBuffer.setSize(numChannels, currentBlockSize, false, false, true);
    
    for (int ch = 0; ch < numChannels; ++ch)
        dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

    // 1.5. 無音検出 (Silence DSP Suspender)
    float maxVal = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch) {
        auto* r = buffer.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i) {
            maxVal = std::max(maxVal, std::abs(r[i]));
        }
    }
    
    bool inputIsSilent = (maxVal <= 1e-6f); // -120 dB
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

    bool lowcut_enable = apvts.getRawParameterValue("lowcut_enable")->load() > 0.5f;

    // 2. フィルタパラメータ更新と信号処理 (Wet)
    std::array<ZeroPhaseFilter::BellParam, 4> bells;
    for (int i = 0; i < 4; ++i) {
        juce::String idSuffix = juce::String(i + 1);
        bells[static_cast<size_t>(i)].freq = static_cast<double>(apvts.getRawParameterValue("bell_freq_" + idSuffix)->load());
        bells[static_cast<size_t>(i)].gain = static_cast<double>(apvts.getRawParameterValue("bell_gain_" + idSuffix)->load());
        bells[static_cast<size_t>(i)].q = static_cast<double>(apvts.getRawParameterValue("bell_q_" + idSuffix)->load());
        bells[static_cast<size_t>(i)].active = apvts.getRawParameterValue("bell_enable_" + idSuffix)->load() > 0.5f;

        // 内部の bellFilters パラメータも念のため更新して整合性を保つ
        bellFilters[static_cast<size_t>(i)].setActive(bells[static_cast<size_t>(i)].active);
        bellFilters[static_cast<size_t>(i)].updateParameters(bells[static_cast<size_t>(i)].freq, bells[static_cast<size_t>(i)].gain, bells[static_cast<size_t>(i)].q);
    }

    zeroPhaseFilter.setSuspended(isSuspended);
    zeroPhaseFilter.updateParameters(static_cast<double>(cutoffHz), order, static_cast<double>(gainDb), lowcut_enable, bells);
    zeroPhaseFilter.process(buffer);

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

    // 3. 遅延バッファリング、波形キャプチャ、および差分出力
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
        
        // Wet信号（または差分信号）をアナライザーへ送る (無音サスペンド時はスキップ)
        if (!isSuspended)
        {
            analyzerDSP.pushAudio(buffer.getReadPointer(0), numSamples);
        }
    }
}

// ============================================================================
//  エディタ
// ============================================================================
juce::AudioProcessorEditor* LowCutPoliceAudioProcessor::createEditor()
{
    return new LowCutPoliceAudioProcessorEditor(*this);
}

// ============================================================================
//  ステート保存・復元
// ============================================================================
void LowCutPoliceAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    xml->setAttribute("currentPaletteIdx", currentPaletteIdx);
    copyXmlToBinary(*xml, destData);
}

void LowCutPoliceAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
    {
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
        currentPaletteIdx = xmlState->getIntAttribute("currentPaletteIdx", 0);
    }
}

// ============================================================================
//  エントリーポイント
// ============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LowCutPoliceAudioProcessor();
}
