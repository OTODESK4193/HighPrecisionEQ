#pragma once
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>
#include <numbers>

// ============================================================================
//  SOSCoefficients.h
//  Butterworth HighPass フィルタの SOS (Second-Order Sections) 係数計算
//
//  設計手順（Oppenheim & Schafer, 2010 に基づく）:
//    1. Butterworth の Q 値を次数 N から算出
//    2. Q 値を昇順ソート（数値安定性向上: Williams & Taylor, 2006）
//    3. Audio EQ Cookbook (Robert Bristow-Johnson) の biquad 公式で
//       各セクションの係数を計算
//    4. 全係数を a0 = 1.0 に正規化（Transposed Direct Form II 用）
//
//  スロープ対応:
//    6  dB/oct → 1次  (1 first-order section)
//    12 dB/oct → 2次  (1 biquad)
//    18 dB/oct → 3次  (1 biquad + 1 first-order)
//    24 dB/oct → 4次  (2 biquads)
//
//  First-order セクションは biquad 構造体に b2=0, a2=0 として格納し、
//  同一の処理ループで扱えるようにしている。
// ============================================================================

/// 1つの Biquad セクションの係数（a0 = 1 に正規化済み）
struct SOSSection
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0;   // 分子係数
    double a1 = 0.0, a2 = 0.0;               // 分母係数
};

class SOSCoefficients
{
public:
    /// フィルタスロープ（= フィルタ次数）
    enum class Slope { dB6 = 1, dB12 = 2, dB18 = 3, dB24 = 4 };

    // ========================================================================
    //  Butterworth HighPass SOS 係数計算
    // ========================================================================

    /// @param cutoffHz   カットオフ周波数 [Hz] (20〜200)
    /// @param sampleRate サンプルレート [Hz]
    /// @param slope      フィルタスロープ
    /// @return SOS セクションのベクタ（Q 昇順ソート済み）
    static inline std::vector<SOSSection> computeHighPass(
        double cutoffHz, double sampleRate, int order, double gainDb)
    {
        if (sampleRate <= 0.0 || cutoffHz <= 0.0) return {};

        const int numBiquads = order / 2;
        const bool hasFirstOrder = (order % 2) != 0;

        std::vector<SOSSection> sections;
        sections.reserve(static_cast<size_t>(numBiquads + (hasFirstOrder ? 1 : 0)));

        // ω₀ = 2π × fc / fs
        const double w0    = 2.0 * std::numbers::pi * cutoffHz / sampleRate;
        const double cosw0 = std::cos(w0);
        const double sinw0 = std::sin(w0);

        // --------------------------------------------------------------------
        //  Butterworth Q 値の計算とソート
        //
        //  N 次 Butterworth の各 biquad セクションの Q 値:
        //    Q_k = 1 / (2 × cos(π(2k+1) / (2N)))   k = 0, ..., N/2-1
        //
        //  Q の小さい順（= ポールが実軸に近い順）に並べることで
        //  数値的に安定なカスケードを構成する。
        // --------------------------------------------------------------------
        struct QIndex { double Q; int k; };
        std::vector<QIndex> qValues;
        qValues.reserve(static_cast<size_t>(numBiquads));

        for (int k = 0; k < numBiquads; ++k)
        {
            double angle = std::numbers::pi * (2.0 * k + 1.0) / (2.0 * order);
            double Q = 1.0 / (2.0 * std::sin(angle));
            
            // Shoulder Smoothing: ゲインが浅いときQを滑らかに0.5に近づける
            double scale = std::clamp(gainDb / -10.0, 0.0, 1.0);
            double scaledQ = 0.5 + (Q - 0.5) * scale;
            
            qValues.push_back({ scaledQ, k });
        }
        std::sort(qValues.begin(), qValues.end(),
            [](const QIndex& a, const QIndex& b) { return a.Q < b.Q; });

        // --------------------------------------------------------------------
        //  First-order セクション（奇数次の場合）
        //  Q が最も低い（最安定）なので先頭に配置
        //
        //  1次 HighPass のバイリニア変換:
        //    K = tan(ω₀/2)
        //    b0 = 1/(1+K),  b1 = -1/(1+K),  b2 = 0
        //    a1 = (K-1)/(1+K),  a2 = 0
        // --------------------------------------------------------------------
        if (hasFirstOrder)
        {
            SOSSection sec;
            double K = std::tan(w0 / 2.0);
            double norm = 1.0 / (1.0 + K);

            sec.b0 =  norm;
            sec.b1 = -norm;
            sec.b2 =  0.0;
            sec.a1 = (K - 1.0) * norm;
            sec.a2 =  0.0;

            sections.push_back(sec);
        }

        // --------------------------------------------------------------------
        //  Biquad セクション（Q 昇順）
        //
        //  Audio EQ Cookbook (Bristow-Johnson) HPF:
        //    α = sin(ω₀) / (2Q)
        //    b0 = (1 + cos(ω₀)) / 2
        //    b1 = -(1 + cos(ω₀))
        //    b2 = (1 + cos(ω₀)) / 2
        //    a0 = 1 + α
        //    a1 = -2 cos(ω₀)
        //    a2 = 1 - α
        //  全て a0 で除算して正規化
        // --------------------------------------------------------------------
        for (const auto& qi : qValues)
        {
            SOSSection sec;
            double alpha = sinw0 / (2.0 * qi.Q);
            double a0 = 1.0 + alpha;
            double a0_inv = 1.0 / a0;

            sec.b0 =  (1.0 + cosw0) * 0.5 * a0_inv;
            sec.b1 = -(1.0 + cosw0)       * a0_inv;
            sec.b2 =  (1.0 + cosw0) * 0.5 * a0_inv;
            sec.a1 = -2.0 * cosw0          * a0_inv;
            sec.a2 =  (1.0 - alpha)        * a0_inv;

            sections.push_back(sec);
        }

        return sections;
    }

