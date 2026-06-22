#include <immintrin.h>
#include "ZeroPhaseFilter.h"

ZeroPhaseFilter::ZeroPhaseFilter()
{
    prepare(44100.0, 512);
}

void ZeroPhaseFilter::prepare(double sampleRate, int /*maxBlockSize*/)
{
    currentSampleRate = sampleRate;
    processSize = computeProcessSize(sampleRate);
    const int hopSize = processSize / 2;

    channels.clear();
    currentSOS.clear();
    currentSOS.reserve(12);
    pendingSOS.clear();
    pendingSOS.reserve(12);
    newSOSCache.clear();
    newSOSCache.reserve(12);
    currentGainMix = 0.0;
    pendingGainMix = 0.0;
    currentOlaGainMix = 0.0;
    coeffsNeedUpdate = false;
    lastCutoffHz = -1.0;
    lastOrder = 2;
    lastGainMix = -1.0;
    lastLcEnable = false;
    lastGainDb = 0.0;
    lastBells = {};

    // Hann Window for OLA
    hannWindow.resize(static_cast<size_t>(2 * hopSize));
    for (int i = 0; i < 2 * hopSize; ++i)
    {
        hannWindow[static_cast<size_t>(i)] = 0.5 * (1.0 - std::cos(std::numbers::pi * i / hopSize));
    }

    const int windowLen = windowHops * hopSize;
    paddedBuffer.resize(static_cast<size_t>(windowLen), 0.0);
    backwardStateCacheL.resize(maxSections);
    backwardStateCacheR.resize(maxSections);
    paddedBufferBellOnlyDry.resize(static_cast<size_t>(windowLen), 0.0);
    inBufferDryCache.resize(static_cast<size_t>(hopSize), 0.0);

    // ステレオ用プリ・アロケートバッファのリサイズ
    paddedBufferL.resize(static_cast<size_t>(windowLen), 0.0);
    paddedBufferR.resize(static_cast<size_t>(windowLen), 0.0);
    paddedBufferBellOnlyDryL.resize(static_cast<size_t>(windowLen), 0.0);
    paddedBufferBellOnlyDryR.resize(static_cast<size_t>(windowLen), 0.0);
}

void ZeroPhaseFilter::updateParameters(double cutoffHz, int order, double gainDb, bool lcEnable,
                                      const std::array<BellParam, 4>& bells)
{
    bool changed = false;

    if (lcEnable != lastLcEnable || cutoffHz != lastCutoffHz || order != lastOrder || std::abs(gainDb - lastGainDb) > 1e-6)
    {
        lastLcEnable = lcEnable;
        lastCutoffHz = cutoffHz;
        lastOrder = order;
        lastGainDb = gainDb;
        changed = true;
    }

    for (size_t i = 0; i < 4; ++i)
    {
        if (bells[i].active != lastBells[i].active ||
            bells[i].freq != lastBells[i].freq ||
            std::abs(bells[i].gain - lastBells[i].gain) > 1e-6 ||
            bells[i].q != lastBells[i].q)
        {
            lastBells[i] = bells[i];
            changed = true;
        }
    }

    // Wet/Dry ミックス比の計算 (フィルタが1つでも有効なら 1.0、すべて無効なら 0.0)
    bool anyFilterActive = false;
    if (lcEnable && gainDb < -0.01) anyFilterActive = true;
    for (const auto& b : bells)
    {
        if (b.active && std::abs(b.gain) > 0.01) anyFilterActive = true;
    }
    
    double newMix = anyFilterActive ? 1.0 : 0.0;
    if (std::abs(newMix - lastGainMix) > 1e-6)
    {
        pendingGainMix = newMix;
        lastGainMix = newMix;
        changed = true;
    }

    if (changed)
    {
        newSOSCache.clear();
        if (lcEnable && gainDb < -0.01)
        {
            TPTSVFCoefficients::computeHighPass(cutoffHz, currentSampleRate, order, gainDb, newSOSCache);
        }
        for (const auto& b : bells)
        {
            if (b.active && std::abs(b.gain) > 0.01)
            {
                newSOSCache.push_back(TPTSVFCoefficients::computeBell(b.freq, currentSampleRate, b.gain, b.q));
            }
        }
        pendingSOS = newSOSCache;
        coeffsNeedUpdate = true;
    }
}

