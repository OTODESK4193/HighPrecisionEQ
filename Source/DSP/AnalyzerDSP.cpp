#include "AnalyzerDSP.h"

AnalyzerDSP::AnalyzerDSP()
    : juce::Thread("AnalyzerDSP")
{
    ringBuffer.resize(BufferSize, 0.0f);
    localBuf.resize(BufferSize, 0.0f);
    
    currentARCoeffs.resize(AR_ORDER, 0.0);
    smoothedEnergies.resize(NumBands, -180.0f);
    bandFrequencies.resize(NumBands, 100.0);
    stateEstimate.resize(NumBands, -80.0);
    stateCovariance.resize(NumBands, 1.0);

    startThread();
}

AnalyzerDSP::~AnalyzerDSP()
{
    stopThread(2000);
}

void AnalyzerDSP::prepare(double newSampleRate)
{
    sampleRate = newSampleRate;

    double fmin = 10.0;
    double fmax = std::min(24000.0, sampleRate * 0.49);

    for (int i = 0; i < NumBands; ++i)
    {
        bandFrequencies[static_cast<size_t>(i)] = fmin * std::pow(fmax / fmin, static_cast<double>(i) / (NumBands - 1));
        smoothedEnergies[static_cast<size_t>(i)] = -180.0f;
        stateEstimate[static_cast<size_t>(i)] = -100.0;
        stateCovariance[static_cast<size_t>(i)] = 1.0;
    }

    writePos.store(0, std::memory_order_relaxed);
    readPos = 0;
    std::fill(ringBuffer.begin(), ringBuffer.end(), 0.0f);

    {
        const juce::CriticalSection::ScopedLockType sl(arLock);
        std::fill(currentARCoeffs.begin(), currentARCoeffs.end(), 0.0);
        currentVariance = 1e-6;
    }
}

void AnalyzerDSP::pushAudio(const float* data, int numSamples)
{
    int wp = writePos.load(std::memory_order_relaxed);
    for (int i = 0; i < numSamples; ++i)
    {
        ringBuffer[(wp + i) & BufferMask] = data[i];
    }
    writePos.store(wp + numSamples, std::memory_order_release);
}

void AnalyzerDSP::run()
{
    const int analysisSize = 2048;
    const int hopSize = 512; // 512サンプル (約11.6ms) ごとに最新の2048サンプルを解析する
    
    while (!threadShouldExit())
    {
        wait(10); // 短めのウェイトにして俊敏な更新を可能にする

        int currentWrite = writePos.load(std::memory_order_acquire);
        int available = currentWrite - readPos;

        // リングバッファのオーバーフローガード
        if (available > BufferSize)
        {
            readPos = currentWrite - analysisSize;
            available = analysisSize;
        }

        if (available >= hopSize)
        {
            int startReadPos = currentWrite - analysisSize;
            localBuf.resize(static_cast<size_t>(analysisSize));
            
            for (int i = 0; i < analysisSize; ++i)
            {
                localBuf[static_cast<size_t>(i)] = ringBuffer[(startReadPos + i) & BufferMask];
            }
            
            // 進捗を hopSize 単位で更新する
            readPos += (available / hopSize) * hopSize;

            processInternal(localBuf.data(), analysisSize);
        }
    }
}

void AnalyzerDSP::processInternal(const float* data, int numSamples)
{
    std::vector<double> arCoeffs(AR_ORDER, 0.0);
    double variance = 1e-6;

    calculateBurgAR(data, numSamples, arCoeffs, variance);

    {
        const juce::CriticalSection::ScopedLockType sl(arLock);
        currentARCoeffs = arCoeffs;
        currentVariance = variance;
    }

    std::vector<double> rawSpectrum(NumBands);
    for (int b = 0; b < NumBands; ++b)
    {
        double f = bandFrequencies[static_cast<size_t>(b)];
        double val = calculateARSpectrumValue(f, arCoeffs, variance);
        double db = 10.0 * std::log10(std::max(val, 1e-18));
        rawSpectrum[static_cast<size_t>(b)] = db;
    }

    applyBayesianSmoothing(rawSpectrum);
    updateCount.fetch_add(1, std::memory_order_release);
}

