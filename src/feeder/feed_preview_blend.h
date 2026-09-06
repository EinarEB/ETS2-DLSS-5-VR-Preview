// Copyright (c) 2026 ETS2 VR preview contributors. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <mutex>

namespace ets2_preview {
struct BlendSelection { float value = 1; uint64_t serial = 0, generation = 0; };
struct BlendStatus {
    BlendSelection selected, delivered;
    uint64_t deliveredFrame = 0, deliveredTick = 0;
    bool temporary = false;
};

// The visible comparison is independent of feature lifetime and saved quality.
// Each frame takes one selection snapshot for both eyes, metadata and delivery.
class BlendControl {
    std::mutex mutex;
    float configured = 1, overrideValue = -1;
    uint64_t serial = 1, generation = 1;
    bool armed = false;
    unsigned boundKey = 0;
    uintptr_t boundRuntime = 0;
    BlendSelection delivered;
    uint64_t deliveredFrame = 0, deliveredTick = 0;
    float Value() const { return overrideValue < 0 ? configured : overrideValue; }
public:
    void Configure(float value) {
        std::lock_guard<std::mutex> lock(mutex);
        if (value >= 0 && value <= 1 && value != configured) {
            configured = value;
            if (overrideValue < 0) ++serial;
        }
    }
    void Manual(float value) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!(value >= 0 && value <= 1)) return;
        configured = value; overrideValue = -1; ++serial;
    }
    bool Key(bool eligible, unsigned key, bool down, uintptr_t runtime) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!eligible || !key || key != boundKey || runtime != boundRuntime) {
            armed = false; boundKey = key; boundRuntime = runtime;
            if (!eligible || !key) return false;
        }
        if (!down) { armed = true; return false; }
        if (!armed) return false;
        armed = false; overrideValue = Value() > 0 ? 0.0f : 1.0f; ++serial;
        return true;
    }
    void Disarm() {
        std::lock_guard<std::mutex> lock(mutex);
        armed = false; boundKey = 0; boundRuntime = 0;
    }
    void ResetRuntime() {
        std::lock_guard<std::mutex> lock(mutex);
        armed = false; boundKey = 0; boundRuntime = 0; ++generation;
        delivered = {}; deliveredFrame = 0; deliveredTick = 0;
    }
    BlendSelection Latch() {
        std::lock_guard<std::mutex> lock(mutex);
        return {Value(), serial, generation};
    }
    void Delivered(BlendSelection frame, uint64_t number, uint64_t tick) {
        std::lock_guard<std::mutex> lock(mutex);
        if (frame.generation != generation) return;
        delivered = frame; deliveredFrame = number; deliveredTick = tick;
    }
    BlendStatus Status() {
        std::lock_guard<std::mutex> lock(mutex);
        return {{Value(), serial, generation}, delivered, deliveredFrame, deliveredTick, overrideValue >= 0};
    }
};
}