void ZeroPhaseFilter::reset()
{
    lastLcEnable = false;
    lastGainDb = 0.0;
    lastBells = {};
    lastOrder = 2;
    currentGainMix = 0.0;
    pendingGainMix = 0.0;
    currentOlaGainMix = 0.0;
    coeffsNeedUpdate = false;
    
    for (auto& ch : channels)
    {
        std::fill(ch.inputWindow.begin(), ch.inputWindow.end(), 0.0);
        std::fill(ch.dryWindow.begin(), ch.dryWindow.end(), 0.0);
        std::fill(ch.inBuffer.begin(), ch.inBuffer.end(), 0.0);
        std::fill(ch.olaBuffer.begin(), ch.olaBuffer.end(), 0.0);
        ch.inPos = 0;
        for (auto& state : ch.forwardState) {
            state = {0.0, 0.0};
        }
    }
}

int ZeroPhaseFilter::computeProcessSize(double sampleRate)
{
    juce::ignoreUnused(sampleRate);
    return 1536;
}

void ZeroPhaseFilter::process(juce::AudioBuffer<float>& buffer)
{
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    const int hopSize = processSize / 2;

    if (numSamples == 0 || numChannels == 0 || inBufferDryCache.empty() || currentSampleRate <= 0.0) return;

    if (coeffsNeedUpdate)
    {
        currentSOS = pendingSOS;
        currentGainMix = std::clamp(std::abs(lastGainDb) / 10.0, 0.0, 1.0);
        currentOlaGainMix = pendingGainMix;
        coeffsNeedUpdate = false;
    }

    while (static_cast<int>(channels.size()) < numChannels)
    {
        ChannelState ch;
        ch.inputWindow.resize(static_cast<size_t>(windowHops * hopSize), 0.0);
        ch.dryWindow.resize(static_cast<size_t>(windowHops * hopSize), 0.0);
        ch.inBuffer.resize(static_cast<size_t>(hopSize), 0.0);
        ch.olaBuffer.resize(static_cast<size_t>(2 * hopSize), 0.0);
        ch.inPos = 0;
        ch.forwardState.assign(maxSections, {0.0, 0.0});
        channels.push_back(std::move(ch));
    }

    if (numChannels == 2)
    {
        processStereo(buffer);
        return;
    }

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* channelData = buffer.getWritePointer(ch);
        auto& state = channels[static_cast<size_t>(ch)];

        for (int i = 0; i < numSamples; ++i)
        {
            state.inBuffer[static_cast<size_t>(state.inPos)] = static_cast<double>(channelData[i]);
            
            // OLAバッファから出力 (8*hopSize 前のサンプルが完成形として出てくる)
            channelData[i] = static_cast<float>(state.olaBuffer[static_cast<size_t>(state.inPos)]);
            
            state.inPos++;
            if (state.inPos >= hopSize)
            {
                processCompleteBlock(state);
                state.inPos = 0;
            }
        }
    }
}