void AnalyzerDSP::calculateBurgAR(const float* data, int N, std::vector<double>& arCoeffs, double& variance)
{
    arCoeffs.assign(AR_ORDER, 0.0);
    
    std::vector<double> f(static_cast<size_t>(N));
    std::vector<double> b(static_cast<size_t>(N));
    
    double error = 0.0;
    for (int i = 0; i < N; ++i)
    {
        double val = static_cast<double>(data[i]);
        f[static_cast<size_t>(i)] = val;
        b[static_cast<size_t>(i)] = val;
        error += val * val;
    }
    
    error /= N;
    
    std::vector<double> a(AR_ORDER, 0.0);
    std::vector<double> a_prev(AR_ORDER, 0.0);

    for (int m = 1; m <= AR_ORDER; ++m)
    {
        double num = 0.0;
        double den = 0.0;

        int limit = N - 1;
        int i = m;

        __m256d num_sum = _mm256_setzero_pd();
        __m256d den_sum = _mm256_setzero_pd();

        for (; i <= limit - 3; i += 4)
        {
            __m256d ef_vec = _mm256_loadu_pd(&f[static_cast<size_t>(i)]);
            __m256d eb_vec = _mm256_loadu_pd(&b[static_cast<size_t>(i - 1)]);

            num_sum = _mm256_add_pd(num_sum, _mm256_mul_pd(ef_vec, eb_vec));

            __m256d ef_sq = _mm256_mul_pd(ef_vec, ef_vec);
            __m256d eb_sq = _mm256_mul_pd(eb_vec, eb_vec);
            den_sum = _mm256_add_pd(den_sum, _mm256_add_pd(ef_sq, eb_sq));
        }

        alignas(32) double num_arr[4];
        alignas(32) double den_arr[4];
        _mm256_store_pd(num_arr, num_sum);
        _mm256_store_pd(den_arr, den_sum);

        num = num_arr[0] + num_arr[1] + num_arr[2] + num_arr[3];
        den = den_arr[0] + den_arr[1] + den_arr[2] + den_arr[3];

        for (; i <= limit; ++i)
        {
            double ef_val = f[static_cast<size_t>(i)];
            double eb_val = b[static_cast<size_t>(i - 1)];
            num += ef_val * eb_val;
            den += ef_val * ef_val + eb_val * eb_val;
        }

        num *= -2.0;

        if (den <= 1e-15)
        {
            break;
        }

        double k = num / den;
        k = std::clamp(k, -0.999999, 0.999999);

        a[static_cast<size_t>(m - 1)] = k;
        if (m > 1)
        {
            for (int j = 0; j < m - 1; ++j)
            {
                a[static_cast<size_t>(j)] = a_prev[static_cast<size_t>(j)] + k * a_prev[static_cast<size_t>(m - 2 - j)];
            }
        }
        
        a_prev = a;

        for (int j = N - 1; j >= m; --j)
        {
            double ef_prev = f[static_cast<size_t>(j)];
            double eb_prev = b[static_cast<size_t>(j - 1)];

            f[static_cast<size_t>(j)] = ef_prev + k * eb_prev;
            b[static_cast<size_t>(j)] = eb_prev + k * ef_prev;
        }

        error *= (1.0 - k * k);
    }

    arCoeffs = a;
    variance = std::max(error, 1e-12);
}

double AnalyzerDSP::calculateARSpectrumValue(double f, const std::vector<double>& arCoeffs, double variance) const
{
    double w = 2.0 * std::numbers::pi * f / sampleRate;
    double cos_w = std::cos(w);
    double sin_w = std::sin(w);

    double re = 1.0;
    double im = 0.0;

    // z^(k+1) = e^(-i*w*(k+1)) の逐次計算用変数
    double z_re = cos_w;
    double z_im = -sin_w;

    const int order = static_cast<int>(arCoeffs.size());
    for (int k = 0; k < order; ++k)
    {
        double ak = arCoeffs[static_cast<size_t>(k)];
        re += ak * z_re;
        im += ak * z_im;

        // 複素乗算による z^(k+2) の更新: (z_re + i*z_im) * (cos_w - i*sin_w)
        double next_z_re = z_re * cos_w + z_im * sin_w;
        double next_z_im = z_im * cos_w - z_re * sin_w;
        z_re = next_z_re;
        z_im = next_z_im;
    }

    double magSq = re * re + im * im;
    if (magSq <= 1e-30) return 0.0;
    return variance / magSq;
}

void AnalyzerDSP::applyBayesianSmoothing(const std::vector<double>& rawSpectrum)
{
    for (int b = 0; b < NumBands; ++b)
    {
        double y = rawSpectrum[static_cast<size_t>(b)];

        double x_pred = stateEstimate[static_cast<size_t>(b)];
        double P_pred = stateCovariance[static_cast<size_t>(b)] + Q_process;

        double K = P_pred / (P_pred + R_measure);
        double x_est = x_pred + K * (y - x_pred);
        double P_est = (1.0 - K) * P_pred;

        stateEstimate[static_cast<size_t>(b)] = x_est;
        stateCovariance[static_cast<size_t>(b)] = P_est;

        smoothedEnergies[static_cast<size_t>(b)] = std::clamp(static_cast<float>(x_est), -180.0f, 20.0f);
    }
}

std::vector<float> AnalyzerDSP::getEnergies()
{
    std::vector<float> energies(NumBands);
    for (int i = 0; i < NumBands; ++i)
    {
        energies[static_cast<size_t>(i)] = smoothedEnergies[static_cast<size_t>(i)];
    }
    return energies;
}

std::vector<float> AnalyzerDSP::getDetailedSpectrum(double fmin, double fmax, int numPoints)
{
    std::vector<float> detailedSpec(static_cast<size_t>(numPoints), -180.0f);
    std::vector<double> arCoeffs;
    double variance = 1e-6;

    {
        const juce::CriticalSection::ScopedLockType sl(arLock);
        arCoeffs = currentARCoeffs;
        variance = currentVariance;
    }

    if (arCoeffs.size() < AR_ORDER || variance <= 1e-12)
        return detailedSpec;

    double logFmin = std::log(std::max(1.0, fmin));
    double logFmax = std::log(std::max(10.0, fmax));

    for (int i = 0; i < numPoints; ++i)
    {
        double logF = logFmin + (logFmax - logFmin) * (static_cast<double>(i) / (numPoints - 1));
        double f = std::exp(logF);

        double val = calculateARSpectrumValue(f, arCoeffs, variance);
        double db = 10.0 * std::log10(std::max(val, 1e-18));

        detailedSpec[static_cast<size_t>(i)] = std::clamp(static_cast<float>(db), -180.0f, 20.0f);
    }

    return detailedSpec;
}
