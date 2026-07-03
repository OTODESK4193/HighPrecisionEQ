#pragma once

#include <JuceHeader.h>
#include <vector>
#include <array>
#include <atomic>
#include <cmath>
#include <numbers>
#include <memory>
#include <algorithm>

class AnalyzerDSP : public juce::Thread
{
public:
    static constexpr int NumBands = 800;

    AnalyzerDSP();
    ~AnalyzerDSP() override;

    void prepare(double sampleRate);
    void pushAudio(const float* data, int numSamples);
    std::vector<float> getEnergies(); // Returns dB values for GUI
    std::vector<float> getHoldEnergies(); // Returns peak hold dB values for GUI
    uint64_t getUpdateCount() const noexcept { return updateCount.load(std::memory_order_acquire); }
    void setHold(bool shouldHold);

    void run() override;

private:
    void processInternal(const float* data, int numSamples);

    struct AnalyzerBand
    {
        double g = 0.0;
        double h = 0.0;
        double R = 0.0;
        double ic1eq = 0.0;
        double ic2eq = 0.0;
        double env = 0.0;
        double attackCoef = 0.0;
        double releaseCoef = 0.0;

        void updateCoeffs(double fc, double Q, double sr)
        {
            const double wd = 2.0 * std::numbers::pi * fc;
            const double T = 1.0 / sr;
            const double wa = (2.0 / T) * std::tan(wd * T / 2.0);
            g = wa * T / 2.0;
            R = 1.0 / (2.0 * Q);
            h = 1.0 / (1.0 + 2.0 * R * g + g * g);

            // 動的時定数の計算 (周期 T = 1 / fc に基づく)
            // アタック時間は周期の 0.2倍 (最小 10ms), リリース時間は周期の 2.0倍 (最小 150ms)
            double period = 1.0 / std::max(1.0, fc);
            double attackTime = std::max(0.010, period * 0.2);
            double releaseTime = std::max(0.150, period * 2.0);

            attackCoef = std::exp(-1.0 / (attackTime * sr));
            releaseCoef = std::exp(-1.0 / (releaseTime * sr));
        }

        double process(double x)
        {
            double hp = (x - (2.0 * R + g) * ic1eq - ic2eq) * h;
            double bp = hp * g + ic1eq;

            if (std::isnan(bp) || std::isinf(bp)) {
                bp = 0.0;
                ic1eq = 0.0;
                ic2eq = 0.0;
            }

            double lp = bp * g + ic2eq;
            ic1eq = 2.0 * bp - ic1eq;
            ic2eq = 2.0 * lp - ic2eq;
            return bp * 2.0 * R;
        }
    };

    // デシメーション前段のアンチエイリアスフィルタ (8次Butterworth = TPT SVF LPF x4段)
    // 単純移動平均では折り返し成分の抑制が不十分 (-10〜-25dB程度) で、
    // 中低域の成分が超低域 (1-200Hz) 帯に偽スペクトルとして混入していたため導入。
    struct AntiAliasFilter
    {
        struct Section { double g = 0.0, h = 0.0, R = 0.0, ic1 = 0.0, ic2 = 0.0; };
        std::array<Section, 4> sections;

        void design(double fc, double sr)
        {
            // 8次Butterworthの各2次セクションQ値
            static constexpr double Qs[4] = { 0.5097956, 0.6013449, 0.8999762, 2.5629154 };
            const double g0 = std::tan(std::numbers::pi * fc / sr);
            for (int s = 0; s < 4; ++s)
            {
                auto& sec = sections[static_cast<size_t>(s)];
                sec.g = g0;
                sec.R = 1.0 / (2.0 * Qs[s]);
                sec.h = 1.0 / (1.0 + 2.0 * sec.R * sec.g + sec.g * sec.g);
                sec.ic1 = 0.0;
                sec.ic2 = 0.0;
            }
        }

        void reset()
        {
            for (auto& sec : sections) { sec.ic1 = 0.0; sec.ic2 = 0.0; }
        }

        double process(double x)
        {
            for (auto& sec : sections)
            {
                double hp = (x - (2.0 * sec.R + sec.g) * sec.ic1 - sec.ic2) * sec.h;
                double bp = sec.g * hp + sec.ic1;
                double lp = sec.g * bp + sec.ic2;
                sec.ic1 = 2.0 * bp - sec.ic1;
                sec.ic2 = 2.0 * lp - sec.ic2;
                x = lp;
            }
            return x;
        }
    };

    std::vector<AnalyzerBand> bands;
    std::unique_ptr<std::atomic<float>[]> peaks;
    std::unique_ptr<std::atomic<float>[]> holdPeaks;
    std::atomic<bool> holdEnabled{ false };

    static constexpr int BufferSize = 131072;
    static constexpr int BufferMask = 131071;
    std::vector<float> ringBuffer;
    std::vector<float> localBuf;
    std::atomic<int> writePos{ 0 };
    int readPos{ 0 };

    double sampleRate = 44100.0;

    // マルチレート構成:
    //   フルレート        : 2kHz-25kHz バンド
    //   ミッドレート(~11k) : 200Hz-2kHz バンド (フルレートの 1/4〜1/16)
    //   ローレート (~1.4k) : 1Hz-200Hz バンド (ミッドレートのさらに 1/8)
    static constexpr double MidBandMaxFreq = 2000.0;
    static constexpr int LowSubRatio = 8;

    int midDecimationRatio = 4;
    double midSampleRate = 44100.0 / 4.0;
    double lowSampleRate = 44100.0 / 32.0;
    int midCounter = 0;
    int lowCounter = 0;
    int highBandStart = 436; // このインデックス以降がフルレート処理

    AntiAliasFilter aaMid; // フルレート -> ミッドレート用
    AntiAliasFilter aaLow; // ミッドレート -> ローレート用

    std::atomic<uint64_t> updateCount{ 0 };
};
