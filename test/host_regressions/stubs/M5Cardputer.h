#pragma once
#include "Arduino.h"
#include <vector>

struct Keyboard_Class {
    struct KeysState {
        bool shift = false, fn = false, opt = false;
        std::vector<char> word;
        std::vector<uint8_t> hid_keys;
    } state;
    size_t lastSize = 0;
    // Mirrors the count-only isChange behavior in pinned M5Cardputer 1.1.1.
    bool isChange() {
        const size_t size = state.word.size() + state.hid_keys.size();
        if (size == lastSize) return false;
        lastSize = size;
        return true;
    }
    bool isPressed() { return !state.word.empty() || !state.hid_keys.empty(); }
    KeysState& keysState() { return state; }
};
struct MockCardputer {
    Keyboard_Class Keyboard;
    struct {
        bool pressed = false;
        bool isPressed() { return pressed; }
    } BtnA;
    void update() {}
};
extern MockCardputer M5Cardputer;