void ZeroPhaseFilter::processCompleteBlock(ChannelState& state)
{
    const int H = processSize / 2;
    const int windowLen = windowHops * H; // 42 * H
    
    // 1. Dry信号のコピーを保存
    std::copy(state.dryWindow.begin() + H, state.dryWindow.end(), state.dryWindow.begin());
    std::copy(state.inBuffer.begin(), state.inBuffer.end(), state.dryWindow.end() - H);

    // インデックスによるSOSの分類（アロケーションフリー）
    size_t numLC = 0;
    while (numLC < currentSOS.size() && !currentSOS[numLC].isBell)
    {
        numLC++;
    }
    const size_t numBells = currentSOS.size() - numLC;

    // 2. 順方向フィルターを連続的に適用 (inBuffer を上書き)
    if (currentOlaGainMix >= 1e-6 && !currentSOS.empty())
    {
        std::copy(state.inBuffer.begin(), state.inBuffer.end(), inBufferDryCache.begin());

        // a. ローカット順方向
        if (numLC > 0)
        {
            for (size_t s = 0; s < numLC; ++s)
            {
                svffiltForwardSingleSection(state.inBuffer.data(), H, currentSOS[s], state.forwardState[s]);
            }
            
            // ローカットのゲインミックスを適用
            const double wet = currentGainMix;
            const double dry = 1.0 - wet;
            for (int i = 0; i < H; ++i)
            {
                state.inBuffer[static_cast<size_t>(i)] = state.inBuffer[static_cast<size_t>(i)] * wet + inBufferDryCache[static_cast<size_t>(i)] * dry;
            }
        }
        
        // b. ベル順方向
        if (numBells > 0)
        {
            for (size_t s = 0; s < numBells; ++s)
            {
                size_t idx = numLC + s;
                svffiltForwardSingleSection(state.inBuffer.data(), H, currentSOS[idx], state.forwardState[idx]);
            }
        }
    }
    
    // 3. inputWindow を左にシフトし、右端に (Forward適用済みの) inBuffer を追加
    std::copy(state.inputWindow.begin() + H, state.inputWindow.end(), state.inputWindow.begin());
    std::copy(state.inBuffer.begin(), state.inBuffer.end(), state.inputWindow.end() - H);
    
    // 4. バイパス判定 (フィルタが何も適用されていない場合、または無音サスペンド時)
    if (currentOlaGainMix < 1e-6 || currentSOS.empty() || isSuspended)
    {
        const double* cleanCenter = state.dryWindow.data(); // 先頭が現在の中心ブロック
        for (int i = 0; i < 2 * H; ++i)
        {
            double windowed = cleanCenter[i] * hannWindow[static_cast<size_t>(i)];
            if (i < H) state.olaBuffer[static_cast<size_t>(i)] = state.olaBuffer[static_cast<size_t>(i + H)] + windowed;
            else       state.olaBuffer[static_cast<size_t>(i)] = windowed;
        }
        return;
    }

    // 5. バックワードフィルタリングのための作業バッファを作成
    std::copy(state.inputWindow.begin(), state.inputWindow.end(), paddedBuffer.begin());
    
    // バックワードフィルターの初期状態を設定 (逆順に伝播させることで LowCut 遮断の初期化を設定)
    double rightEdge = paddedBuffer.back();
    for (int s = static_cast<int>(numLC) - 1; s >= 0; --s)
    {
        auto zi = TPTSVFCoefficients::computeZi(currentSOS[static_cast<size_t>(s)], rightEdge);
        backwardStateCacheL[static_cast<size_t>(s)] = { zi[0], zi[1] };
        rightEdge *= currentSOS[static_cast<size_t>(s)].getDCGain();
    }
    
    // LowCutの逆処理を行う前の状態（Bell EQのみ逆処理が完了した状態）をミックス用のDry信号として保存
    if (numLC > 0)
    {
        std::copy(paddedBuffer.begin(), paddedBuffer.end(), paddedBufferBellOnlyDry.begin());
    }
    
    // b. ローカット逆方向
    if (numLC > 0)
    {
        for (int s = static_cast<int>(numLC) - 1; s >= 0; --s)
        {
            size_t idx = static_cast<size_t>(s);
            svffiltBackwardSingleSection(paddedBuffer.data(), windowLen, currentSOS[idx], backwardStateCacheL[idx]);
        }
        
        // ローカットのゲインミックスを適用（Dry信号として、Bellのみかかった状態の paddedBufferBellOnlyDry を使用し対称性を維持）
        const double wet = currentGainMix;
        const double dry = 1.0 - wet;
        for (int i = 0; i < windowLen; ++i)
        {
            paddedBuffer[static_cast<size_t>(i)] = paddedBuffer[static_cast<size_t>(i)] * wet + paddedBufferBellOnlyDry[static_cast<size_t>(i)] * dry;
        }
    }
    
    // 6. Overlap-Add Reconstruction (全体としては OLA 有効なので wet=currentOlaGainMix)
    const double wet = currentOlaGainMix;
    const double dry = 1.0 - wet;
    const double* dryCenter = state.dryWindow.data(); // 干渉していない元の音
    const double* filtCenter = paddedBuffer.data();   // 順逆フィルタリング済みの音
    
    for (int i = 0; i < 2 * H; ++i)
    {
        double mixed = dryCenter[i] * dry + filtCenter[i] * wet;
        double windowed = mixed * hannWindow[static_cast<size_t>(i)];
        if (i < H) state.olaBuffer[static_cast<size_t>(i)] = state.olaBuffer[static_cast<size_t>(i + H)] + windowed;
        else       state.olaBuffer[static_cast<size_t>(i)] = windowed;
    }
}



