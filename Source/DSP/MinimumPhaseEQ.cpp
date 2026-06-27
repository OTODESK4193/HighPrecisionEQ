#include "MinimumPhaseEQ.h"

MinimumPhaseEQ::MinimumPhaseEQ()
{
    activeSections.resize(MaxSections);
    pendingSections.resize(MaxSections);
    targetBells.resize(4);

    reset();
}

void MinimumPhaseEQ::prepare(double sampleRate, int maxBlockSize)
{
    currentSampleRate = sampleRate;
    currentMaxBlockSize = maxBlockSize;
    reset();
}

void MinimumPhaseEQ::reset()
{
    const juce::CriticalSection::ScopedLockType sl(lock);
    for (auto& sec : activeSections)
        sec.reset();
    for (auto& sec : pendingSections)
        sec.reset();
}

void MinimumPhaseEQ::updateParameters(double lowCutFreq, int lowCutOrder, bool lowCutEnable, double lowCutGainDb,
                                      double highCutFreq, int highCutOrder, bool highCutEnable, double highCutGainDb,
                                      const std::array<BellParam, 4>& bells)
{
    const juce::CriticalSection::ScopedLockType sl(lock);

    currentLowCutEnable = lowCutEnable;
    currentHighCutEnable = highCutEnable;
    currentLowCutGainDb = lowCutGainDb;
    currentHighCutGainDb = highCutGainDb;

    // フィルターのDry/Wetミックスは激しいコムフィルタリング（位相干渉）を引き起こすため廃止し、常に100%適用とする
    lowCutMix = 1.0;
    highCutMix = 1.0;

    for (size_t i = 0; i < 4; ++i)
    {
        targetBells[i] = bells[i];
    }

    std::array<BellParam, 4> optimizedBells;
    optimizeBells(optimizedBells);

    std::vector<FilterSection> newSections;
    newSections.reserve(MaxSections);

    // A. LowCut (HighPass)
    if (lowCutEnable && lowCutOrder > 0)
    {
        int order = std::clamp(lowCutOrder, 1, 16);
        int numBiquads = order / 2;
        bool hasFirstOrder = (order % 2) != 0;

        if (hasFirstOrder)
        {
            FilterSection sec;
            sec.type = FilterSection::Type::HighPass;
            sec.freq = lowCutFreq;
            sec.active = true;
            sec.isFirstOrder = true;
            sec.updateCoefficients(currentSampleRate);
            newSections.push_back(sec);
        }

        for (int k = 0; k < numBiquads; ++k)
        {
            FilterSection sec;
            sec.type = FilterSection::Type::HighPass;
            sec.freq = lowCutFreq;
            sec.active = true;
            sec.isFirstOrder = false;
            
            double angle = std::numbers::pi * (2.0 * k + 1.0) / (2.0 * order);
            double Q_butterworth = 1.0 / (2.0 * std::sin(angle));
            
            // Bessel-Butterworth ハイブリッド: ポストエコー（リンギング）抑制
            // 先頭セクションのQを控えめに、後段はButterworth本来のQを維持
            constexpr double alpha = 0.25;  // スムージング強度
            constexpr double beta  = 1.2;   // 後段への減衰率
            double smoothing = 1.0 - alpha * std::exp(-beta * static_cast<double>(k));
            sec.q = Q_butterworth * smoothing;

            sec.updateCoefficients(currentSampleRate);
            newSections.push_back(sec);
        }
    }

    // B. HighCut (LowPass)
    if (highCutEnable && highCutOrder > 0)
    {
        int order = std::clamp(highCutOrder, 1, 16);
        int numBiquads = order / 2;
        bool hasFirstOrder = (order % 2) != 0;

        if (hasFirstOrder)
        {
            FilterSection sec;
            sec.type = FilterSection::Type::LowPass;
            sec.freq = highCutFreq;
            sec.active = true;
            sec.isFirstOrder = true;
            sec.updateCoefficients(currentSampleRate);
            newSections.push_back(sec);
        }

        for (int k = 0; k < numBiquads; ++k)
        {
            FilterSection sec;
            sec.type = FilterSection::Type::LowPass;
            sec.freq = highCutFreq;
            sec.active = true;
            sec.isFirstOrder = false;

            double angle = std::numbers::pi * (2.0 * k + 1.0) / (2.0 * order);
            double Q_butterworth = 1.0 / (2.0 * std::sin(angle));
            
            // Bessel-Butterworth ハイブリッド: ポストエコー（リンギング）抑制
            // 先頭セクションのQを控えめに、後段はButterworth本来のQを維持
            constexpr double alpha = 0.25;  // スムージング強度
            constexpr double beta  = 1.2;   // 後段への減衰率
            double smoothing = 1.0 - alpha * std::exp(-beta * static_cast<double>(k));
            sec.q = Q_butterworth * smoothing;

            sec.updateCoefficients(currentSampleRate);
            newSections.push_back(sec);
        }
    }

    // C. 4バンド Bell
    for (int i = 0; i < 4; ++i)
    {
        if (optimizedBells[static_cast<size_t>(i)].active)
        {
            FilterSection sec;
            sec.type = FilterSection::Type::Bell;
            sec.freq = optimizedBells[static_cast<size_t>(i)].freq;
            sec.gain = optimizedBells[static_cast<size_t>(i)].gain;
            sec.q = optimizedBells[static_cast<size_t>(i)].q;
            sec.active = true;
            sec.isFirstOrder = false;
            sec.updateCoefficients(currentSampleRate);
            newSections.push_back(sec);
        }
    }

    while (newSections.size() < MaxSections)
    {
        FilterSection sec;
        sec.type = FilterSection::Type::Bypass;
        sec.active = false;
        newSections.push_back(sec);
    }

    for (size_t i = 0; i < MaxSections; ++i)
    {
        if (i < activeSections.size())
        {
            newSections[i].s1[0] = activeSections[i].s1[0];
            newSections[i].s1[1] = activeSections[i].s1[1];
            newSections[i].s2[0] = activeSections[i].s2[0];
            newSections[i].s2[1] = activeSections[i].s2[1];
        }
        pendingSections[i] = newSections[i];
    }

    parametersNeedUpdate = true;
}

