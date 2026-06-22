#pragma once
#include <JuceHeader.h>
#include "SOSCoefficients.h"
#include "TPTSVF.h"
#include <vector>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>

class ZeroPhaseFilter
{
public:
    ZeroPhaseFilter();
    struct BellParam
    {
        double freq = 1000.0;
        double gain = 0.0;
        double q = 1.0;
        bool active = false;
    };
    void prepare(double sampleRate, int maxBlockSize);
    void updateParameters(double cutoffHz, int order, double gainDb, bool lcEnable,
                          const std::array<BellParam, 4>& bells);
    void process(juce::AudioBuffer<float>& buffer);
    void reset();
    void setSuspended(bool suspend) { isSuspended = suspend; }

    /// レイテンシー: (futureHops + 2) * hopSize (hopSize = processSize / 2)
    int getLatencySamples() const noexcept { return (futureHops + 2) * (processSize / 2); }

private:
    // TPT SVF 系
    void filtfiltRaw(double* data, int numSamples, const std::vector<TPTSection>& svf);
    void svffiltForwardSingleSection(double* data, int numSamples, const TPTSection& sec, std::array<double, 2>& state);
    void svffiltBackwardSingleSection(double* data, int numSamples, const TPTSection& sec, std::array<double, 2>& state);

    struct ChannelState;

    // ステレオ並列 SIMD 処理用メソッド
    void processStereo(juce::AudioBuffer<float>& buffer);
    void processCompleteBlockStereo(ChannelState& stateL, ChannelState& stateR);
    void svffiltForwardSingleSectionStereo(double* dataL, double* dataR, int numSamples, const TPTSection& sec, std::array<double, 2>& stateL, std::array<double, 2>& stateR);
    void svffiltBackwardSingleSectionStereo(double* dataL, double* dataR, int numSamples, const TPTSection& sec, std::array<double, 2>& stateL, std::array<double, 2>& stateR);

    void processCompleteBlock(ChannelState& state);
    static int computeProcessSize(double sampleRate);

    double currentSampleRate = 44100.0;
    int    processSize = 1536; // computeProcessSize(44100.0) returns 1536. Default to 1536.

    std::vector<TPTSection> currentSOS;
    std::vector<TPTSection> pendingSOS;
    std::vector<TPTSection> newSOSCache;
    double currentGainMix = 0.0;
    double pendingGainMix = 0.0;
    double currentOlaGainMix = 0.0;
    bool   coeffsNeedUpdate = false;
    bool   isSuspended = false;

    double lastCutoffHz = -1.0;
    int    lastOrder = 2;
    double lastGainMix = -1.0;
    bool   lastLcEnable = false;
    double lastGainDb = 0.0;
    std::array<BellParam, 4> lastBells;

    // ========================================================================
    // ウィンドウバッファ設計 (Continuous Forward / Block Backward OLA)
    // ========================================================================
    static constexpr int centerHops  = 2;
    static constexpr int futureHops  = 58; // ~480ms lookahead @ 96kHz (Latency = 46080 samples)
    static constexpr int windowHops  = centerHops + futureHops; // 60
    
    static constexpr int maxSections = 8;

    struct ChannelState
    {
        std::vector<double> inputWindow; // [windowHops * hopSize]
        std::vector<double> dryWindow;   // [windowHops * hopSize]
        std::vector<double> inBuffer;    // [hopSize]
        std::vector<double> olaBuffer;   // [2 * hopSize]
        int inPos = 0;
        
        std::vector<std::array<double, 2>> forwardState;
    };
    std::vector<ChannelState> channels;

    std::vector<double> paddedBuffer;
    std::vector<double> hannWindow;
    std::vector<double> paddedBufferBellOnlyDry;
    std::vector<double> inBufferDryCache;

    // ステレオ用プリ・アロケートキャッシュバッファ
    std::vector<double> paddedBufferL;
    std::vector<double> paddedBufferR;
    std::vector<double> paddedBufferBellOnlyDryL;
    std::vector<double> paddedBufferBellOnlyDryR;
    std::vector<std::array<double, 2>> backwardStateCacheL;
    std::vector<std::array<double, 2>> backwardStateCacheR;
};