void ZeroPhaseFilter::svffiltForwardSingleSection(double* data, int numSamples, const TPTSection& sec, std::array<double, 2>& state)
{
    double s1 = state[0];
    double s2 = state[1];
    const double g = sec.g;
    const double h = sec.h;
    const double R2PlusG = sec.R2PlusG;

    if (sec.isBell)
    {
        for (int n = 0; n < numSamples; ++n)
        {
            const double x = data[n];
            const double hp = (x - (sec.kV + g) * s1 - s2) * h;
            const double bp = g * hp + s1;
            const double lp = g * bp + s2;
            s1 = s1 + 2.0 * g * hp;
            s2 = s2 + 2.0 * g * bp;
            
            data[n] = lp + sec.k * bp + hp;
        }
    }
    else if (sec.isFirstOrder)
    {
        for (int n = 0; n < numSamples; ++n)
        {
            const double x = data[n];
            const double v = (x - s1) * h; // h = 1/(1+g)
            double hp = v;
            const double lp = v * g + s1;
            s1 = lp + v * g;
            data[n] = hp;
        }
    }
    else
    {
        for (int n = 0; n < numSamples; ++n)
        {
            const double x = data[n];
            const double hp = h * (x - R2PlusG * s1 - s2);
            const double bp = g * hp + s1;
            const double lp = g * bp + s2;
            s1 = s1 + 2.0 * g * hp;
            s2 = s2 + 2.0 * g * bp;
            data[n] = hp;
        }
    }

    state[0] = s1;
    state[1] = s2;
}

void ZeroPhaseFilter::svffiltBackwardSingleSection(
    double* data, int numSamples,
    const TPTSection& sec,
    std::array<double, 2>& state)
{
    double s1 = state[0];
    double s2 = state[1];
    const double g = sec.g;
    const double h = sec.h;
    const double R2PlusG = sec.R2PlusG;

    if (sec.isBell)
    {
        for (int n = numSamples - 1; n >= 0; --n)
        {
            const double x = data[n];
            const double hp = (x - (sec.kV + g) * s1 - s2) * h;
            const double bp = g * hp + s1;
            const double lp = g * bp + s2;
            s1 = s1 + 2.0 * g * hp;
            s2 = s2 + 2.0 * g * bp;
            
            data[n] = lp + sec.k * bp + hp;
        }
    }
    else if (sec.isFirstOrder)
    {
        for (int n = numSamples - 1; n >= 0; --n)
        {
            const double x = data[n];
            const double v = (x - s1) * h;
            double hp = v;
            const double lp = v * g + s1;
            s1 = lp + v * g;
            data[n] = hp;
        }
    }
    else
    {
        for (int n = numSamples - 1; n >= 0; --n)
        {
            const double x = data[n];
            const double hp = h * (x - R2PlusG * s1 - s2);
            const double bp = g * hp + s1;
            const double lp = g * bp + s2;
            s1 = s1 + 2.0 * g * hp;
            s2 = s2 + 2.0 * g * bp;
            data[n] = hp;
        }
    }

    state[0] = s1;
    state[1] = s2;
}

void ZeroPhaseFilter::processStereo(juce::AudioBuffer<float>& buffer)
{
    const int hopSize = processSize / 2;
    const int numSamples = buffer.getNumSamples();
    
    auto* channelDataL = buffer.getWritePointer(0);
    auto* channelDataR = buffer.getWritePointer(1);
    
    auto& stateL = channels[0];
    auto& stateR = channels[1];

    for (int i = 0; i < numSamples; ++i)
    {
        stateL.inBuffer[static_cast<size_t>(stateL.inPos)] = static_cast<double>(channelDataL[i]);
        stateR.inBuffer[static_cast<size_t>(stateR.inPos)] = static_cast<double>(channelDataR[i]);
        
        // OLAバッファから出力
        channelDataL[i] = static_cast<float>(stateL.olaBuffer[static_cast<size_t>(stateL.inPos)]);
        channelDataR[i] = static_cast<float>(stateR.olaBuffer[static_cast<size_t>(stateR.inPos)]);
        
        stateL.inPos++;
        stateR.inPos++;
        
        if (stateL.inPos >= hopSize)
        {
            processCompleteBlockStereo(stateL, stateR);
            stateL.inPos = 0;
            stateR.inPos = 0;
        }
    }
}

