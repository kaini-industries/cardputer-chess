#include "esp_now_transport.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_idf_version.h>
#include <cstring>

// =====================================================================
// ESP-NOW Transport Implementation
// =====================================================================

// ISR-safe spinlock for ring buffer access
static portMUX_TYPE s_rxMux = portMUX_INITIALIZER_UNLOCKED;

// ── Singleton ────────────────────────────────────────────────────────

EspNowTransport& EspNowTransport::instance() {
    static EspNowTransport inst;
    return inst;
}

// ── ESP-NOW Callbacks (static, forward to singleton) ─────────────────

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
static void onRecvCb(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    EspNowTransport::instance()._onReceive(info->src_addr, data, len);
}
#else
static void onRecvCb(const uint8_t* mac, const uint8_t* data, int len) {
    EspNowTransport::instance()._onReceive(mac, data, len);
}
#endif

// ── Lifecycle ────────────────────────────────────────────────────────

bool EspNowTransport::init() {
    if (m_state != State::Idle) return true; // Already initialized

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // Fix channel to 1 so both devices match
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

    // Store our MAC address
    WiFi.macAddress(m_ownMac);

    if (esp_now_init() != ESP_OK) {
        return false;
    }

    esp_now_register_recv_cb(onRecvCb);

    m_state = State::Ready;
    m_lastRecvTime = millis();
    return true;
}

void EspNowTransport::shutdown() {
    if (m_state == State::Idle) return;

    removePeer();
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    WiFi.mode(WIFI_OFF);

    m_state = State::Idle;
    m_rxHead = 0;
    m_rxTail = 0;
    for (auto& slot : m_rxBuf) {
        slot.used = false;
    }
}

// ── Peer Management ──────────────────────────────────────────────────

void EspNowTransport::addPeer(const uint8_t mac[6]) {
    // Remove existing peer first
    if (m_hasPeer) {
        removePeer();
    }

    memcpy(m_peerMac, mac, 6);

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, mac, 6);
    peerInfo.channel = 0; // Use current channel
    peerInfo.encrypt = false;

    esp_now_add_peer(&peerInfo);
    m_hasPeer = true;
    m_state = State::Paired;
    m_lastRecvTime = millis();
}

void EspNowTransport::removePeer() {
    if (m_hasPeer) {
        esp_now_del_peer(m_peerMac);
        m_hasPeer = false;
        memset(m_peerMac, 0, 6);
    }
    if (m_state == State::Paired) {
        m_state = State::Ready;
    }
}

// ── Sending ──────────────────────────────────────────────────────────

bool EspNowTransport::broadcast(const uint8_t* data, uint8_t len) {
    if (m_state == State::Idle) return false;

    // Broadcast address
    static const uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    // Ensure broadcast peer exists
    if (!esp_now_is_peer_exist(broadcastMac)) {
        esp_now_peer_info_t bcast = {};
        memcpy(bcast.peer_addr, broadcastMac, 6);
        bcast.channel = 0;
        bcast.encrypt = false;
        esp_now_add_peer(&bcast);
    }

    return esp_now_send(broadcastMac, data, len) == ESP_OK;
}

bool EspNowTransport::send(const uint8_t* data, uint8_t len) {
    if (m_state != State::Paired || !m_hasPeer) return false;
    return esp_now_send(m_peerMac, data, len) == ESP_OK;
}

// ── Receiving ────────────────────────────────────────────────────────

void EspNowTransport::_onReceive(const uint8_t* mac, const uint8_t* data, int len) {
    if (len <= 0 || len > RX_SLOT_SIZE) return;

    portENTER_CRITICAL_ISR(&s_rxMux);

    RxSlot& slot = m_rxBuf[m_rxHead];
    memcpy(slot.data, data, len);
    memcpy(slot.mac, mac, 6);
    slot.len = (uint8_t)len;
    slot.used = true;
    m_rxHead = (m_rxHead + 1) % RX_SLOTS;
    m_lastRecvTime = millis();

    portEXIT_CRITICAL_ISR(&s_rxMux);
}

bool EspNowTransport::hasReceived() const {
    portENTER_CRITICAL(&s_rxMux);
    bool has = m_rxBuf[m_rxTail].used;
    portEXIT_CRITICAL(&s_rxMux);
    return has;
}

uint8_t EspNowTransport::receive(uint8_t* buf, uint8_t maxLen, uint8_t* senderMac) {
    portENTER_CRITICAL(&s_rxMux);

    RxSlot& slot = m_rxBuf[m_rxTail];
    if (!slot.used) {
        portEXIT_CRITICAL(&s_rxMux);
        return 0;
    }

    uint8_t copyLen = (slot.len < maxLen) ? slot.len : maxLen;
    memcpy(buf, slot.data, copyLen);
    if (senderMac) {
        memcpy(senderMac, slot.mac, 6);
    }

    slot.used = false;
    m_rxTail = (m_rxTail + 1) % RX_SLOTS;

    portEXIT_CRITICAL(&s_rxMux);
    return copyLen;
}

uint32_t EspNowTransport::msSinceLastReceive() const {
    return millis() - m_lastRecvTime;
}
