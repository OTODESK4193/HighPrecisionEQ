#pragma once
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
#include <numbers>

// ============================================================================
//  TPTSVF.h
//  Topology-Preserving Transform State Variable Filter
//  Butterworth HighPass フィルタの TPT SVF 係数計算と構造体
//
//  Biquad (DF2T) に比べ、極めて低いカットオフ（5Hz等）やハイサンプリング
//  レート（192kHz等）においても、係数の丸め誤差やトランケーションノイズが
//  ほぼ発生しない、非常に数値的安定性の高いフィルター構造です。
// ============================================================================

struct TPTSection
{
    bool isFirstOrder = false;
    bool isBell = false;
    double g = 0.0;
    double h = 0.0;
    double R2PlusG = 0.0; // 2R + g
    double k = 0.0;
    double kV = 0.0;
    
    // DCゲイン (Gustafsson初期化における入力伝播用)
    double getDCGain() const { return isBell ? 1.0 : 0.0; }
};

class TPTSVFCoefficients
{
public:
    enum class Slope { dB6 = 1, dB12 = 2, dB18 = 3, dB24 = 4 };

    // ========================================================================
    //  Butterworth HighPass TPT 係数計算
    // ========================================================================
    static inline void computeHighPass(
        double cutoffHz, double sampleRate, int order, double gainDb, std::vector<TPTSection>& sections)
    {
        sections.clear();
        if (sampleRate <= 0.0 || cutoffHz <= 0.0) return;

        const int numBiquads = order / 2;
        const bool hasFirstOrder = (order % 2) != 0;

        // Prewarp angular frequency
        const double wd = 2.0 * std::numbers::pi * cutoffHz;
        const double T = 1.0 / sampleRate;
        const double wa = (2.0 / T) * std::tan(wd * T / 2.0);
        const double g = wa * T / 2.0;

        // 1次フィルタ (Odd order)
        if (hasFirstOrder)
        {
            TPTSection sec;
            sec.isFirstOrder = true;
            sec.g = g;
            sec.h = 1.0 / (1.0 + g); // 1次用の h
            sections.push_back(sec);
        }

        // 2次フィルタ (Q値の昇順ソート用、最大4つの要素に対応する固定配列を使用)
        struct QIndex { double Q = 0.0; int k = 0; };
        std::array<QIndex, 4> qValues;
        
        // orderは最大8なので、numBiquadsは最大4
        const int safeNumBiquads = std::min(numBiquads, 4);

        for (int k = 0; k < safeNumBiquads; ++k)
        {
            double angle = std::numbers::pi * (2.0 * k + 1.0) / (2.0 * order);
            double Q = 1.0 / (2.0 * std::sin(angle));
            
            // Shoulder Smoothing
            double scale = std::clamp(gainDb / -10.0, 0.0, 1.0);
            double scaledQ = 0.5 + (Q - 0.5) * scale;
            
            qValues[static_cast<size_t>(k)] = { scaledQ, k };
        }
        
        std::sort(qValues.begin(), qValues.begin() + safeNumBiquads,
            [](const QIndex& a, const QIndex& b) { return a.Q < b.Q; });

        for (int k = 0; k < safeNumBiquads; ++k)
        {
            const auto& qi = qValues[static_cast<size_t>(k)];
            TPTSection sec;
            sec.isFirstOrder = false;
            sec.g = g;
            double R = 1.0 / (2.0 * qi.Q);
            sec.R2PlusG = 2.0 * R + g;
            sec.h = 1.0 / (1.0 + 2.0 * R * g + g * g);
            sections.push_back(sec);
        }
    }

    // ========================================================================
    //  TPT Bell 係数計算
    // ========================================================================
    static inline TPTSection computeBell(double cutoffHz, double sampleRate, double gainDb, double Q)
    {
        TPTSection sec;
        sec.isFirstOrder = false;
        sec.isBell = true;
        
        if (cutoffHz < 20.0) cutoffHz = 20.0;
        if (cutoffHz > sampleRate * 0.49) cutoffHz = sampleRate * 0.49;
        if (Q < 0.1) Q = 0.1;

        double V = std::pow(10.0, gainDb / 20.0);
        double wd = 2.0 * std::numbers::pi * cutoffHz;
        double T = 1.0 / sampleRate;
        
        sec.g = std::tan(wd * T / 2.0);
        double k_raw = 1.0 / Q;
        
        double V_safe = std::max(V, 1e-6);
        if (gainDb >= 0.0)
        {
            // Boost
            sec.h = 1.0 / (1.0 + sec.g * k_raw + sec.g * sec.g);
            sec.kV = k_raw;
            sec.k = k_raw * V_safe;
        }
        else
        {
            // Cut
            sec.h = 1.0 / (1.0 + sec.g * (k_raw / V_safe) + sec.g * sec.g);
            sec.kV = k_raw / V_safe;
            sec.k = k_raw;
        }
        
        return sec;
    }

    // ========================================================================
    //  Gustafsson 初期状態計算 (TPT SVF 用)
    // ========================================================================
    // TPT SVF のハイパスフィルタにおけるDC入力(x0)に対する定常状態(steady state)は、
    // 非常にシンプルに s1 = 0, s2 = x0 となります。
    // Biquad(DF2T)のような逆行列計算が不要なため、初期化時の数値誤差もゼロです。
    static inline std::array<double, 2> computeZi(const TPTSection& sec, double x0)
    {
        if (sec.isFirstOrder) {
            // 1次の状態変数 s
            return { x0, 0.0 };
        } else {
            // 2次の状態変数 s1, s2
            return { 0.0, x0 };
        }
    }
};