void ZeroPhaseFilter::processCompleteBlockStereo(ChannelState& stateL, ChannelState& stateR)
{
    const int H = processSize / 2;
    const int windowLen = windowHops * H; // 22 * H
    
    // 1. Dry信号のコピーを保存
    std::copy(stateL.dryWindow.begin() + H, stateL.dryWindow.end(), stateL.dryWindow.begin());
    std::copy(stateL.inBuffer.begin(), stateL.inBuffer.end(), stateL.dryWindow.end() - H);
    
    std::copy(stateR.dryWindow.begin() + H, stateR.dryWindow.end(), stateR.dryWindow.begin());
    std::copy(stateR.inBuffer.begin(), stateR.inBuffer.end(), stateR.dryWindow.end() - H);

    // インデックスによるSOSの分類
    size_t numLC = 0;
    while (numLC < currentSOS.size() && !currentSOS[numLC].isBell)
    {
        numLC++;
    }
    const size_t numBells = currentSOS.size() - numLC;

    // 2. 順方向フィルターを連続的に適用 (inBuffer を上書き)
    if (currentOlaGainMix >= 1e-6 && !currentSOS.empty())
    {
        alignas(16) double inBufferDryL[2048];
        alignas(16) double inBufferDryR[2048];
        
        const size_t copySize = static_cast<size_t>(H);
        std::copy_n(stateL.inBuffer.begin(), copySize, inBufferDryL);
        std::copy_n(stateR.inBuffer.begin(), copySize, inBufferDryR);

        // a. ローカット順方向 (Stereo SIMD)
        if (numLC > 0)
        {
            for (size_t s = 0; s < numLC; ++s)
            {
                svffiltForwardSingleSectionStereo(stateL.inBuffer.data(), stateR.inBuffer.data(), H, currentSOS[s], stateL.forwardState[s], stateR.forwardState[s]);
            }
            
            // ローカットのゲインミックスを適用
            const double wet = currentGainMix;
            const double dry = 1.0 - wet;
            for (int i = 0; i < H; ++i)
            {
                stateL.inBuffer[static_cast<size_t>(i)] = stateL.inBuffer[static_cast<size_t>(i)] * wet + inBufferDryL[i] * dry;
                stateR.inBuffer[static_cast<size_t>(i)] = stateR.inBuffer[static_cast<size_t>(i)] * wet + inBufferDryR[i] * dry;
            }
        }
        
        // b. ベル順方向 (Stereo SIMD)
        if (numBells > 0)
        {
            for (size_t s = 0; s < numBells; ++s)
            {
                size_t idx = numLC + s;
                svffiltForwardSingleSectionStereo(stateL.inBuffer.data(), stateR.inBuffer.data(), H, currentSOS[idx], stateL.forwardState[idx], stateR.forwardState[idx]);
            }
        }
    }
    
    // 3. inputWindow を左にシフトし、右端に (Forward適用済みの) inBuffer を追加
    std::copy(stateL.inputWindow.begin() + H, stateL.inputWindow.end(), stateL.inputWindow.begin());
    std::copy(stateL.inBuffer.begin(), stateL.inBuffer.end(), stateL.inputWindow.end() - H);
    
    std::copy(stateR.inputWindow.begin() + H, stateR.inputWindow.end(), stateR.inputWindow.begin());
    std::copy(stateR.inBuffer.begin(), stateR.inBuffer.end(), stateR.inputWindow.end() - H);
    
    // 4. バイパス判定 (フィルタが何も適用されていない場合、または無音サスペンド時)
    if (currentOlaGainMix < 1e-6 || currentSOS.empty() || isSuspended)
    {
        const double* cleanCenterL = stateL.dryWindow.data();
        const double* cleanCenterR = stateR.dryWindow.data();
        for (int i = 0; i < 2 * H; ++i)
        {
            double windowedL = cleanCenterL[i] * hannWindow[static_cast<size_t>(i)];
            if (i < H) stateL.olaBuffer[static_cast<size_t>(i)] = stateL.olaBuffer[static_cast<size_t>(i + H)] + windowedL;
            else       stateL.olaBuffer[static_cast<size_t>(i)] = windowedL;
            
            double windowedR = cleanCenterR[i] * hannWindow[static_cast<size_t>(i)];
            if (i < H) stateR.olaBuffer[static_cast<size_t>(i)] = stateR.olaBuffer[static_cast<size_t>(i + H)] + windowedR;
            else       stateR.olaBuffer[static_cast<size_t>(i)] = windowedR;
        }
        return;
    }

    // 5. バックワードフィルタリングのための作業バッファを作成 (L/R 個別バッファを使用)
    std::copy(stateL.inputWindow.begin(), stateL.inputWindow.end(), paddedBufferL.begin());
    std::copy(stateR.inputWindow.begin(), stateR.inputWindow.end(), paddedBufferR.begin());
    
    // バックワードフィルターの初期状態を設定 (逆順に伝播させることで LowCut 遮断の初期化を設定)
    double rightEdgeL = paddedBufferL.back();
    double rightEdgeR = paddedBufferR.back();
    for (int s = static_cast<int>(numLC) - 1; s >= 0; --s)
    {
        auto ziL = TPTSVFCoefficients::computeZi(currentSOS[static_cast<size_t>(s)], rightEdgeL);
        auto ziR = TPTSVFCoefficients::computeZi(currentSOS[static_cast<size_t>(s)], rightEdgeR);
        
        backwardStateCacheL[static_cast<size_t>(s)] = { ziL[0], ziL[1] };
        backwardStateCacheR[static_cast<size_t>(s)] = { ziR[0], ziR[1] };
        
        rightEdgeL *= currentSOS[static_cast<size_t>(s)].getDCGain();
        rightEdgeR *= currentSOS[static_cast<size_t>(s)].getDCGain();
    }
    
    // LowCutの逆処理を行う前の状態（Bell EQのみ逆処理が完了した状態）をミックス用のDry信号として保存
    if (numLC > 0)
    {
        std::copy(paddedBufferL.begin(), paddedBufferL.end(), paddedBufferBellOnlyDryL.begin());
        std::copy(paddedBufferR.begin(), paddedBufferR.end(), paddedBufferBellOnlyDryR.begin());
    }
    
    // b. ローカット逆方向 (Stereo SIMD)
    if (numLC > 0)
    {
        for (int s = static_cast<int>(numLC) - 1; s >= 0; --s)
        {
            size_t idx = static_cast<size_t>(s);
            svffiltBackwardSingleSectionStereo(paddedBufferL.data(), paddedBufferR.data(), windowLen, currentSOS[idx], backwardStateCacheL[idx], backwardStateCacheR[idx]);
        }
        
        // ローカットのゲインミックスを適用
        const double wet = currentGainMix;
        const double dry = 1.0 - wet;
        for (int i = 0; i < windowLen; ++i)
        {
            paddedBufferL[static_cast<size_t>(i)] = paddedBufferL[static_cast<size_t>(i)] * wet + paddedBufferBellOnlyDryL[static_cast<size_t>(i)] * dry;
            paddedBufferR[static_cast<size_t>(i)] = paddedBufferR[static_cast<size_t>(i)] * wet + paddedBufferBellOnlyDryR[static_cast<size_t>(i)] * dry;
        }
    }
    
    // 6. Overlap-Add Reconstruction (全体としては OLA 有効なので wet=currentOlaGainMix)
    const double wet = currentOlaGainMix;
    const double dry = 1.0 - wet;
    
    const double* dryCenterL = stateL.dryWindow.data();
    const double* dryCenterR = stateR.dryWindow.data();
    const double* filtCenterL = paddedBufferL.data();
    const double* filtCenterR = paddedBufferR.data();
    
    for (int i = 0; i < 2 * H; ++i)
    {
        double mixedL = dryCenterL[i] * dry + filtCenterL[i] * wet;
        double windowedL = mixedL * hannWindow[static_cast<size_t>(i)];
        if (i < H) stateL.olaBuffer[static_cast<size_t>(i)] = stateL.olaBuffer[static_cast<size_t>(i + H)] + windowedL;
        else       stateL.olaBuffer[static_cast<size_t>(i)] = windowedL;
        
        double mixedR = dryCenterR[i] * dry + filtCenterR[i] * wet;
        double windowedR = mixedR * hannWindow[static_cast<size_t>(i)];
        if (i < H) stateR.olaBuffer[static_cast<size_t>(i)] = stateR.olaBuffer[static_cast<size_t>(i + H)] + windowedR;
        else       stateR.olaBuffer[static_cast<size_t>(i)] = windowedR;
    }
}

