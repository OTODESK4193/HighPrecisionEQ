#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>
#include <cmath>
#include <numbers>
#include <algorithm>
#include <immintrin.h>

class MinimumPhaseEQ
{
public:
    MinimumPhaseEQ();
    ~MinimumPhaseEQ() = default;

    struct BellParam
    {
        double freq = 1000.0;
        double gain = 0.0;
        double q = 1.0;
        bool active = false;
    };

    struct FilterSection
    {
        enum class Type { Bypass, HighPass, LowPass, Bell };
        Type type = Type::Bypass;

        double freq = 1000.0;
        double gain = 0.0;
        double q = 1.0;
        bool active = false;

        // TPT 係数
        double g = 0.0;
        double D = 0.0;
        double R2 = 0.0;
        double kV = 0.0;
        double k = 0.0;

        bool isFirstOrder = false;

        // 状態変数 (AVX2でのロード/ストア用に32バイトアライメント、ダミーを含む4要素)
        alignas(32) double s1[4] = { 0.0, 0.0, 0.0, 0.0 };
        alignas(32) double s2[4] = { 0.0, 0.0, 0.0, 0.0 };

        void reset()
        {
            s1[0] = 0.0; s1[1] = 0.0; s1[2] = 0.0; s1[3] = 0.0;
            s2[0] = 0.0; s2[1] = 0.0; s2[2] = 0.0; s2[3] = 0.0;
        }

        void updateCoefficients(double sr)
        {
            if (sr <= 0.0 || !active)
            {
                type = Type::Bypass;
                return;
            }

            double f_safe = std::clamp(freq, 1.0, sr * 0.49);
            double wd = 2.0 * std::numbers::pi * f_safe;
            double T = 1.0 / sr;
            g = std::tan(wd * T / 2.0);

            if (type == Type::HighPass || type == Type::LowPass)
            {
                if (isFirstOrder)
                {
                    D = 1.0 / (1.0 + g);
                }
                else
                {
                    double q_safe = std::max(q, 0.1);
                    R2 = 1.0 / q_safe;
                    D = 1.0 / (1.0 + g * R2 + g * g);
                }
            }
            else if (type == Type::Bell)
            {
                double q_safe = std::max(q, 0.05);
                double V = std::pow(10.0, gain / 20.0);
                double k_raw = 1.0 / q_safe;
                double V_safe = std::max(V, 1e-6);

                if (gain >= 0.0)
                {
                    // Boost
                    D = 1.0 / (1.0 + g * k_raw + g * g);
                    kV = k_raw;
                    k = k_raw * V_safe;
                }
                else
                {
                    // Cut
                    D = 1.0 / (1.0 + g * (k_raw / V_safe) + g * g);
                    kV = k_raw / V_safe;
                    k = k_raw;
                }
            }
        }

