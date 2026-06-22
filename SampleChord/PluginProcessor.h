#pragma once
#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP/ZeroPhaseFilter.h"
#include "DSP/SOSCoefficients.h"
#include "DSP/SPSCQueue.h"
#include "DSP/WaveformCatcher.h"
#include "DSP/AnalyzerDSP.h"
#include "DSP/TPTSVFBellFilter.h"
#include <array>

// ============================================================================
//  PluginProcessor.h
//  LowCut Police – メイン DSP プロセッサ
//
//  パラメータ:
//    cutoffHz  : カットオフ周波数 (20 〜 200 Hz)
//    slopeMode : スロープ (6 / 12 / 18 / 24 dB/oct)
//    gainDb    : カットゲイン (0 〜 -10 dB)
//
//  レイテンシー: LookAheadBuffer が計算し setLatencySamples() で報告
// ============================================================================

class LowCutPoliceAudioProcessor : public juce::AudioProcessor
{
public:
    LowCutPoliceAudioProcessor();
    ~LowCutPoliceAudioProcessor() override;

    //==========================================================================
    // AudioProcessor overrides
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    //==========================================================================
    // パラメータ
    juce::AudioProcessorValueTreeState apvts;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // GUI通信用キューへの参照を提供する
    SPSCSlotQueue<WaveformSnapshot>& getSnapshotQueue() { return snapshotQueue; }
    AnalyzerDSP& getAnalyzer() { return analyzerDSP; }

    int currentPaletteIdx = 0;

    void setSoloMode(bool enabled, float freq, float q) noexcept
    {
        soloMode.store(enabled);
        if (enabled)
        {
            soloFreq.store(freq);
            soloQ.store(q);
        }
    }

private:
    // DSP コンポーネント
    ZeroPhaseFilter  zeroPhaseFilter;
    std::array<TPTSVFBellFilter, 4> bellFilters;
    AnalyzerDSP      analyzerDSP;

    // 波形キャプチャ
    SPSCSlotQueue<WaveformSnapshot> snapshotQueue{8};
    WaveformCatcher waveformCatcher{snapshotQueue};
    juce::AudioBuffer<float> dryBuffer;
    
    // アナライザ用のDry遅延バッファ（Wetとタイミングを合わせるため）
    juce::AudioBuffer<float> dryDelayBuffer;
    int dryDelayWriteIdx = 0;

    double currentSampleRate = 44100.0;
    int    currentBlockSize  = 512;

    int    silenceSamplesCounter = 0;
    bool   isSuspended = false;

    // ソロ試聴用
    std::atomic<bool> soloMode { false };
    std::atomic<float> soloFreq { 1000.0f };
    std::atomic<float> soloQ { 1.0f };

    struct SoloFilter
    {
        double s1[2] = { 0.0, 0.0 };
        double s2[2] = { 0.0, 0.0 };
        double g = 0.0;
        double R2 = 0.0;
        double D = 0.0;
        double currentSampleRate = 44100.0;

        void prepare(double sampleRate)
        {
            currentSampleRate = sampleRate;
            reset();
        }

        void reset()
        {
            s1[0] = 0.0; s1[1] = 0.0;
            s2[0] = 0.0; s2[1] = 0.0;
        }

        void updateParameters(double freq, double Q)
        {
            if (freq < 20.0) freq = 20.0;
            if (freq > currentSampleRate * 0.49) freq = currentSampleRate * 0.49;
            if (Q < 0.1) Q = 0.1;

            double wd = 2.0 * std::numbers::pi * freq;
            double T = 1.0 / currentSampleRate;
            g = std::tan(wd * T / 2.0);
            R2 = 1.0 / Q;
            D = 1.0 / (1.0 + g * R2 + g * g);
        }

        void process(juce::AudioBuffer<float>& buffer)
        {
            const int numChannels = buffer.getNumChannels();
            const int numSamples = buffer.getNumSamples();

            for (int ch = 0; ch < std::min(numChannels, 2); ++ch)
            {
                float* channelData = buffer.getWritePointer(ch);
                double state1 = s1[static_cast<size_t>(ch)];
                double state2 = s2[static_cast<size_t>(ch)];

                for (int i = 0; i < numSamples; ++i)
                {
                    double x = channelData[i];
                    double hp = (x - (R2 + g) * state1 - state2) * D;
                    double bp = g * hp + state1;
                    
                    state1 = bp + g * hp;
                    state2 = state2 + 2.0 * g * bp;

                    double y = bp * R2;
                    channelData[i] = static_cast<float>(y);
                }

                s1[static_cast<size_t>(ch)] = state1;
                s2[static_cast<size_t>(ch)] = state2;
            }
        }
    };

    SoloFilter soloFilter;
    bool wasSoloActive = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LowCutPoliceAudioProcessor)
};