void ZeroPhaseFilter::svffiltForwardSingleSectionStereo(
    double* dataL, double* dataR, int numSamples,
    const TPTSection& sec,
    std::array<double, 2>& stateL, std::array<double, 2>& stateR)
{
    // SSE2 Intrinsics による並列処理
    __m128d s1 = _mm_set_pd(stateR[0], stateL[0]);
    __m128d s2 = _mm_set_pd(stateR[1], stateL[1]);
    
    const __m128d g = _mm_set1_pd(sec.g);
    const __m128d h = _mm_set1_pd(sec.h);
    const __m128d R2PlusG = _mm_set1_pd(sec.R2PlusG);
    const __m128d two = _mm_set1_pd(2.0);

    if (sec.isBell)
    {
        const __m128d kV = _mm_set1_pd(sec.kV);
        const __m128d k = _mm_set1_pd(sec.k);
        const __m128d kVPlusG = _mm_add_pd(kV, g);
        
        for (int n = 0; n < numSamples; ++n)
        {
            __m128d x = _mm_set_pd(dataR[n], dataL[n]);
            
            // hp = (x - (kV + g) * s1 - s2) * h
            __m128d tmp = _mm_sub_pd(x, _mm_mul_pd(kVPlusG, s1));
            __m128d hp = _mm_mul_pd(_mm_sub_pd(tmp, s2), h);
            
            // bp = g * hp + s1
            __m128d bp = _mm_add_pd(_mm_mul_pd(g, hp), s1);
            
            // lp = g * bp + s2
            __m128d lp = _mm_add_pd(_mm_mul_pd(g, bp), s2);
            
            // s1 = s1 + 2.0 * g * hp
            s1 = _mm_add_pd(s1, _mm_mul_pd(_mm_mul_pd(two, g), hp));
            
            // s2 = s2 + 2.0 * g * bp
            s2 = _mm_add_pd(s2, _mm_mul_pd(_mm_mul_pd(two, g), bp));
            
            // y = lp + k * bp + hp
            __m128d y = _mm_add_pd(_mm_add_pd(lp, _mm_mul_pd(k, bp)), hp);
            
            // 書き戻し
            double out[2];
            _mm_storeu_pd(out, y);
            dataL[n] = out[0];
            dataR[n] = out[1];
        }
    }
    else if (sec.isFirstOrder)
    {
        for (int n = 0; n < numSamples; ++n)
        {
            __m128d x = _mm_set_pd(dataR[n], dataL[n]);
            
            // v = (x - s1) * h
            __m128d hp = _mm_mul_pd(_mm_sub_pd(x, s1), h);
            
            // lp = v * g + s1
            __m128d lp = _mm_add_pd(_mm_mul_pd(hp, g), s1);
            
            // s1 = lp + v * g
            s1 = _mm_add_pd(lp, _mm_mul_pd(hp, g));
            
            // 書き戻し
            double out[2];
            _mm_storeu_pd(out, hp);
            dataL[n] = out[0];
            dataR[n] = out[1];
        }
    }
    else
    {
        for (int n = 0; n < numSamples; ++n)
        {
            __m128d x = _mm_set_pd(dataR[n], dataL[n]);
            
            // hp = h * (x - R2PlusG * s1 - s2)
            __m128d tmp = _mm_sub_pd(x, _mm_mul_pd(R2PlusG, s1));
            __m128d hp = _mm_mul_pd(h, _mm_sub_pd(tmp, s2));
            
            // bp = g * hp + s1
            __m128d bp = _mm_add_pd(_mm_mul_pd(g, hp), s1);
            
            // lp = g * bp + s2
            __m128d lp = _mm_add_pd(_mm_mul_pd(g, bp), s2);
            
            // s1 = s1 + 2.0 * g * hp
            s1 = _mm_add_pd(s1, _mm_mul_pd(_mm_mul_pd(two, g), hp));
            
            // s2 = s2 + 2.0 * g * bp
            s2 = _mm_add_pd(s2, _mm_mul_pd(_mm_mul_pd(two, g), bp));
            
            // 書き戻し
            double out[2];
            _mm_storeu_pd(out, hp);
            dataL[n] = out[0];
            dataR[n] = out[1];
        }
    }
    
    double finalS1[2];
    double finalS2[2];
    _mm_storeu_pd(finalS1, s1);
    _mm_storeu_pd(finalS2, s2);
    stateL[0] = finalS1[0]; stateR[0] = finalS1[1];
    stateL[1] = finalS2[0]; stateR[1] = finalS2[1];
}