    // ========================================================================
    //  DF2T 初期条件計算（Gustafsson, 1996 に基づく）
    // ========================================================================

    /// 1 セクション分の DF2T 初期状態（単位入力あたり）を計算
    ///
    /// Transposed Direct Form II:
    ///   y[n]    = b0·x[n] + s1[n]
    ///   s1[n+1] = b1·x[n] − a1·y[n] + s2[n]
    ///   s2[n+1] = b2·x[n] − a2·y[n]
    ///
    /// 定常状態（x[n] = 1, y[n] = G = (b0+b1+b2)/(1+a1+a2)）を
    /// 線形方程式系 (I − A)·zi = B として解く:
    ///
    ///   zi[0] = (b1 + b2 − (a1+a2)·b0) / (1 + a1 + a2)
    ///   zi[1] = (b2·(1+a1) − a2·(b0+b1)) / (1 + a1 + a2)
    ///
    /// 使用時は x[0]（最初のサンプル値）を乗じて実際の初期状態とする。
    ///
    /// @return {zi0, zi1}
    static inline std::array<double, 2> computeZi(const SOSSection& sec)
    {
        double sumA = 1.0 + sec.a1 + sec.a2;

        if (std::abs(sumA) < 1e-15)
            return { 0.0, 0.0 };

        double inv = 1.0 / sumA;
        double zi0 = (sec.b1 + sec.b2 - (sec.a1 + sec.a2) * sec.b0) * inv;
        double zi1 = (sec.b2 * (1.0 + sec.a1) - sec.a2 * (sec.b0 + sec.b1)) * inv;

        return { zi0, zi1 };
    }

    /// DC ゲイン H(1) = (b0+b1+b2) / (1+a1+a2)
    static inline double dcGain(const SOSSection& sec)
    {
        double sumA = 1.0 + sec.a1 + sec.a2;
        if (std::abs(sumA) < 1e-15) return 0.0;
        return (sec.b0 + sec.b1 + sec.b2) / sumA;
    }

    static inline int getFilterOrder(Slope slope) noexcept
    {
        return static_cast<int>(slope);
    }

    static inline int getNumSections(Slope slope) noexcept
    {
        int order = static_cast<int>(slope);
        return order / 2 + order % 2;
    }
};
