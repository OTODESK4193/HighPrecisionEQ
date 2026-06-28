#pragma once

#include <JuceHeader.h>
#include <vector>
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

    int decimationRatio = 64;
    double lowSampleRate = 44100.0 / 64.0;
    double decimationAccumulator = 0.0;
    int decimationCounter = 0;

    std::atomic<uint64_t> updateCount{ 0 };
};
