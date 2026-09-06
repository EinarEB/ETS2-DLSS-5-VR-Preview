#pragma once

// Pure key-edge state; the runtime adapter supplies only Ctrl, Alt and N.
// Releasing N while eligible arms the next N press. Focus loss disarms it, so
// returning to the game with a held key cannot accidentally toggle comparison.
struct TestCompareInput {
    bool enabled = false, windowless = false, foreground = false;
    bool ctrl = false, alt = false, n = false;
};
enum class TestCompareChange { none, original_on, original_off };
struct TestCompareState {
    bool original = false, armed = false;
    TestCompareChange Update(const TestCompareInput &input) {
        if (!input.enabled) {
            const bool was_original = original;
            original = armed = false;
            return was_original ? TestCompareChange::original_off : TestCompareChange::none;
        }
        if (!input.windowless || !input.foreground) {
            armed = false;
            return TestCompareChange::none;
        }
        if (!input.n) {
            armed = true;
            return TestCompareChange::none;
        }
        const bool trigger = armed && input.ctrl && input.alt;
        armed = false;
        if (!trigger) return TestCompareChange::none;
        original = !original;
        return original ? TestCompareChange::original_on : TestCompareChange::original_off;
    }
};
