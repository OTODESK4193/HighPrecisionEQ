#pragma once
#include <JuceHeader.h>
#include "SPSCQueue.h"
#include <vector>

class WaveformCatcher {
public:
    WaveformCatcher(SPSCSlotQueue<WaveformSnapshot>& queueToUse);

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    
    // Call this per sample in the audio callback
    void processSample(float drySample, float wetSample);

    // Set the threshold for transient detection (amplitude)
    void setThreshold(float newThreshold) { threshold = newThreshold; }

private:
    SPSCSlotQueue<WaveformSnapshot>& snapshotQueue;
    
    double sampleRate = 48000.0;
    
    // Configuration for capture window
    size_t preTriggerSamples = 0;
    size_t postTriggerSamples = 0;
    size_t totalSnapshotSamples = 0;
    
    // Power-of-two ring buffer for lock-free bitwise modulo
    std::vector<float> dryRingBuffer;
    std::vector<float> wetRingBuffer;
    size_t ringBufferMask = 0;
    size_t writeIndex = 0;
    
    // State machine
    enum class State {
        Monitoring,
        AwaitingCapture
    };
    State state = State::Monitoring;
    
    size_t samplesUntilEnd = 0;
    
    // Simple envelope follower for transient detection
    float envelope = 0.0f;
    float attackCoef = 0.0f;
    float releaseCoef = 0.0f;
    float threshold = 0.05f; 
    
    // Internal helper
    void transferSnapshot();
    
    // Find the next power of two
    static size_t nextPowerOfTwo(size_t v) {
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v |= v >> 32;
        v++;
        return v;
    }
};
