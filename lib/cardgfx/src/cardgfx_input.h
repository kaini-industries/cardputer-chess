#ifndef CARDGFX_INPUT_H
#define CARDGFX_INPUT_H

#include "cardgfx_config.h"
#include <cstdint>
#include <functional>

namespace CardGFX {

// ── Key Codes ────────────────────────────────────────────────────
// These map to the Cardputer's keyboard matrix.
// Printable ASCII keys use their ASCII value.

namespace Key {
    constexpr uint8_t NONE       = 0;
    constexpr uint8_t ENTER      = '\n';
    constexpr uint8_t BACKSPACE  = '\b';
    constexpr uint8_t TAB        = '\t';
    constexpr uint8_t ESCAPE     = 27;
    constexpr uint8_t SPACE      = ' ';

    // Special keys (non-ASCII, using 0x80+ range)
    constexpr uint8_t LEFT       = 0x80;
    constexpr uint8_t RIGHT      = 0x81;
    constexpr uint8_t UP         = 0x82;
    constexpr uint8_t DOWN       = 0x83;
    constexpr uint8_t FN         = 0x84;
    constexpr uint8_t OPT        = 0x85;
    constexpr uint8_t DEL        = 0x86;
    constexpr uint8_t HOME       = 0x87;
}

// ── Modifier Flags ───────────────────────────────────────────────

namespace Mod {
    constexpr uint8_t NONE  = 0x00;
    constexpr uint8_t SHIFT = 0x01;
    constexpr uint8_t FN    = 0x02;
    constexpr uint8_t OPT   = 0x04;
}

// ── Event Types ──────────────────────────────────────────────────

enum class EventType : uint8_t {
    None,
    KeyDown,       // Key pressed
    KeyUp,         // Key released
    KeyRepeat,     // Key held (auto-repeat)
};

// ── Input Event ──────────────────────────────────────────────────

struct InputEvent {
    EventType type      = EventType::None;
    uint8_t   key       = Key::NONE;
    char      character = 0;      // Printable char, or 0
    uint8_t   modifiers = Mod::NONE;

    bool isNone() const   { return type == EventType::None; }
    bool isDown() const   { return type == EventType::KeyDown; }
    bool isUp() const     { return type == EventType::KeyUp; }
    bool isRepeat() const { return type == EventType::KeyRepeat; }
    bool isPrintable() const { return character >= 32 && character < 127; }

    bool hasShift() const { return modifiers & Mod::SHIFT; }
    bool hasFn() const    { return modifiers & Mod::FN; }
    bool hasOpt() const   { return modifiers & Mod::OPT; }
};

// ── Action Binding ───────────────────────────────────────────────

using ActionCallback = std::function<bool()>;  // Return true if consumed

struct KeyBinding {
    uint8_t  key       = Key::NONE;
    uint8_t  modifiers = Mod::NONE;
    uint16_t actionId  = 0;
    ActionCallback callback = nullptr;
    bool     active    = false;
};

// ── Input Manager ────────────────────────────────────────────────

/**
 * Scans the Cardputer keyboard, generates InputEvent structs,
 * and routes them through the focus chain and key bindings.
 */
class InputManager {
public:
    InputManager();

    /**
     * Initialize the keyboard hardware.
     */
    bool init();

    /**
     * Poll the keyboard. Call once per frame.
     * Generates events and stores them in the internal queue.
     */
    void poll();

    /**
     * Get the next pending event (FIFO).
     * Returns an event with type == None when the queue is empty.
     */
    InputEvent nextEvent();

    /**
     * Peek at the next event without removing it.
     */
    InputEvent peekEvent() const;

    /**
     * Check if any events are pending.
     */
    bool hasEvents() const { return m_eventHead != m_eventTail; }

    /**
     * Clear all pending events.
     */
    void clearEvents();

    // ── Key State ────────────────────────────────────────────────

    /** Is a key currently held down? */
    bool isKeyDown(uint8_t key) const;

    /** Was a key just pressed this frame? */
    bool isKeyPressed(uint8_t key) const;

    /** Was a key just released this frame? */
    bool isKeyReleased(uint8_t key) const;

    // ── Bindings ─────────────────────────────────────────────────

    /**
     * Bind a key (+ optional modifiers) to an action callback.
     * Bindings are checked before the focus chain.
     *
     * @param key       Key code.
     * @param modifiers Required modifier flags.
     * @param actionId  Identifier for debugging.
     * @param callback  Function to call. Return true to consume the event.
     * @return true if the binding was added.
     */
    bool bind(uint8_t key, uint8_t modifiers, uint16_t actionId,
              ActionCallback callback);

    /**
     * Remove a binding by action ID.
     */
    void unbind(uint16_t actionId);

    /**
     * Remove all bindings.
     */
    void clearBindings();

    /**
     * Process an event against the current bindings.
     * @return true if the event was consumed by a binding.
     */
    bool processBindings(const InputEvent& event);

    /**
     * Inject a synthetic event into the queue (for testing).
     */
    void pushEvent(const InputEvent& event);

private:
    // Event ring buffer
    static constexpr uint8_t EVENT_QUEUE_SIZE = 32;
    InputEvent m_eventQueue[EVENT_QUEUE_SIZE] = {};
    uint8_t    m_eventHead = 0;
    uint8_t    m_eventTail = 0;

    // Key state tracking
    static constexpr uint16_t KEY_COUNT = 256;  // Full byte range
    uint8_t m_keyCurrent[KEY_COUNT / 8] = {};  // Bitfield: currently held
    uint8_t m_keyPrevious[KEY_COUNT / 8] = {}; // Bitfield: previous frame

    void setKeyState(uint8_t key, bool down);
    bool getKeyState(const uint8_t* bitfield, uint8_t key) const;

    // Key repeat tracking
    uint8_t  m_repeatKey  = Key::NONE;
    uint32_t m_repeatTime = 0;
    bool     m_repeatActive = false;

    // Bindings
    KeyBinding m_bindings[MAX_KEY_BINDINGS] = {};
};

} // namespace CardGFX

#endif // CARDGFX_INPUT_H