void MinimumPhaseEQ::process(juce::AudioBuffer<float>& buffer)
{
    {
        const juce::CriticalSection::ScopedTryLockType sl(lock);
        if (parametersNeedUpdate)
        {
            activeSections = pendingSections;
            parametersNeedUpdate = false;
        }
    }

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0) return;

    float* left = buffer.getWritePointer(0);
    float* right = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    std::vector<float> dummyRight;
    if (right == nullptr)
    {
        dummyRight.assign(static_cast<size_t>(numSamples), 0.0f);
        right = dummyRight.data();
    }

    const __m256d two_vec = _mm256_set1_pd(2.0);

    for (int i = 0; i < numSamples; ++i)
    {
        double x_L = static_cast<double>(left[i]);
        double x_R = static_cast<double>(right[i]);
        __m256d x = _mm256_setr_pd(x_L, x_R, 0.0, 0.0);

        // A. LowCut 処理 (HPF)
        __m256d x_lc_in = x;
        for (auto& sec : activeSections)
        {
            if (sec.type != FilterSection::Type::HighPass || !sec.active)
                continue;

            __m256d s1_vec = _mm256_load_pd(sec.s1);
            __m256d s2_vec = _mm256_load_pd(sec.s2);
            __m256d g_vec = _mm256_set1_pd(sec.g);
            __m256d D_vec = _mm256_set1_pd(sec.D);
            __m256d R2_vec = _mm256_set1_pd(sec.R2);

            __m256d y_vec = x;

            if (sec.isFirstOrder)
            {
                __m256d hp = _mm256_mul_pd(_mm256_sub_pd(x, s1_vec), D_vec);
                __m256d v1 = _mm256_mul_pd(g_vec, hp);
                s1_vec = _mm256_add_pd(s1_vec, _mm256_mul_pd(two_vec, v1));
                y_vec = hp;
            }
            else
            {
                __m256d R2_plus_g = _mm256_add_pd(R2_vec, g_vec);
                __m256d tmp = _mm256_sub_pd(_mm256_sub_pd(x, _mm256_mul_pd(R2_plus_g, s1_vec)), s2_vec);
                __m256d hp = _mm256_mul_pd(tmp, D_vec);
                __m256d bp = _mm256_add_pd(_mm256_mul_pd(g_vec, hp), s1_vec);
                s1_vec = _mm256_add_pd(bp, _mm256_mul_pd(g_vec, hp));
                __m256d lp = _mm256_add_pd(_mm256_mul_pd(g_vec, bp), s2_vec);
                s2_vec = _mm256_add_pd(lp, _mm256_mul_pd(g_vec, bp));
                y_vec = hp;
            }

            _mm256_store_pd(sec.s1, s1_vec);
            _mm256_store_pd(sec.s2, s2_vec);
            x = y_vec;
        }
        
        if (currentLowCutEnable)
        {
            __m256d mix_vec = _mm256_set1_pd(lowCutMix);
            __m256d one_minus_mix = _mm256_set1_pd(1.0 - lowCutMix);
            x = _mm256_add_pd(_mm256_mul_pd(one_minus_mix, x_lc_in), _mm256_mul_pd(mix_vec, x));
        }

        // B. HighCut 処理 (LPF)
        __m256d x_hc_in = x;
        for (auto& sec : activeSections)
        {
            if (sec.type != FilterSection::Type::LowPass || !sec.active)
                continue;

            __m256d s1_vec = _mm256_load_pd(sec.s1);
            __m256d s2_vec = _mm256_load_pd(sec.s2);
            __m256d g_vec = _mm256_set1_pd(sec.g);
            __m256d D_vec = _mm256_set1_pd(sec.D);
            __m256d R2_vec = _mm256_set1_pd(sec.R2);

            __m256d y_vec = x;

            if (sec.isFirstOrder)
            {
                __m256d lp = _mm256_mul_pd(_mm256_add_pd(_mm256_mul_pd(g_vec, x), s1_vec), D_vec);
                __m256d v1 = _mm256_sub_pd(lp, s1_vec);
                s1_vec = _mm256_add_pd(lp, v1);
                y_vec = lp;
            }
            else
            {
                __m256d R2_plus_g = _mm256_add_pd(R2_vec, g_vec);
                __m256d tmp = _mm256_sub_pd(_mm256_sub_pd(x, _mm256_mul_pd(R2_plus_g, s1_vec)), s2_vec);
                __m256d hp = _mm256_mul_pd(tmp, D_vec);
                __m256d bp = _mm256_add_pd(_mm256_mul_pd(g_vec, hp), s1_vec);
                s1_vec = _mm256_add_pd(bp, _mm256_mul_pd(g_vec, hp));
                __m256d lp = _mm256_add_pd(_mm256_mul_pd(g_vec, bp), s2_vec);
                s2_vec = _mm256_add_pd(lp, _mm256_mul_pd(g_vec, bp));
                y_vec = lp;
            }

            _mm256_store_pd(sec.s1, s1_vec);
            _mm256_store_pd(sec.s2, s2_vec);
            x = y_vec;
        }

        if (currentHighCutEnable)
        {
            __m256d mix_vec = _mm256_set1_pd(highCutMix);
            __m256d one_minus_mix = _mm256_set1_pd(1.0 - highCutMix);
            x = _mm256_add_pd(_mm256_mul_pd(one_minus_mix, x_hc_in), _mm256_mul_pd(mix_vec, x));
        }

        // C. Bells 処理 (Orfanidis Biquad TDF2)
        for (auto& sec : activeSections)
        {
            if (sec.type != FilterSection::Type::Bell || !sec.active)
                continue;

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

        alignas(32) double out_samples[4];
        _mm256_store_pd(out_samples, x);

        left[i] = static_cast<float>(out_samples[0]);
        if (numChannels > 1) right[i] = static_cast<float>(out_samples[1]);
    }
}