        // 周波数レスポンス計算 (GUIプロット用)
        double getMagnitudeForFrequency(double f, double sr) const
        {
            if (!active || type == Type::Bypass) return 1.0;

            double w = 2.0 * std::numbers::pi * f / sr;
            double cosw = std::cos(w);
            double cos2w = std::cos(2.0 * w);

            double b0 = 1.0, b1 = 0.0, b2 = 0.0;
            double a0 = 1.0, a1 = 0.0, a2 = 0.0;

            if (type == Type::HighPass)
            {
                if (isFirstOrder)
                {
                    // 1次HPF: H(z) = (1 - z^-1) / ((1+1/g) - (1-1/g)z^-1)
                    double g_inv = 1.0 / g;
                    a0 = 1.0 + g_inv;
                    double a1_raw = 1.0 - g_inv;
                    b0 = 1.0 / a0;
                    b1 = -1.0 / a0;
                    a1 = a1_raw / a0;
                    
                    double numRe = b0 + b1 * cosw;
                    double numIm = -b1 * std::sin(w);
                    double denRe = 1.0 + a1 * cosw;
                    double denIm = -a1 * std::sin(w);
                    return std::sqrt((numRe * numRe + numIm * numIm) / (denRe * denRe + denIm * denIm));
                }
                else
                {
                    // 2次HPF: Butterworth
                    double alpha = std::sin(2.0 * std::numbers::pi * freq / sr) / (2.0 * q);
                    double cosw0 = std::cos(2.0 * std::numbers::pi * freq / sr);
                    a0 = 1.0 + alpha;
                    b0 = ((1.0 + cosw0) / 2.0) / a0;
                    b1 = (-(1.0 + cosw0)) / a0;
                    b2 = ((1.0 + cosw0) / 2.0) / a0;
                    a1 = (-2.0 * cosw0) / a0;
                    a2 = (1.0 - alpha) / a0;
                }
            }
            else if (type == Type::LowPass)
            {
                if (isFirstOrder)
                {
                    // 1次LPF
                    double g_inv = 1.0 / g;
                    a0 = 1.0 + g_inv;
                    double a1_raw = 1.0 - g_inv;
                    b0 = 1.0 / a0;
                    b1 = 1.0 / a0;
                    a1 = a1_raw / a0;

                    double numRe = b0 + b1 * cosw;
                    double numIm = -b1 * std::sin(w);
                    double denRe = 1.0 + a1 * cosw;
                    double denIm = -a1 * std::sin(w);
                    return std::sqrt((numRe * numRe + numIm * numIm) / (denRe * denRe + denIm * denIm));
                }
                else
                {
                    // 2次LPF
                    double alpha = std::sin(2.0 * std::numbers::pi * freq / sr) / (2.0 * q);
                    double cosw0 = std::cos(2.0 * std::numbers::pi * freq / sr);
                    a0 = 1.0 + alpha;
                    b0 = ((1.0 - cosw0) / 2.0) / a0;
                    b1 = (1.0 - cosw0) / a0;
                    b2 = ((1.0 - cosw0) / 2.0) / a0;
                    a1 = (-2.0 * cosw0) / a0;
                    a2 = (1.0 - alpha) / a0;
                }
            }
            else if (type == Type::Bell)
            {
                double A = std::pow(10.0, gain / 40.0);
                double w0 = 2.0 * std::numbers::pi * freq / sr;
                double alpha = std::sin(w0) / (2.0 * q);
                a0 = 1.0 + alpha / A;
                b0 = (1.0 + alpha * A) / a0;
                b1 = (-2.0 * std::cos(w0)) / a0;
                b2 = (1.0 - alpha * A) / a0;
                a1 = (-2.0 * std::cos(w0)) / a0;
                a2 = (1.0 - alpha / A) / a0;
            }

            double numRe = b0 + b1 * cosw + b2 * cos2w;
            double numIm = -b1 * std::sin(w) - b2 * std::sin(2.0 * w);
            double numMagSq = numRe * numRe + numIm * numIm;

            double denRe = 1.0 + a1 * cosw + a2 * cos2w;
            double denIm = -a1 * std::sin(w) - a2 * std::sin(2.0 * w);
            double denMagSq = denRe * denRe + denIm * denIm;

            if (denMagSq <= 0.0) return 1.0;
            return std::sqrt(numMagSq / denMagSq);
        }
    };

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    void updateParameters(double lowCutFreq, int lowCutOrder, bool lowCutEnable,
                          double highCutFreq, int highCutOrder, bool highCutEnable,
                          const std::array<BellParam, 4>& bells);

    void process(juce::AudioBuffer<float>& buffer);

    // GUIプロット用の全体の振幅応答の算出
    double getMagnitudeForFrequency(double freq) const;

private:
    double currentSampleRate = 44100.0;
    int currentMaxBlockSize = 512;

    static constexpr size_t MaxSections = 12;
    std::vector<FilterSection> activeSections;
    std::vector<FilterSection> pendingSections;
    std::vector<BellParam> targetBells;

    bool parametersNeedUpdate = false;
    juce::CriticalSection lock;

    void optimizeBells(std::array<BellParam, 4>& optimizedBells);
    double getBellCascadeMagnitude(double freq, const std::array<BellParam, 4>& testBells) const;
};
