#include "MinimumPhaseEQ.h"
#include <cstring>

namespace
{
    void setupSlotLayout(std::vector<MinimumPhaseEQ::FilterSection>& v)
    {
        using FS = MinimumPhaseEQ::FilterSection;

        // 0: LC 1次
        v[0].type = FS::Type::HighPass;
        v[0].isFirstOrder = true;

        // 1-8: LC 2次
        for (int k = 1; k <= 8; ++k)
        {
            v[static_cast<size_t>(k)].type = FS::Type::HighPass;
            v[static_cast<size_t>(k)].isFirstOrder = false;
        }

        // 9: HC 1次
        v[9].type = FS::Type::LowPass;
        v[9].isFirstOrder = true;

        // 10-17: HC 2次
        for (int k = 10; k <= 17; ++k)
        {
            v[static_cast<size_t>(k)].type = FS::Type::LowPass;
            v[static_cast<size_t>(k)].isFirstOrder = false;
        }

        // 18-21: Bell
        for (int k = 18; k <= 21; ++k)
        {
            v[static_cast<size_t>(k)].type = FS::Type::Bell;
            v[static_cast<size_t>(k)].isFirstOrder = false;
        }

        for (auto& sec : v)
        {
            sec.active = false;
            sec.reset();
        }
    }
}

MinimumPhaseEQ::MinimumPhaseEQ()
{
    sections.resize(NumSlots);
    fadeSections.resize(NumSlots);
    displaySections.resize(NumSlots);
    setupSlotLayout(sections);
    setupSlotLayout(fadeSections);
    setupSlotLayout(displaySections);
}

void MinimumPhaseEQ::prepare(double sampleRate, int maxBlockSize)
{
    currentSampleRate = sampleRate;
    currentMaxBlockSize = maxBlockSize;

    // 一次スムージングの時定数 約15ms (サブブロック単位で進行)
    smoothAlpha = 1.0 - std::exp(-static_cast<double>(SubBlockSize) / (0.015 * sampleRate));

    // 構造変更時のクロスフェード長 約10ms
    fadeLength = std::max(64, static_cast<int>(std::lround(0.010 * sampleRate)));

    fadeScratch.setSize(2, std::max(maxBlockSize, SubBlockSize));
    dummyRight.assign(static_cast<size_t>(std::max(maxBlockSize, SubBlockSize)), 0.0f);

    reset();
}

void MinimumPhaseEQ::reset()
{
    for (auto& sec : sections)
        sec.reset();
    for (auto& sec : fadeSections)
        sec.reset();

    fadeRemaining = 0;
    firstApply = true;
    hasLastPushed = false; // 次の updateParameters を必ず反映させる
}

double MinimumPhaseEQ::butterworthSectionQ(int order, int k) noexcept
{
    double angle = std::numbers::pi * (2.0 * k + 1.0) / (2.0 * order);
    double Q_butterworth = 1.0 / (2.0 * std::sin(angle));

    // Bessel-Butterworth ハイブリッド: ポストエコー（リンギング）抑制
    // 先頭セクションのQを控えめに、後段はButterworth本来のQを維持
    constexpr double alpha = 0.25; // スムージング強度
    constexpr double beta = 1.2;   // 後段への減衰率
    double smoothing = 1.0 - alpha * std::exp(-beta * static_cast<double>(k));

    return Q_butterworth * smoothing;
}

void MinimumPhaseEQ::computeStructure(const Targets& t,
                                      std::array<bool, NumSlots>& outActive,
                                      std::array<double, NumSlots>& outQ) const noexcept
{
    outActive.fill(false);
    outQ.fill(1.0);

    // A. LowCut (HighPass)
    if (t.lcEnable && t.lcOrder > 0)
    {
        int order = std::clamp(t.lcOrder, 1, 16);
        int numBiquads = order / 2;
        bool hasFirstOrder = (order % 2) != 0;

        outActive[LcFoSlot] = hasFirstOrder;
        for (int k = 0; k < numBiquads; ++k)
        {
            outActive[static_cast<size_t>(LcBqStart + k)] = true;
            outQ[static_cast<size_t>(LcBqStart + k)] = butterworthSectionQ(order, k);
        }
    }

    // B. HighCut (LowPass)
    if (t.hcEnable && t.hcOrder > 0)
    {
        int order = std::clamp(t.hcOrder, 1, 16);
        int numBiquads = order / 2;
        bool hasFirstOrder = (order % 2) != 0;

        outActive[HcFoSlot] = hasFirstOrder;
        for (int k = 0; k < numBiquads; ++k)
        {
            outActive[static_cast<size_t>(HcBqStart + k)] = true;
            outQ[static_cast<size_t>(HcBqStart + k)] = butterworthSectionQ(order, k);
        }
    }

    // C. Bell: 構造上は常時アクティブ。ON/OFFはゲインの0dBスムージングで表現し、
    //    0dB到達時は identity フラグにより処理をスキップする (CPUコストほぼゼロ)。
    for (int i = 0; i < NumBells; ++i)
        outActive[static_cast<size_t>(BellStart + i)] = true;
}