double MinimumPhaseEQ::getMagnitudeForFrequency(double freq) const
{
    const juce::CriticalSection::ScopedLockType sl(lock);

    // 1. LowCut の応答特性
    double lcMag = 1.0;
    for (const auto& sec : pendingSections)
    {
        if (sec.active && sec.type == FilterSection::Type::HighPass)
        {
            lcMag *= sec.getMagnitudeForFrequency(freq, currentSampleRate);
        }
    }
    if (currentLowCutEnable)
    {
        lcMag = (1.0 - lowCutMix) + lowCutMix * lcMag;
    }

    // 2. HighCut の応答特性
    double hcMag = 1.0;
    for (const auto& sec : pendingSections)
    {
        if (sec.active && sec.type == FilterSection::Type::LowPass)
        {
            hcMag *= sec.getMagnitudeForFrequency(freq, currentSampleRate);
        }
    }
    if (currentHighCutEnable)
    {
        hcMag = (1.0 - highCutMix) + highCutMix * hcMag;
    }

    // 3. Bell の応答特性
    double bellMag = 1.0;
    for (const auto& sec : pendingSections)
    {
        if (sec.active && sec.type == FilterSection::Type::Bell)
        {
            bellMag *= sec.getMagnitudeForFrequency(freq, currentSampleRate);
        }
    }

    return lcMag * hcMag * bellMag;
}

