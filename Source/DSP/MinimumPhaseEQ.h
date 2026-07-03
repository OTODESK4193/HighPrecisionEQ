#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>
#include <cmath>
#include <numbers>
#include <algorithm>
#include <immintrin.h>

// ミニマムフェーズEQ
//
// 改修点 (2026-07):
//  - パラメータスムージング: freq(対数)/gain/q をサブブロック(32サンプル)単位で
//    一次スムージングし、係数を漸進更新。オートメーション時のジッパーノイズを解消。
//  - 構造変更 (カットのON/OFF・次数変更) 時は旧チェーンと新チェーンを約10msで
//    クロスフェードし、クリックを解消。
//  - フィルタスロットを固定化 (LC 9 + HC 9 + Bell 4 = 22)。バンド追加/削除で
//    他バンドの状態がリセットされる問題を解消。
//  - Bell の ON/OFF はゲインを 0dB へスムージングして実現 (構造変更なし)。
//  - optimizeBells (設定ゲインの内部補正・近接バンドQ自動低減) を撤去。
//    設定値どおりに動作する。
//  - updateParameters はパラメータ変更検出付き。無変更時は完全にゼロコスト。
//    オーディオスレッドでのヒープ確保も撤廃。
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

        bool operator==(const BellParam&) const = default;
    };

    struct FilterSection
    {
        enum class Type { Bypass, HighPass, LowPass, Bell };
        Type type = Type::Bypass;

        double freq = 1000.0;
        double gain = 0.0;
        double q = 1.0;
        bool active = false;
        bool isFirstOrder = false;
        bool identity = false; // Bellが実質0dB(パススルー係数)のとき true -> 処理スキップ

        // TPT 係数
        double g = 0.0;
        double R2 = 0.0;
        double D = 0.0;

        // Biquad coefficients for Bell (Orfanidis)
        double b0 = 1.0;
        double b1 = 0.0;
        double b2 = 0.0;
        double a1 = 0.0;
        double a2 = 0.0;

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
            if (sr <= 0.0)
                return;

            double f_min = (type == Type::Bell) ? 10.0 : 1.0;
            double f_safe = std::clamp(freq, f_min, sr * 0.49);
            double wd = 2.0 * std::numbers::pi * f_safe;
            double T = 1.0 / sr;
            g = std::tan(wd * T / 2.0);
            identity = false;

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
                    identity = true;
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

                    if (G >= 1.0)
                    {
                        double u = std::sqrt(G);
                        double B = (W * W + c) * delta_w / (u * W * c_sqrt);

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
                        double B = (W * W + c) * delta_w / (u * W * c_sqrt);

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
                double num_imag = -(b1 * sin_w + b2 * sin_2w);

                double den_real = 1.0 + a1 * cos_w + a2 * cos_2w;
                double den_imag = -(a1 * sin_w + a2 * sin_2w);

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

    // シグネチャは従来互換 (lowCutGainDb / highCutGainDb は従来から実効なしのため未使用)
    void updateParameters(double lowCutFreq, int lowCutOrder, bool lowCutEnable, double lowCutGainDb,
                          double highCutFreq, int highCutOrder, bool highCutEnable, double highCutGainDb,
                          const std::array<BellParam, 4>& bells);

    void process(juce::AudioBuffer<float>& buffer);

    // GUIプロット用の全体の振幅応答の算出
    double getMagnitudeForFrequency(double freq) const;

private:
    // 固定スロット配置:
    //   0        : LowCut 1次セクション
    //   1 - 8    : LowCut 2次セクション (最大8 = 96dB/oct)
    //   9        : HighCut 1次セクション
    //   10 - 17  : HighCut 2次セクション
    //   18 - 21  : Bell 1-4
    static constexpr int LcFoSlot = 0;
    static constexpr int LcBqStart = 1;
    static constexpr int NumCutBq = 8;
    static constexpr int HcFoSlot = 9;
    static constexpr int HcBqStart = 10;
    static constexpr int BellStart = 18;
    static constexpr int NumBells = 4;
    static constexpr int NumSlots = 22;

    static constexpr int SubBlockSize = 32;

    struct Targets
    {
        double lcFreq = 80.0;
        int lcOrder = 4;
        bool lcEnable = true;
        double hcFreq = 20000.0;
        int hcOrder = 4;
        bool hcEnable = false;
        std::array<BellParam, 4> bells{};

        bool operator==(const Targets&) const = default;
    };

    struct Smoother
    {
        double cur = 0.0;
        double tgt = 0.0;

        void snap(double v) noexcept { cur = v; tgt = v; }

        // 戻り値: まだ動いている間 true
        bool advance(double alpha, double eps) noexcept
        {
            double d = tgt - cur;
            if (std::abs(d) <= eps) { cur = tgt; return false; }
            cur += alpha * d;
            return true;
        }
    };

    struct SlotSmooth
    {
        Smoother logFreq; // 対数領域でスムージング
        Smoother gainDb;
        Smoother q;
    };

    double currentSampleRate = 44100.0;
    int currentMaxBlockSize = 512;

    // --- オーディオスレッド側状態 ---
    std::vector<FilterSection> sections;     // 現行チェーン
    std::vector<FilterSection> fadeSections; // クロスフェード用の旧チェーン (凍結)
    std::array<SlotSmooth, NumSlots> smooth{};
    std::array<bool, NumSlots> slotDirty{};
    double smoothAlpha = 0.05; // サブブロックごとの一次スムージング係数
    int fadeRemaining = 0;
    int fadeLength = 441;
    bool firstApply = true;
    Targets audioTargets;

    // --- パラメータ受け渡し (updateParameters -> process) ---
    Targets uiTargets;
    bool targetsChanged = false;
    juce::CriticalSection paramLock;

    // updateParameters 側の変更検出 (呼び出し元スレッドでのみ使用)
    Targets lastPushed;
    bool hasLastPushed = false;

    // --- GUI表示用スナップショット ---
    std::vector<FilterSection> displaySections;
    mutable juce::CriticalSection displayLock;

    // --- 作業バッファ (prepareで確保、オーディオスレッドでの確保なし) ---
    juce::AudioBuffer<float> fadeScratch;
    std::vector<float> dummyRight;

    static double butterworthSectionQ(int order, int k) noexcept;
    void computeStructure(const Targets& t,
                          std::array<bool, NumSlots>& outActive,
                          std::array<double, NumSlots>& outQ) const noexcept;
    void applyTargets(const Targets& t);
    void advanceSmoothing();
    void rebuildDisplaySections(const Targets& t);
    void processChain(std::vector<FilterSection>& secs, float* left, float* right, int numSamples);
};