void MinimumPhaseEQ::updateParameters(double lowCutFreq, int lowCutOrder, bool lowCutEnable, double lowCutGainDb,
                                      double highCutFreq, int highCutOrder, bool highCutEnable, double highCutGainDb,
                                      const std::array<BellParam, 4>& bells)
{
    juce::ignoreUnused(lowCutGainDb, highCutGainDb);

    Targets t;
    t.lcFreq = lowCutFreq;
    t.lcOrder = std::clamp(lowCutOrder, 1, 16);
    t.lcEnable = lowCutEnable;
    t.hcFreq = highCutFreq;
    t.hcOrder = std::clamp(highCutOrder, 1, 16);
    t.hcEnable = highCutEnable;
    t.bells = bells;

    // 変更検出: 無変更ならゼロコストで戻る (毎ブロック呼ばれるため重要)
    // GUIスレッド(updateGraph)とオーディオスレッド(processBlock)の両方から
    // 呼ばれるため、検出も含めて paramLock で直列化する
    {
        const juce::CriticalSection::ScopedLockType sl(paramLock);
        if (hasLastPushed && t == lastPushed)
            return;

        lastPushed = t;
        hasLastPushed = true;
        uiTargets = t;
        targetsChanged = true;
    }

    rebuildDisplaySections(t);
}

void MinimumPhaseEQ::applyTargets(const Targets& t)
{
    std::array<bool, NumSlots> act{};
    std::array<double, NumSlots> qArr{};
    computeStructure(t, act, qArr);

    // 構造変更の検出 (カットスロットのみ。Bellスロットは構造固定)
    bool structural = false;
    if (!firstApply)
    {
        for (int s = 0; s < BellStart; ++s)
        {
            if (sections[static_cast<size_t>(s)].active != act[static_cast<size_t>(s)])
            {
                structural = true;
                break;
            }
        }
    }

    if (structural)
    {
        // 旧チェーンを凍結コピーしてクロスフェード開始 (両vectorは同サイズのため再確保なし)
        fadeSections = sections;
        fadeRemaining = fadeLength;
    }

    for (int s = 0; s < NumSlots; ++s)
    {
        bool wasActive = sections[static_cast<size_t>(s)].active;
        sections[static_cast<size_t>(s)].active = act[static_cast<size_t>(s)];
        if (act[static_cast<size_t>(s)] && !wasActive)
            sections[static_cast<size_t>(s)].reset();
    }

    // --- スムージングターゲットの設定 ---
    double lcLog = std::log(std::max(t.lcFreq, 1.0));
    double hcLog = std::log(std::max(t.hcFreq, 1.0));

    smooth[LcFoSlot].logFreq.tgt = lcLog;
    for (int k = 0; k < NumCutBq; ++k)
    {
        auto& sm = smooth[static_cast<size_t>(LcBqStart + k)];
        sm.logFreq.tgt = lcLog;
        sm.q.tgt = qArr[static_cast<size_t>(LcBqStart + k)];
    }

    smooth[HcFoSlot].logFreq.tgt = hcLog;
    for (int k = 0; k < NumCutBq; ++k)
    {
        auto& sm = smooth[static_cast<size_t>(HcBqStart + k)];
        sm.logFreq.tgt = hcLog;
        sm.q.tgt = qArr[static_cast<size_t>(HcBqStart + k)];
    }

    for (int i = 0; i < NumBells; ++i)
    {
        auto& sm = smooth[static_cast<size_t>(BellStart + i)];
        const auto& b = t.bells[static_cast<size_t>(i)];
        sm.logFreq.tgt = std::log(std::max(b.freq, 10.0));
        sm.q.tgt = std::max(b.q, 0.05);
        sm.gainDb.tgt = b.active ? b.gain : 0.0; // OFF = 0dBへスムージング
    }

    // 初回适用と構造変更時はカット系スロットをスナップ
    // (初回は無音から、構造変更時はクロスフェードがジャンプを隠すため安全)
    if (firstApply || structural)
    {
        for (int s = 0; s < BellStart; ++s)
        {
            auto& sm = smooth[static_cast<size_t>(s)];
            sm.logFreq.snap(sm.logFreq.tgt);
            sm.gainDb.snap(sm.gainDb.tgt);
            sm.q.snap(sm.q.tgt);
        }
    }
    if (firstApply)
    {
        for (int s = BellStart; s < NumSlots; ++s)
        {
            auto& sm = smooth[static_cast<size_t>(s)];
            sm.logFreq.snap(sm.logFreq.tgt);
            sm.gainDb.snap(sm.gainDb.tgt);
            sm.q.snap(sm.q.tgt);
        }
        fadeRemaining = 0;
    }

    firstApply = false;
    slotDirty.fill(true); // 次のサブブロックで係数を再計算
}

