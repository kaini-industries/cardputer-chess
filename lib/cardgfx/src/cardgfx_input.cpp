#include "cardgfx_input.h"
#include <M5Cardputer.h>
#undef SHIFT  // Keyboard_def.h defines SHIFT as 0x80, conflicts with Mod::SHIFT
#include <cstring>

namespace CardGFX {

InputManager::InputManager() {
    memset(m_keyCurrent, 0, sizeof(m_keyCurrent));
    memset(m_keyPrevious, 0, sizeof(m_keyPrevious));
}

bool InputManager::init() {
    // Keyboard is initialized by M5Cardputer.begin()
    return true;
}

void InputManager::poll() {
    // Save previous state
    memcpy(m_keyPrevious, m_keyCurrent, sizeof(m_keyCurrent));

    // Update the M5 stack
    M5Cardputer.update();

    // Track BtnA (G0 side button) state for ESC
    bool btnAHeld = M5Cardputer.BtnA.isHolding() || M5Cardputer.BtnA.wasPressed();
    bool btnAPressed = M5Cardputer.BtnA.wasPressed();
    bool btnAReleased = M5Cardputer.BtnA.wasReleased();

    // No keyboard change — only process BtnA events
    if (!M5Cardputer.Keyboard.isChange()) {
        // Maintain BtnA ESC state even when keyboard is idle
        if (btnAHeld) setKeyState(Key::ESCAPE, true);

        if (btnAPressed) {
            InputEvent evt;
            evt.type = EventType::KeyDown;
            evt.key = Key::ESCAPE;
            evt.character = 0;
            evt.modifiers = Mod::NONE;
            pushEvent(evt);
            m_repeatKey = Key::ESCAPE;
            m_repeatTime = millis();
            m_repeatActive = false;
        } else if (btnAReleased) {
            setKeyState(Key::ESCAPE, false);
            InputEvent evt;
            evt.type = EventType::KeyUp;
            evt.key = Key::ESCAPE;
            evt.character = 0;
            evt.modifiers = Mod::NONE;
            pushEvent(evt);
            if (m_repeatKey == Key::ESCAPE) {
                m_repeatKey = Key::NONE;
                m_repeatActive = false;
            }
        }
        return;
    }

    // Clear current state only when we have new data
    memset(m_keyCurrent, 0, sizeof(m_keyCurrent));

    if (M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState state = M5Cardputer.Keyboard.keysState();

        // Track modifier state
        uint8_t modifiers = Mod::NONE;
        if (state.shift) modifiers |= Mod::SHIFT;
        if (state.fn)    modifiers |= Mod::FN;
        if (state.opt)   modifiers |= Mod::OPT;

        // Process each pressed key
        for (auto key : state.word) {
            if (key == 0) continue;

            uint8_t keyCode = (uint8_t)key;
            char    ch      = key;

            // FN + key → navigation (Cardputer has no arrow/ESC keys)
            if (modifiers & Mod::FN) {
                uint8_t nav = Key::NONE;
                switch (keyCode) {
                    case '`': case '~': nav = Key::ESCAPE; break;
                    case ';': case ':': nav = Key::UP;     break;
                    case '.': case '>': nav = Key::DOWN;   break;
                    case ',': case '<': nav = Key::LEFT;   break;
                    case '/': case '?': nav = Key::RIGHT;  break;
                }
                if (nav != Key::NONE) {
                    keyCode = nav;
                    ch = 0;
                }
            }

            setKeyState(keyCode, true);

            // Generate key down event if this is a new press
            if (!getKeyState(m_keyPrevious, keyCode)) {
                InputEvent evt;
                evt.type = EventType::KeyDown;
                evt.key = keyCode;
                evt.character = (ch >= 32 && ch < 127) ? ch : 0;
                evt.modifiers = modifiers;
                pushEvent(evt);

                // Start repeat tracking
                m_repeatKey = keyCode;
                m_repeatTime = millis();
                m_repeatActive = false;
            }
        }

        // Handle special/control keys from the hid_keys state
        for (auto hid : state.hid_keys) {
            uint8_t mapped = Key::NONE;
            switch (hid) {
                case 0x28: mapped = Key::ENTER;     break; // HID Enter
                case 0x29: mapped = Key::ESCAPE;    break; // HID Escape
                case 0x2A: mapped = Key::BACKSPACE; break; // HID Backspace
                case 0x2B: mapped = Key::TAB;       break; // HID Tab
                case 0x4F: mapped = Key::RIGHT;     break; // HID Right
                case 0x50: mapped = Key::LEFT;      break; // HID Left
                case 0x51: mapped = Key::DOWN;      break; // HID Down
                case 0x52: mapped = Key::UP;        break; // HID Up
                case 0x4C: mapped = Key::DEL;       break; // HID Delete
                default: break;
            }
            if (mapped != Key::NONE) {
                setKeyState(mapped, true);
                if (!getKeyState(m_keyPrevious, mapped)) {
                    InputEvent evt;
                    evt.type = EventType::KeyDown;
                    evt.key = mapped;
                    evt.character = 0;
                    evt.modifiers = modifiers;
                    pushEvent(evt);

                    m_repeatKey = mapped;
                    m_repeatTime = millis();
                    m_repeatActive = false;
                }
            }
        }
    }

    // Preserve BtnA ESC state after keyboard memset
    if (btnAHeld) {
        setKeyState(Key::ESCAPE, true);
        if (btnAPressed) {
            InputEvent evt;
            evt.type = EventType::KeyDown;
            evt.key = Key::ESCAPE;
            evt.character = 0;
            evt.modifiers = Mod::NONE;
            pushEvent(evt);
            m_repeatKey = Key::ESCAPE;
            m_repeatTime = millis();
            m_repeatActive = false;
        }
    } else if (btnAReleased) {
        setKeyState(Key::ESCAPE, false);
    }

    // Generate KeyUp events for keys that were held last frame but not now
    for (uint16_t i = 0; i < KEY_COUNT; i++) {
        uint8_t k = (uint8_t)i;
        if (getKeyState(m_keyPrevious, k) && !getKeyState(m_keyCurrent, k)) {
            InputEvent evt;
            evt.type = EventType::KeyUp;
            evt.key = k;
            evt.character = (k >= 32 && k < 127) ? (char)k : 0;
            evt.modifiers = Mod::NONE;
            pushEvent(evt);

            if (m_repeatKey == k) {
                m_repeatKey = Key::NONE;
                m_repeatActive = false;
            }
        }
    }

    // Handle key repeat
    if (m_repeatKey != Key::NONE && isKeyDown(m_repeatKey)) {
        uint32_t now = millis();
        uint32_t threshold = m_repeatActive
                             ? KEY_REPEAT_RATE_MS
                             : KEY_REPEAT_DELAY_MS;
        if (now - m_repeatTime >= threshold) {
            InputEvent evt;
            evt.type = EventType::KeyRepeat;
            evt.key = m_repeatKey;
            evt.character = (m_repeatKey >= 32 && m_repeatKey < 127)
                            ? (char)m_repeatKey : 0;
            evt.modifiers = Mod::NONE;
            pushEvent(evt);

            m_repeatTime = now;
            m_repeatActive = true;
        }
    }
}

InputEvent InputManager::nextEvent() {
    if (m_eventHead == m_eventTail) {
        return InputEvent{};
    }
    InputEvent evt = m_eventQueue[m_eventHead];
    m_eventHead = (m_eventHead + 1) % EVENT_QUEUE_SIZE;
    return evt;
}

InputEvent InputManager::peekEvent() const {
    if (m_eventHead == m_eventTail) {
        return InputEvent{};
    }
    return m_eventQueue[m_eventHead];
}

void InputManager::clearEvents() {
    m_eventHead = 0;
    m_eventTail = 0;
}

bool InputManager::isKeyDown(uint8_t key) const {
    return getKeyState(m_keyCurrent, key);
}

bool InputManager::isKeyPressed(uint8_t key) const {
    return getKeyState(m_keyCurrent, key) && !getKeyState(m_keyPrevious, key);
}

bool InputManager::isKeyReleased(uint8_t key) const {
    return !getKeyState(m_keyCurrent, key) && getKeyState(m_keyPrevious, key);
}

bool InputManager::bind(uint8_t key, uint8_t modifiers, uint16_t actionId,
                        ActionCallback callback) {
    for (uint8_t i = 0; i < MAX_KEY_BINDINGS; i++) {
        if (!m_bindings[i].active) {
            m_bindings[i].key = key;
            m_bindings[i].modifiers = modifiers;
            m_bindings[i].actionId = actionId;
            m_bindings[i].callback = callback;
            m_bindings[i].active = true;
            return true;
        }
    }
    return false;  // No free binding slots
}

void InputManager::unbind(uint16_t actionId) {
    for (uint8_t i = 0; i < MAX_KEY_BINDINGS; i++) {
        if (m_bindings[i].active && m_bindings[i].actionId == actionId) {
            m_bindings[i].active = false;
            m_bindings[i].callback = nullptr;
        }
    }
}

void InputManager::clearBindings() {
    for (uint8_t i = 0; i < MAX_KEY_BINDINGS; i++) {
        m_bindings[i].active = false;
        m_bindings[i].callback = nullptr;
    }
}

bool InputManager::processBindings(const InputEvent& event) {
    if (event.type != EventType::KeyDown && event.type != EventType::KeyRepeat)
        return false;

    for (uint8_t i = 0; i < MAX_KEY_BINDINGS; i++) {
        if (m_bindings[i].active &&
            m_bindings[i].key == event.key &&
            (m_bindings[i].modifiers == Mod::NONE ||
             m_bindings[i].modifiers == event.modifiers)) {
            if (m_bindings[i].callback && m_bindings[i].callback()) {
                return true;  // Consumed
            }
        }
    }
    return false;
}

// ── Private ──────────────────────────────────────────────────────

void InputManager::pushEvent(const InputEvent& event) {
    uint8_t next = (m_eventTail + 1) % EVENT_QUEUE_SIZE;
    if (next == m_eventHead) return;  // Queue full, drop event
    m_eventQueue[m_eventTail] = event;
    m_eventTail = next;
}

void InputManager::setKeyState(uint8_t key, bool down) {
    if (down) {
        m_keyCurrent[key / 8] |= (1 << (key % 8));
    } else {
        m_keyCurrent[key / 8] &= ~(1 << (key % 8));
    }
}

bool InputManager::getKeyState(const uint8_t* bitfield, uint8_t key) const {
    return bitfield[key / 8] & (1 << (key % 8));
}

} // namespace CardGFX
