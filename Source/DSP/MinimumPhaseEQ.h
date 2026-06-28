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
        double R2 = 0.0;
        double D = 0.0;
        double k = 0.0;
        double kV = 0.0;

        // Biquad coefficients for Bell
        double b0 = 1.0;
        double b1 = 0.0;
        double b2 = 0.0;
        double a1 = 0.0;
        double a2 = 0.0;

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

            double f_min = (type == Type::Bell) ? 10.0 : 1.0;
            double f_safe = std::clamp(freq, f_min, sr * 0.49);
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
                double G0 = 1.0;
                double G = std::pow(10.0, gain / 20.0);
                
                if (std::abs(G - G0) < 1e-5)
                {
                    b0 = 1.0; b1 = 0.0; b2 = 0.0; a1 = 0.0; a2 = 0.0;
                }
                else
                {
                    double q_safe = std::max(q, 0.05);
                    double wc = 2.0 * std::numbers::pi * f_safe / sr;
                    double W = std::tan(wc / 2.0);
                    
                    double delta_w = wc / q_safe;
                    double wn = std::numbers::pi / wc;
                    
                    // Numerically stable Orfanidis coefficients (canceling the denominator avoids NaN)
                    double c_sqrt = W * std::abs(1.0 - wn * wn) / (wn / q_safe);
                    double c = c_sqrt * c_sqrt;
                    double D_val = c_sqrt;
                    
                    if (G >= 1.0)
                    {
                        double u = std::sqrt(G);
                        double B = (W*W + c) * delta_w / (u * W * c_sqrt);
                        
                        double a0_bq = 1.0 + B + c;
                        a1 = -2.0 * std::cos(wc) * (1.0 + c) / a0_bq;
                        a2 = (1.0 - B + c) / a0_bq;
                        
                        b0 = (1.0 + G * B + c) / a0_bq;
                        b1 = -2.0 * std::cos(wc) * (1.0 + c) / a0_bq;
                        b2 = (1.0 - G * B + c) / a0_bq;
                    }
                    else
                    {
                        double G_inv = 1.0 / G;
                        double u = std::sqrt(G_inv);
                        double B = (W*W + c) * delta_w / (u * W * c_sqrt);
                        
                        double a0_bq = 1.0 + G_inv * B + c;
                        a1 = -2.0 * std::cos(wc) * (1.0 + c) / a0_bq;
                        a2 = (1.0 - G_inv * B + c) / a0_bq;
                        
                        b0 = (1.0 + B + c) / a0_bq;
                        b1 = -2.0 * std::cos(wc) * (1.0 + c) / a0_bq;
                        b2 = (1.0 - B + c) / a0_bq;
                    }
                }
            }
        }

        // 周波数レスポンス計算 (GUIプロット用) - TPT SVF 数値安定版
        double getMagnitudeForFrequency(double f, double sr) const
        {
            if (!active || type == Type::Bypass) return 1.0;

            // アナログプリワーピング周波数 Ω = std::tan(π * f / sr)
            // ナイキスト周波数（sr/2）以上での描画の折り返し（エイリアス）を防ぐためクランプする
            double f_eval = std::clamp(f, 1.0, sr * 0.4999);
            double omega = std::tan(std::numbers::pi * f_eval / sr);
            
            // フィルターのカットオフに対するプリワーピング周波数 g_val = std::tan(π * fc / sr)
            double cutoff_safe = std::clamp(freq, 1.0, sr * 0.49);
            double g_val = std::tan(std::numbers::pi * cutoff_safe / sr);
            
            if (g_val <= 0.0) return 1.0;
            
            double R = omega / g_val;
            double R_sq = R * R;

            if (type == Type::HighPass)
            {
                if (isFirstOrder)
                {
                    // 1次HPF: |H| = R / sqrt(R^2 + 1)
                    return R / std::sqrt(R_sq + 1.0);
                }
                else
                {
                    // 2次HPF: |H| = R^2 / sqrt((1 - R^2)^2 + (R/Q)^2)
                    double q_safe = std::max(q, 0.1);
                    double denomSq = (1.0 - R_sq) * (1.0 - R_sq) + (R / q_safe) * (R / q_safe);
                    if (denomSq <= 0.0) return 1.0;
                    return R_sq / std::sqrt(denomSq);
                }
            }
            else if (type == Type::LowPass)
            {
                if (isFirstOrder)
                {
                    // 1次LPF: |H| = 1 / sqrt(R^2 + 1)
                    return 1.0 / std::sqrt(R_sq + 1.0);
                }
                else
                {
                    // 2次LPF: |H| = 1 / sqrt((1 - R^2)^2 + (R/Q)^2)
                    double q_safe = std::max(q, 0.1);
                    double denomSq = (1.0 - R_sq) * (1.0 - R_sq) + (R / q_safe) * (R / q_safe);
                    if (denomSq <= 0.0) return 1.0;
                    return 1.0 / std::sqrt(denomSq);
                }
            }
            else if (type == Type::Bell)
            {
                // Biquad magnitude response: |H(e^jw)| = |b0 + b1 e^-jw + b2 e^-2jw| / |1 + a1 e^-jw + a2 e^-2jw|
                double w = 2.0 * std::numbers::pi * f_eval / sr;
                
                double cos_w = std::cos(w);
                double sin_w = std::sin(w);
                double cos_2w = std::cos(2.0 * w);
                double sin_2w = std::sin(2.0 * w);
                
                double num_real = b0 + b1 * cos_w + b2 * cos_2w;
                double num_imag = - (b1 * sin_w + b2 * sin_2w);
                
                double den_real = 1.0 + a1 * cos_w + a2 * cos_2w;
                double den_imag = - (a1 * sin_w + a2 * sin_2w);
                
                double num_sq = num_real * num_real + num_imag * num_imag;
                double den_sq = den_real * den_real + den_imag * den_imag;
                
                if (den_sq <= 0.0) return 1.0;
                return std::sqrt(num_sq / den_sq);
            }

            return 1.0;
        }
    };

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    void updateParameters(double lowCutFreq, int lowCutOrder, bool lowCutEnable, double lowCutGainDb,
                          double highCutFreq, int highCutOrder, bool highCutEnable, double highCutGainDb,
                          const std::array<BellParam, 4>& bells);

    void process(juce::AudioBuffer<float>& buffer);

    // GUIプロット用の全体の振幅応答の算出
    double getMagnitudeForFrequency(double freq) const;

private:
    double currentSampleRate = 44100.0;
    int currentMaxBlockSize = 512;

    double currentLowCutGainDb = -10.0;
    double currentHighCutGainDb = -10.0;
    double lowCutMix = 1.0;
    double highCutMix = 1.0;

    bool currentLowCutEnable = true;
    bool currentHighCutEnable = false;

    static constexpr size_t MaxSections = 24;
    std::vector<FilterSection> activeSections;
    std::vector<FilterSection> pendingSections;
    std::vector<BellParam> targetBells;

    bool parametersNeedUpdate = false;
    juce::CriticalSection lock;

    void optimizeBells(std::array<BellParam, 4>& optimizedBells);
    double getBellCascadeMagnitude(double freq, const std::array<BellParam, 4>& testBells) const;
};