void MinimumPhaseEQ::advanceSmoothing()
{
    for (int s = 0; s < NumSlots; ++s)
    {
        if (!slotDirty[static_cast<size_t>(s)])
            continue;

        auto& sm = smooth[static_cast<size_t>(s)];
        auto& sec = sections[static_cast<size_t>(s)];

        bool moving = false;
        moving |= sm.logFreq.advance(smoothAlpha, 1e-5);
        moving |= sm.gainDb.advance(smoothAlpha, 1e-3);
        moving |= sm.q.advance(smoothAlpha, 1e-4 * std::max(1.0, std::abs(sm.q.tgt)));

        if (sec.active)
        {
            sec.freq = std::exp(sm.logFreq.cur);
            sec.gain = sm.gainDb.cur;
            sec.q = sm.q.cur;

            bool wasIdentity = sec.identity;
            sec.updateCoefficients(currentSampleRate);

            // パススルー状態から復帰する瞬間は係数がほぼ恒等のため、
            // 古い状態変数を流し込まないようリセットする (ポップノイズ防止)
            if (wasIdentity && !sec.identity)
                sec.reset();
        }

        if (!moving)
            slotDirty[static_cast<size_t>(s)] = false;
    }
}

void MinimumPhaseEQ::process(juce::AudioBuffer<float>& buffer)
{
    // パラメータターゲットの取り込み (try-lockのため音声処理をブロックしない)
    bool doApply = false;
    {
        const juce::CriticalSection::ScopedTryLockType sl(paramLock);
        if (sl.isLocked() && targetsChanged)
        {
            audioTargets = uiTargets;
            targetsChanged = false;
            doApply = true;
        }
    }
    if (doApply)
        applyTargets(audioTargets);

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0)
        return;

    float* left = buffer.getWritePointer(0);
    float* right = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    if (right == nullptr)
    {
        // モノラル時のダミーRチャンネル (prepareで確保済み。足りない場合のみ稀に再確保)
        if (dummyRight.size() < static_cast<size_t>(numSamples))
            dummyRight.resize(static_cast<size_t>(numSamples));
        std::memset(dummyRight.data(), 0, static_cast<size_t>(numSamples) * sizeof(float));
        right = dummyRight.data();
    }

    if (fadeRemaining > 0 && fadeScratch.getNumSamples() < SubBlockSize)
        fadeScratch.setSize(2, SubBlockSize, false, false, true);

    int pos = 0;
    while (pos < numSamples)
    {
        const int n = std::min(SubBlockSize, numSamples - pos);

        // サブブロック境界で係数を漸進更新 (ジッパーノイズ防止)
        advanceSmoothing();

        if (fadeRemaining > 0)
        {
            // 構造変更中: 旧チェーンと新チェーンを並走させてクロスフェード
            float* scratchL = fadeScratch.getWritePointer(0);
            float* scratchR = fadeScratch.getWritePointer(1);
            std::memcpy(scratchL, left + pos, static_cast<size_t>(n) * sizeof(float));
            std::memcpy(scratchR, right + pos, static_cast<size_t>(n) * sizeof(float));

            processChain(sections, left + pos, right + pos, n);
            processChain(fadeSections, scratchL, scratchR, n);

            for (int k = 0; k < n; ++k)
            {
                if (fadeRemaining > 0)
                {
                    // レイズドコサイン形状: 端点で傾きがゼロになり、
                    // フェード開始/終了時の2階差分スパイク (可聴クリック) を防ぐ
                    double progress = 1.0 - static_cast<double>(fadeRemaining) / static_cast<double>(fadeLength);
                    double wNew = 0.5 * (1.0 - std::cos(std::numbers::pi * progress));
                    left[pos + k] = static_cast<float>(wNew * left[pos + k] + (1.0 - wNew) * scratchL[k]);
                    right[pos + k] = static_cast<float>(wNew * right[pos + k] + (1.0 - wNew) * scratchR[k]);
                    --fadeRemaining;
                }
            }
        }
        else
        {
            processChain(sections, left + pos, right + pos, n);
        }

        pos += n;
    }
}