void ZeroPhaseFilter::svffiltBackwardSingleSectionStereo(
    double* dataL, double* dataR, int numSamples,
    const TPTSection& sec,
    std::array<double, 2>& stateL, std::array<double, 2>& stateR)
{
    // SSE2 Intrinsics による並列処理 (逆方向)
    __m128d s1 = _mm_set_pd(stateR[0], stateL[0]);
    __m128d s2 = _mm_set_pd(stateR[1], stateL[1]);
    
    const __m128d g = _mm_set1_pd(sec.g);
    const __m128d h = _mm_set1_pd(sec.h);
    const __m128d R2PlusG = _mm_set1_pd(sec.R2PlusG);
    const __m128d two = _mm_set1_pd(2.0);

    if (sec.isBell)
    {
        const __m128d kV = _mm_set1_pd(sec.kV);
        const __m128d k = _mm_set1_pd(sec.k);
        const __m128d kVPlusG = _mm_add_pd(kV, g);
        
        for (int n = numSamples - 1; n >= 0; --n)
        {
            __m128d x = _mm_set_pd(dataR[n], dataL[n]);
            
            // hp = (x - (kV + g) * s1 - s2) * h
            __m128d tmp = _mm_sub_pd(x, _mm_mul_pd(kVPlusG, s1));
            __m128d hp = _mm_mul_pd(_mm_sub_pd(tmp, s2), h);
            
            // bp = g * hp + s1
            __m128d bp = _mm_add_pd(_mm_mul_pd(g, hp), s1);
            
            // lp = g * bp + s2
            __m128d lp = _mm_add_pd(_mm_mul_pd(g, bp), s2);
            
            // s1 = s1 + 2.0 * g * hp
            s1 = _mm_add_pd(s1, _mm_mul_pd(_mm_mul_pd(two, g), hp));
            
            // s2 = s2 + 2.0 * g * bp
            s2 = _mm_add_pd(s2, _mm_mul_pd(_mm_mul_pd(two, g), bp));
            
            // y = lp + k * bp + hp
            __m128d y = _mm_add_pd(_mm_add_pd(lp, _mm_mul_pd(k, bp)), hp);
            
            // 書き戻し
            double out[2];
            _mm_storeu_pd(out, y);
            dataL[n] = out[0];
            dataR[n] = out[1];
        }
    }
    else if (sec.isFirstOrder)
    {
        for (int n = numSamples - 1; n >= 0; --n)
        {
            __m128d x = _mm_set_pd(dataR[n], dataL[n]);
            
            // v = (x - s1) * h
            __m128d hp = _mm_mul_pd(_mm_sub_pd(x, s1), h);
            
            // lp = v * g + s1
            __m128d lp = _mm_add_pd(_mm_mul_pd(hp, g), s1);
            
            // s1 = lp + v * g
            s1 = _mm_add_pd(lp, _mm_mul_pd(hp, g));
            
            // 書き戻し
            double out[2];
            _mm_storeu_pd(out, hp);
            dataL[n] = out[0];
            dataR[n] = out[1];
        }
    }
    else
    {
        for (int n = numSamples - 1; n >= 0; --n)
        {
            __m128d x = _mm_set_pd(dataR[n], dataL[n]);
            
            // hp = h * (x - R2PlusG * s1 - s2)
            __m128d tmp = _mm_sub_pd(x, _mm_mul_pd(R2PlusG, s1));
            __m128d hp = _mm_mul_pd(h, _mm_sub_pd(tmp, s2));
            
            // bp = g * hp + s1
            __m128d bp = _mm_add_pd(_mm_mul_pd(g, hp), s1);
            
            // lp = g * bp + s2
            __m128d lp = _mm_add_pd(_mm_mul_pd(g, bp), s2);
            
            // s1 = s1 + 2.0 * g * hp
            s1 = _mm_add_pd(s1, _mm_mul_pd(_mm_mul_pd(two, g), hp));
            
            // s2 = s2 + 2.0 * g * bp
            s2 = _mm_add_pd(s2, _mm_mul_pd(_mm_mul_pd(two, g), bp));
            
            // 書き戻し
            double out[2];
            _mm_storeu_pd(out, hp);
            dataL[n] = out[0];
            dataR[n] = out[1];
        }
    }
    
    double finalS1[2];
    double finalS2[2];
    _mm_storeu_pd(finalS1, s1);
    _mm_storeu_pd(finalS2, s2);
    stateL[0] = finalS1[0]; stateR[0] = finalS1[1];
    stateL[1] = finalS2[0]; stateR[1] = finalS2[1];
}