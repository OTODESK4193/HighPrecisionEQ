#pragma once

#include <JuceHeader.h>
#include <vector>
#include <atomic>
#include <cmath>
#include <numbers>
#include <memory>
#include <complex>
#include <algorithm>
#include <immintrin.h>

class AnalyzerDSP : public juce::Thread
{
public:
    static constexpr int NumBands = 240;

    AnalyzerDSP();
    ~AnalyzerDSP() override;

    void prepare(double sampleRate);
    void pushAudio(const float* data, int numSamples);
    std::vector<float> getEnergies(); // Returns dB values for GUI

    std::vector<float> getDetailedSpectrum(double fmin, double fmax, int numPoints);
    uint64_t getUpdateCount() const noexcept { return updateCount.load(std::memory_order_acquire); }

    void run() override;

private:
    void processInternal(const float* data, int numSamples);
    void calculateBurgAR(const float* data, int N, std::vector<double>& arCoeffs, double& variance);
    void applyBayesianSmoothing(const std::vector<double>& rawSpectrum);
    double calculateARSpectrumValue(double f, const std::vector<double>& arCoeffs, double variance) const;

    static constexpr int BufferSize = 65536;
    static constexpr int BufferMask = 65535;
    std::vector<float> ringBuffer;
    std::vector<float> localBuf;
    std::atomic<int> writePos{ 0 };
    int readPos{ 0 };

    double sampleRate = 44100.0;
    
    static constexpr int AR_ORDER = 48;
    
    std::vector<double> currentARCoeffs;
    double currentVariance = 1e-6;
    juce::CriticalSection arLock;

    std::vector<double> bandFrequencies;
    std::vector<float> smoothedEnergies;
    std::vector<double> stateEstimate;
    std::vector<double> stateCovariance;

    double Q_process = 1e-4;
    double R_measure = 2.0;
    std::atomic<uint64_t> updateCount{ 0 };
};