void MinimumPhaseEQ::processChain(std::vector<FilterSection>& secs, float* left, float* right, int numSamples)
{
    const __m256d two_vec = _mm256_set1_pd(2.0);

    for (int i = 0; i < numSamples; ++i)
    {
        double x_L = static_cast<double>(left[i]);
        double x_R = static_cast<double>(right[i]);
        __m256d x = _mm256_setr_pd(x_L, x_R, 0.0, 0.0);

        // スロットは LC(HPF) -> HC(LPF) -> Bell の順に固定配置されている
        for (int s = 0; s < NumSlots; ++s)
        {
            auto& sec = secs[static_cast<size_t>(s)];
            if (!sec.active || sec.identity)
                continue;

            if (sec.type == FilterSection::Type::HighPass)
            {
                __m256d s1_vec = _mm256_load_pd(sec.s1);
                __m256d s2_vec = _mm256_load_pd(sec.s2);
                __m256d g_vec = _mm256_set1_pd(sec.g);
                __m256d D_vec = _mm256_set1_pd(sec.D);

                if (sec.isFirstOrder)
                {
                    __m256d hp = _mm256_mul_pd(_mm256_sub_pd(x, s1_vec), D_vec);
                    __m256d v1 = _mm256_mul_pd(g_vec, hp);
                    s1_vec = _mm256_add_pd(s1_vec, _mm256_mul_pd(two_vec, v1));
                    x = hp;
                }
                else
                {
                    __m256d R2_vec = _mm256_set1_pd(sec.R2);
                    __m256d R2_plus_g = _mm256_add_pd(R2_vec, g_vec);
                    __m256d tmp = _mm256_sub_pd(_mm256_sub_pd(x, _mm256_mul_pd(R2_plus_g, s1_vec)), s2_vec);
                    __m256d hp = _mm256_mul_pd(tmp, D_vec);
                    __m256d bp = _mm256_add_pd(_mm256_mul_pd(g_vec, hp), s1_vec);
                    s1_vec = _mm256_add_pd(bp, _mm256_mul_pd(g_vec, hp));
                    __m256d lp = _mm256_add_pd(_mm256_mul_pd(g_vec, bp), s2_vec);
                    s2_vec = _mm256_add_pd(lp, _mm256_mul_pd(g_vec, bp));
                    x = hp;
                }

                _mm256_store_pd(sec.s1, s1_vec);
                _mm256_store_pd(sec.s2, s2_vec);
            }
            else if (sec.type == FilterSection::Type::LowPass)
            {
                __m256d s1_vec = _mm256_load_pd(sec.s1);
                __m256d s2_vec = _mm256_load_pd(sec.s2);
                __m256d g_vec = _mm256_set1_pd(sec.g);
                __m256d D_vec = _mm256_set1_pd(sec.D);

                if (sec.isFirstOrder)
                {
                    __m256d lp = _mm256_mul_pd(_mm256_add_pd(_mm256_mul_pd(g_vec, x), s1_vec), D_vec);
                    __m256d v1 = _mm256_sub_pd(lp, s1_vec);
                    s1_vec = _mm256_add_pd(lp, v1);
                    x = lp;
                }
                else
                {
                    __m256d R2_vec = _mm256_set1_pd(sec.R2);
                    __m256d R2_plus_g = _mm256_add_pd(R2_vec, g_vec);
                    __m256d tmp = _mm256_sub_pd(_mm256_sub_pd(x, _mm256_mul_pd(R2_plus_g, s1_vec)), s2_vec);
                    __m256d hp = _mm256_mul_pd(tmp, D_vec);
                    __m256d bp = _mm256_add_pd(_mm256_mul_pd(g_vec, hp), s1_vec);
                    s1_vec = _mm256_add_pd(bp, _mm256_mul_pd(g_vec, hp));
                    __m256d lp = _mm256_add_pd(_mm256_mul_pd(g_vec, bp), s2_vec);
                    s2_vec = _mm256_add_pd(lp, _mm256_mul_pd(g_vec, bp));
                    x = lp;
                }

                _mm256_store_pd(sec.s1, s1_vec);
                _mm256_store_pd(sec.s2, s2_vec);
            }
            else if (sec.type == FilterSection::Type::Bell)
            {
                // Orfanidis Biquad TDF2
                __m256d s1_vec = _mm256_load_pd(sec.s1);
                __m256d s2_vec = _mm256_load_pd(sec.s2);
                __m256d b0_vec = _mm256_set1_pd(sec.b0);
                __m256d b1_vec = _mm256_set1_pd(sec.b1);
                __m256d b2_vec = _mm256_set1_pd(sec.b2);
                __m256d a1_vec = _mm256_set1_pd(sec.a1);
                __m256d a2_vec = _mm256_set1_pd(sec.a2);

                __m256d y_vec = _mm256_add_pd(_mm256_mul_pd(b0_vec, x), s1_vec);
                __m256d new_s1 = _mm256_add_pd(_mm256_sub_pd(_mm256_mul_pd(b1_vec, x), _mm256_mul_pd(a1_vec, y_vec)), s2_vec);
                __m256d new_s2 = _mm256_sub_pd(_mm256_mul_pd(b2_vec, x), _mm256_mul_pd(a2_vec, y_vec));

                _mm256_store_pd(sec.s1, new_s1);
                _mm256_store_pd(sec.s2, new_s2);
                x = y_vec;
            }
        }

        alignas(32) double out_samples[4];
        _mm256_store_pd(out_samples, x);

        left[i] = static_cast<float>(out_samples[0]);
        right[i] = static_cast<float>(out_samples[1]);
    }
}

