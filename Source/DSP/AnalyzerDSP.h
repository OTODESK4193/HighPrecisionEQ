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
    static constexpr int NumBands = 480;

    AnalyzerDSP();
    ~AnalyzerDSP() override;

    void prepare(double sampleRate);
    void pushAudio(const float* data, int numSamples);
    std::vector<float> getEnergies(); // Returns dB values for GUI
    uint64_t getUpdateCount() const noexcept { return updateCount.load(std::memory_order_acquire); }

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

        void updateCoeffs(double fc, double Q, double sr)
        {
            const double wd = 2.0 * std::numbers::pi * fc;
            const double T = 1.0 / sr;
            const double wa = (2.0 / T) * std::tan(wd * T / 2.0);
            g = wa * T / 2.0;
            R = 1.0 / (2.0 * Q);
            h = 1.0 / (1.0 + 2.0 * R * g + g * g);
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

    static constexpr int BufferSize = 131072;
    static constexpr int BufferMask = 131071;
    std::vector<float> ringBuffer;
    std::vector<float> localBuf;
    std::atomic<int> writePos{ 0 };
    int readPos{ 0 };

    double sampleRate = 44100.0;
    double attackCoef = 0.0;
    double releaseCoef = 0.0;

    std::atomic<uint64_t> updateCount{ 0 };
};