void MinimumPhaseEQ::optimizeBells(std::array<BellParam, 4>& optimizedBells)
{
    for (int i = 0; i < 4; ++i)
    {
        optimizedBells[static_cast<size_t>(i)] = targetBells[static_cast<size_t>(i)];
    }

    int activeCount = 0;
    for (const auto& b : targetBells)
        if (b.active && std::abs(b.gain) > 0.05)
            activeCount++;

    if (activeCount <= 1)
        return;

    const int maxIterations = 8;
    const double stepSize = 0.65;

    for (int iter = 0; iter < maxIterations; ++iter)
    {
        bool converged = true;

        for (int i = 0; i < 4; ++i)
        {
            if (!targetBells[static_cast<size_t>(i)].active)
                continue;

            double f_target = targetBells[static_cast<size_t>(i)].freq;
            double g_target = targetBells[static_cast<size_t>(i)].gain;

            double currentMag = getBellCascadeMagnitude(f_target, optimizedBells);
            double currentGainDb = 20.0 * std::log10(std::max(currentMag, 1e-5));

            double error = g_target - currentGainDb;

            if (std::abs(error) > 0.02)
            {
                converged = false;
                optimizedBells[static_cast<size_t>(i)].gain += stepSize * error;
                double minG = targetBells[static_cast<size_t>(i)].gain - 6.0;
                double maxG = targetBells[static_cast<size_t>(i)].gain + 6.0;
                optimizedBells[static_cast<size_t>(i)].gain = std::clamp(optimizedBells[static_cast<size_t>(i)].gain, minG, maxG);
            }
        }

        if (converged)
            break;
    }

    // 位相歪み対策: 近接バンド間のQ相互干渉補正
    // 2つのBellバンドの中心周波数が1オクターブ以内に近接している場合、
    // 両方のQを自動的に下げて位相回転の急変を平滑化する
    for (int i = 0; i < 4; ++i)
    {
        if (!optimizedBells[static_cast<size_t>(i)].active) continue;
        for (int j = i + 1; j < 4; ++j)
        {
            if (!optimizedBells[static_cast<size_t>(j)].active) continue;

            double ratio = optimizedBells[static_cast<size_t>(i)].freq
                         / optimizedBells[static_cast<size_t>(j)].freq;
            if (ratio < 1.0) ratio = 1.0 / ratio;

            // 1オクターブ以内の近接バンド
            if (ratio < 2.0)
            {
                double proximity = 1.0 - (ratio - 1.0);  // 0〜1 (近いほど1)
                double qReduction = 1.0 - 0.2 * proximity;
                optimizedBells[static_cast<size_t>(i)].q *= qReduction;
                optimizedBells[static_cast<size_t>(j)].q *= qReduction;
            }
        }
    }
}

double MinimumPhaseEQ::getBellCascadeMagnitude(double freq, const std::array<BellParam, 4>& testBells) const
{
    double mag = 1.0;
    for (const auto& b : testBells)
    {
        if (b.active && std::abs(b.gain) > 0.01)
        {
            FilterSection sec;
            sec.type = FilterSection::Type::Bell;
            sec.freq = b.freq;
            sec.gain = b.gain;
            sec.q = b.q;
            sec.active = true;
            sec.isFirstOrder = false;
            sec.updateCoefficients(currentSampleRate);
            mag *= sec.getMagnitudeForFrequency(freq, currentSampleRate);
        }
    }
    return mag;
}