void MinimumPhaseEQ::rebuildDisplaySections(const Targets& t)
{
    std::array<bool, NumSlots> act{};
    std::array<double, NumSlots> qArr{};
    computeStructure(t, act, qArr);

    const juce::CriticalSection::ScopedLockType sl(displayLock);

    for (int s = 0; s < NumSlots; ++s)
    {
        auto& d = displaySections[static_cast<size_t>(s)];
        d.active = act[static_cast<size_t>(s)];
        if (!d.active)
            continue;

        if (s < HcFoSlot)
        {
            d.freq = t.lcFreq;
            d.q = qArr[static_cast<size_t>(s)];
        }
        else if (s < BellStart)
        {
            d.freq = t.hcFreq;
            d.q = qArr[static_cast<size_t>(s)];
        }
        else
        {
            const auto& b = t.bells[static_cast<size_t>(s - BellStart)];
            d.freq = b.freq;
            d.q = b.q;
            d.gain = b.active ? b.gain : 0.0;
        }

        d.updateCoefficients(currentSampleRate);
    }
}

double MinimumPhaseEQ::getMagnitudeForFrequency(double freq) const
{
    const juce::CriticalSection::ScopedLockType sl(displayLock);

    double mag = 1.0;
    for (const auto& sec : displaySections)
    {
        if (sec.active && !sec.identity)
            mag *= sec.getMagnitudeForFrequency(freq, currentSampleRate);
    }
    return mag;
}

void MinimumPhaseEQ::getMagnitudeCurve(const double* freqs, double* outMags, int numPoints) const
{
    const juce::CriticalSection::ScopedLockType sl(displayLock);

    for (int i = 0; i < numPoints; ++i)
    {
        double mag = 1.0;
        for (const auto& sec : displaySections)
        {
            if (sec.active && !sec.identity)
                mag *= sec.getMagnitudeForFrequency(freqs[i], currentSampleRate);
        }
        outMags[i] = mag;
    }
}
