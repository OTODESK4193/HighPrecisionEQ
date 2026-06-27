#include "WaveformCatcher.h"

WaveformCatcher::WaveformCatcher(SPSCSlotQueue<WaveformSnapshot>& queueToUse)
    : snapshotQueue(queueToUse)
{
}

void WaveformCatcher::prepareToPlay(double sr, int samplesPerBlock)
{
    (void)samplesPerBlock;
    sampleRate = sr;
    
    // 100ms pre-trigger, 2000ms post-trigger = 2100ms total
    preTriggerSamples = static_cast<size_t>(0.100 * sampleRate);
    postTriggerSamples = static_cast<size_t>(2.000 * sampleRate);
    totalSnapshotSamples = preTriggerSamples + postTriggerSamples;
    
    // Calculate ring buffer size (must be power of two, larger than total snapshot)
    size_t requiredSize = totalSnapshotSamples * 2; 
    size_t pow2Size = nextPowerOfTwo(requiredSize);
    
    dryRingBuffer.resize(pow2Size, 0.0f);
    wetRingBuffer.resize(pow2Size, 0.0f);
    ringBufferMask = pow2Size - 1;
    writeIndex = 0;
    
    state = State::Monitoring;
    envelope = 0.0f;
    
    // Envelope follower coefficients (e.g. 1ms attack, 50ms release)
    attackCoef = std::exp(-1.0f / (0.001f * static_cast<float>(sampleRate)));
    releaseCoef = std::exp(-1.0f / (0.050f * static_cast<float>(sampleRate)));
}

void WaveformCatcher::processSample(float drySample, float wetSample)
{
    // Write to ring buffer
    dryRingBuffer[writeIndex] = drySample;
    wetRingBuffer[writeIndex] = wetSample;
    
    // Envelope tracking (simple peak follower)
    float absVal = std::abs(drySample);
    if (absVal > envelope) {
        envelope = attackCoef * envelope + (1.0f - attackCoef) * absVal;
    } else {
        envelope = releaseCoef * envelope + (1.0f - releaseCoef) * absVal;
    }
    
    if (state == State::Monitoring) {
        // Transient detection
        if (envelope > threshold) {
            state = State::AwaitingCapture;
            samplesUntilEnd = postTriggerSamples;
        }
    } else if (state == State::AwaitingCapture) {
        if (samplesUntilEnd > 0) {
            samplesUntilEnd--;
        } else {
            // Reached the end of the post-trigger window
            transferSnapshot();
            
            // Back to monitoring, clear envelope to avoid immediate re-trigger
            envelope = 0.0f;
            state = State::Monitoring;
        }
    }
    
    // Advance index with bitwise AND
    writeIndex = (writeIndex + 1) & ringBufferMask;
}

void WaveformCatcher::transferSnapshot()
{
    auto* slot = snapshotQueue.getWriteSlot();
    if (slot != nullptr) {
        // Ensure size matches
        if (slot->dryWaveform.size() != totalSnapshotSamples) {
            // Ideally we pre-allocated this in prepareToPlay, but just in case
            slot->resize(totalSnapshotSamples);
        }
        
        slot->sampleRate = sampleRate;
        
        // The current writeIndex is at the end of the post-trigger (we just wrote it).
        // The start of the snapshot is totalSnapshotSamples in the past.
        size_t startIdx = (writeIndex - totalSnapshotSamples + dryRingBuffer.size()) & ringBufferMask;
        
        // Copy to the slot
        for (size_t i = 0; i < totalSnapshotSamples; ++i) {
            size_t srcIdx = (startIdx + i) & ringBufferMask;
            slot->dryWaveform[i] = dryRingBuffer[srcIdx];
            slot->wetWaveform[i] = wetRingBuffer[srcIdx];
        }
        
        snapshotQueue.commitWrite();
    }
}
