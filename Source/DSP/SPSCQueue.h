#pragma once
#include <atomic>
#include <vector>
#include <cstddef>
#include <span>

// オーディオスレッドとUIスレッド間のゼロアロケーションデータ転送用構造体
struct WaveformSnapshot {
    std::vector<float> dryWaveform;
    std::vector<float> wetWaveform;
    double sampleRate = 48000.0;
    
    void resize(size_t size) {
        dryWaveform.resize(size, 0.0f);
        wetWaveform.resize(size, 0.0f);
    }
};

// alignas(64) と Acquire-Release オーダリングを用いたSPSCロックフリーキュー
// インプレース（スロット参照）操作によりコピーとメモリアロケーションを排除
template <typename T>
class SPSCSlotQueue {
public:
    explicit SPSCSlotQueue(size_t capacity) : buffer(capacity + 1), capacity_(capacity + 1) {}

    // Producer (Audio Thread) side
    T* getWriteSlot() {
        auto currentWrite = writeIndex.load(std::memory_order_relaxed);
        auto nextWrite = (currentWrite + 1) % capacity_;
        if (nextWrite == readIndexCached) {
            readIndexCached = readIndex.load(std::memory_order_acquire);
            if (nextWrite == readIndexCached) {
                return nullptr; // Full
            }
        }
        return &buffer[currentWrite];
    }

    void commitWrite() {
        auto currentWrite = writeIndex.load(std::memory_order_relaxed);
        writeIndex.store((currentWrite + 1) % capacity_, std::memory_order_release);
    }

    // Consumer (UI Thread) side
    T* getReadSlot() {
        auto currentRead = readIndex.load(std::memory_order_relaxed);
        if (currentRead == writeIndexCached) {
            writeIndexCached = writeIndex.load(std::memory_order_acquire);
            if (currentRead == writeIndexCached) {
                return nullptr; // Empty
            }
        }
        return &buffer[currentRead];
    }

    void commitRead() {
        auto currentRead = readIndex.load(std::memory_order_relaxed);
        readIndex.store((currentRead + 1) % capacity_, std::memory_order_release);
    }

    // Capacity (excluding the extra slot)
    size_t capacity() const { return capacity_ - 1; }
    
    // Resize the buffer explicitly before starting the audio thread
    void reset(size_t capacity) {
        capacity_ = capacity + 1;
        buffer.resize(capacity_);
        writeIndex.store(0, std::memory_order_relaxed);
        readIndex.store(0, std::memory_order_relaxed);
        readIndexCached = 0;
        writeIndexCached = 0;
    }
    
    // Access elements to pre-allocate their internal vectors
    T& getElement(size_t index) {
        return buffer[index];
    }

private:
    std::vector<T> buffer;
    size_t capacity_;

    alignas(64) std::atomic<size_t> writeIndex{0};
    alignas(64) size_t readIndexCached{0};
    
    alignas(64) std::atomic<size_t> readIndex{0};
    alignas(64) size_t writeIndexCached{0};
};
