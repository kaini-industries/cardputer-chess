#ifndef ESP_NOW_TRANSPORT_H
#define ESP_NOW_TRANSPORT_H

#include <cstdint>

// =====================================================================
// EspNowTransport: Low-level ESP-NOW wrapper singleton.
// Handles init, peer management, send/receive with ISR-safe ring buffer.
// Knows nothing about chess or message semantics.
// =====================================================================

class EspNowTransport {
public:
    static EspNowTransport& instance();

    enum class State : uint8_t {
        Idle,          // Not initialized
        Ready,         // Initialized, no peer
        Paired,        // Peer added, can send/receive
        Disconnected   // Was paired, lost contact
    };

    // Lifecycle
    bool init();       // WiFi STA mode + esp_now_init
    void shutdown();   // esp_now_deinit + WiFi off

    // Peer management
    void addPeer(const uint8_t mac[6]);
    void removePeer();

    // Sending
    bool broadcast(const uint8_t* data, uint8_t len);
    bool send(const uint8_t* data, uint8_t len);  // To paired peer

    // Receiving (call from main loop, not ISR)
    bool hasReceived() const;
    // Returns bytes copied into buf (0 if nothing). Fills senderMac if non-null.
    uint8_t receive(uint8_t* buf, uint8_t maxLen, uint8_t* senderMac = nullptr);

    // State
    State state() const { return m_state; }
    void setState(State s) { m_state = s; }
    uint32_t msSinceLastReceive() const;
    const uint8_t* ownMac() const { return m_ownMac; }
    const uint8_t* peerMac() const { return m_peerMac; }

    // Called by ISR callback — do not call directly
    void _onReceive(const uint8_t* mac, const uint8_t* data, int len);

private:
    EspNowTransport() = default;

    // Ring buffer for received packets (ISR-safe)
    static constexpr uint8_t RX_SLOTS = 4;
    static constexpr uint8_t RX_SLOT_SIZE = 32;

    struct RxSlot {
        uint8_t data[RX_SLOT_SIZE];
        uint8_t mac[6];
        uint8_t len = 0;
        bool    used = false;
    };

    RxSlot   m_rxBuf[RX_SLOTS];
    uint8_t  m_rxHead = 0;  // Next slot to write (ISR)
    uint8_t  m_rxTail = 0;  // Next slot to read (main loop)

    State    m_state = State::Idle;
    uint8_t  m_ownMac[6] = {};
    uint8_t  m_peerMac[6] = {};
    bool     m_hasPeer = false;
    uint32_t m_lastRecvTime = 0;
};

#endif // ESP_NOW_TRANSPORT_H
