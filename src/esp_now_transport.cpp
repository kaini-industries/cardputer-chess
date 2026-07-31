#include "esp_now_transport.h"
#include <WiFi.h>
#include <esp_idf_version.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <cstring>

// =====================================================================
// ESP-NOW Transport Implementation
// =====================================================================

static portMUX_TYPE s_rxMux = portMUX_INITIALIZER_UNLOCKED;

namespace {

static constexpr uint8_t ESPNOW_CHANNEL = 1;
static constexpr uint8_t BROADCAST_MAC[6] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

} // namespace

// ── Singleton ────────────────────────────────────────────────────────

EspNowTransport& EspNowTransport::instance() {
    static EspNowTransport inst;
    return inst;
}

// ── ESP-NOW Callbacks (static, forward to singleton) ─────────────────

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
static void onRecvCb(const esp_now_recv_info_t* info,
                     const uint8_t* data,
                     int len) {
    if (info != nullptr) {
        EspNowTransport::instance()._onReceive(info->src_addr, data, len);
    }
}
#else
static void onRecvCb(const uint8_t* mac, const uint8_t* data, int len) {
    EspNowTransport::instance()._onReceive(mac, data, len);
}
#endif

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
static void onSendCb(const wifi_tx_info_t* /*info*/, esp_now_send_status_t status) {
    EspNowTransport::instance()._onSend(
        nullptr, status == ESP_NOW_SEND_SUCCESS);
}
#else
static void onSendCb(const uint8_t* mac, esp_now_send_status_t status) {
    EspNowTransport::instance()._onSend(
        mac, status == ESP_NOW_SEND_SUCCESS);
}
#endif

// ── Diagnostics ──────────────────────────────────────────────────────

void EspNowTransport::setError(Error error, int32_t espError) {
    m_lastError = error;
    m_lastEspError = espError;
}

void EspNowTransport::clearDiagnostics() {
    m_lastError = Error::None;
    m_lastEspError = 0;
    m_lastSendDelivered = false;
    m_sendQueueFailures = 0;
    m_sendDeliveryFailures = 0;
    m_rxDrops = 0;
    m_rxRejected = 0;
}

const char* EspNowTransport::lastErrorName() const {
    switch (m_lastError) {
        case Error::None:                      return "none";
        case Error::InvalidArgument:           return "invalid argument";
        case Error::WifiMode:                  return "WiFi mode";
        case Error::WifiChannel:               return "WiFi channel";
        case Error::EspNowInit:                return "ESP-NOW init";
        case Error::RegisterReceiveCallback:   return "receive callback";
        case Error::RegisterSendCallback:      return "send callback";
        case Error::UnregisterReceiveCallback: return "remove receive callback";
        case Error::UnregisterSendCallback:    return "remove send callback";
        case Error::AddPeer:                   return "add peer";
        case Error::RemovePeer:                return "remove peer";
        case Error::AddBroadcastPeer:          return "add broadcast peer";
        case Error::RemoveBroadcastPeer:       return "remove broadcast peer";
        case Error::SendQueue:                 return "send queue";
        case Error::SendDelivery:              return "radio delivery";
        case Error::EspNowDeinit:              return "ESP-NOW deinit";
        default:                               return "unknown";
    }
}

// ── Lifecycle ────────────────────────────────────────────────────────

bool EspNowTransport::init() {
    // A lobby can be entered repeatedly in one boot. Always tear down the old
    // ESP-NOW instance so its unicast peer and receive queue cannot leak into
    // the next pairing attempt.
    shutdown();
    clearDiagnostics();
    resetReceiveQueue();

    auto failInit = [this](Error error, esp_err_t espError) {
        shutdown();
        setError(error, static_cast<int32_t>(espError));
        return false;
    };

    if (!WiFi.mode(WIFI_STA)) {
        return failInit(Error::WifiMode, ESP_FAIL);
    }
    WiFi.disconnect(false, false);

    esp_err_t result = esp_wifi_set_channel(
        ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (result != ESP_OK) {
        return failInit(Error::WifiChannel, result);
    }

    WiFi.macAddress(m_ownMac);
    if (!isValidUnicastMac(m_ownMac)) {
        return failInit(Error::WifiMode, ESP_FAIL);
    }

    result = esp_now_init();
    if (result != ESP_OK) {
        return failInit(Error::EspNowInit, result);
    }
    m_espNowInitialized = true;

    result = esp_now_register_recv_cb(onRecvCb);
    if (result != ESP_OK) {
        return failInit(Error::RegisterReceiveCallback, result);
    }
    m_recvCallbackRegistered = true;

    result = esp_now_register_send_cb(onSendCb);
    if (result != ESP_OK) {
        return failInit(Error::RegisterSendCallback, result);
    }
    m_sendCallbackRegistered = true;

    m_state = State::Ready;
    m_lastRecvTime = millis();
    return true;
}

void EspNowTransport::shutdown() {
    Error cleanupError = Error::None;
    int32_t cleanupEspError = 0;

    auto rememberCleanupError = [&](Error error, esp_err_t result) {
        if (cleanupError == Error::None) {
            cleanupError = error;
            cleanupEspError = static_cast<int32_t>(result);
        }
    };

    if (m_hasPeer && !removePeer()) {
        rememberCleanupError(Error::RemovePeer,
                             static_cast<esp_err_t>(m_lastEspError));
    }

    if (m_espNowInitialized &&
        (m_hasBroadcastPeer || esp_now_is_peer_exist(BROADCAST_MAC))) {
        const esp_err_t result = esp_now_del_peer(BROADCAST_MAC);
        if (result != ESP_OK && result != ESP_ERR_ESPNOW_NOT_FOUND) {
            rememberCleanupError(Error::RemoveBroadcastPeer, result);
        }
    }
    m_hasBroadcastPeer = false;

    if (m_recvCallbackRegistered) {
        const esp_err_t result = esp_now_unregister_recv_cb();
        if (result != ESP_OK) {
            rememberCleanupError(Error::UnregisterReceiveCallback, result);
        }
    }
    m_recvCallbackRegistered = false;

    if (m_sendCallbackRegistered) {
        const esp_err_t result = esp_now_unregister_send_cb();
        if (result != ESP_OK) {
            rememberCleanupError(Error::UnregisterSendCallback, result);
        }
    }
    m_sendCallbackRegistered = false;

    if (m_espNowInitialized) {
        const esp_err_t result = esp_now_deinit();
        if (result != ESP_OK) {
            rememberCleanupError(Error::EspNowDeinit, result);
        }
    }
    m_espNowInitialized = false;

    if (!WiFi.mode(WIFI_OFF)) {
        rememberCleanupError(Error::WifiMode, ESP_FAIL);
    }
    m_state = State::Idle;
    m_hasPeer = false;
    memset(m_peerMac, 0, sizeof(m_peerMac));
    resetReceiveQueue();

    if (cleanupError != Error::None) {
        setError(cleanupError, cleanupEspError);
    }
}

// ── Peer Management ──────────────────────────────────────────────────

bool EspNowTransport::isValidUnicastMac(const uint8_t mac[6]) {
    if (mac == nullptr || (mac[0] & 0x01) != 0) return false;

    bool anyNonZero = false;
    for (uint8_t i = 0; i < 6; ++i) anyNonZero |= mac[i] != 0;
    return anyNonZero;
}

bool EspNowTransport::addPeer(const uint8_t mac[6]) {
    if (m_state == State::Idle || !m_espNowInitialized ||
        !isValidUnicastMac(mac) || memcmp(mac, m_ownMac, 6) == 0) {
        setError(Error::InvalidArgument, ESP_ERR_INVALID_ARG);
        return false;
    }

    if (m_hasPeer && memcmp(mac, m_peerMac, 6) == 0 &&
        esp_now_is_peer_exist(mac)) {
        return true;
    }

    if (m_hasPeer && !removePeer()) return false;

    // Recover from any ESP-NOW peer entry which outlived our local flag.
    if (esp_now_is_peer_exist(mac)) {
        const esp_err_t removeResult = esp_now_del_peer(mac);
        if (removeResult != ESP_OK &&
            removeResult != ESP_ERR_ESPNOW_NOT_FOUND) {
            setError(Error::RemovePeer, removeResult);
            return false;
        }
    }

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, mac, 6);
    peerInfo.channel = ESPNOW_CHANNEL;
    peerInfo.ifidx = WIFI_IF_STA;
    peerInfo.encrypt = false;

    const esp_err_t result = esp_now_add_peer(&peerInfo);
    if (result != ESP_OK) {
        setError(Error::AddPeer, result);
        return false;
    }

    memcpy(m_peerMac, mac, 6);
    m_hasPeer = true;
    m_state = State::Paired;
    m_lastRecvTime = millis();
    return true;
}

bool EspNowTransport::removePeer() {
    if (!m_hasPeer) {
        if (m_state == State::Paired || m_state == State::Disconnected) {
            m_state = m_espNowInitialized ? State::Ready : State::Idle;
        }
        return true;
    }

    bool success = true;
    if (m_espNowInitialized) {
        const esp_err_t result = esp_now_del_peer(m_peerMac);
        if (result != ESP_OK && result != ESP_ERR_ESPNOW_NOT_FOUND) {
            setError(Error::RemovePeer, result);
            success = false;
        }
    }

    m_hasPeer = false;
    memset(m_peerMac, 0, sizeof(m_peerMac));
    m_state = m_espNowInitialized ? State::Ready : State::Idle;
    return success;
}

// ── Sending ──────────────────────────────────────────────────────────

bool EspNowTransport::broadcast(const uint8_t* data, uint8_t len) {
    if (m_state == State::Idle || !m_espNowInitialized || data == nullptr ||
        len == 0 || len > RX_SLOT_SIZE) {
        setError(Error::InvalidArgument, ESP_ERR_INVALID_ARG);
        return false;
    }

    if (!esp_now_is_peer_exist(BROADCAST_MAC)) {
        esp_now_peer_info_t broadcastPeer = {};
        memcpy(broadcastPeer.peer_addr, BROADCAST_MAC, 6);
        broadcastPeer.channel = ESPNOW_CHANNEL;
        broadcastPeer.ifidx = WIFI_IF_STA;
        broadcastPeer.encrypt = false;

        const esp_err_t addResult = esp_now_add_peer(&broadcastPeer);
        if (addResult != ESP_OK && addResult != ESP_ERR_ESPNOW_EXIST) {
            setError(Error::AddBroadcastPeer, addResult);
            return false;
        }
    }
    m_hasBroadcastPeer = true;

    m_lastSendDelivered = false;
    const esp_err_t result = esp_now_send(BROADCAST_MAC, data, len);
    if (result != ESP_OK) {
        ++m_sendQueueFailures;
        setError(Error::SendQueue, result);
        return false;
    }
    return true;
}

bool EspNowTransport::send(const uint8_t* data, uint8_t len) {
    if (m_state != State::Paired || !m_hasPeer || data == nullptr ||
        len == 0 || len > RX_SLOT_SIZE) {
        setError(Error::InvalidArgument, ESP_ERR_INVALID_ARG);
        return false;
    }

    m_lastSendDelivered = false;
    const esp_err_t result = esp_now_send(m_peerMac, data, len);
    if (result != ESP_OK) {
        ++m_sendQueueFailures;
        setError(Error::SendQueue, result);
        return false;
    }
    return true;
}

void EspNowTransport::_onSend(const uint8_t* /*mac*/, bool delivered) {
    m_lastSendDelivered = delivered;
    if (!delivered) {
        ++m_sendDeliveryFailures;
        setError(Error::SendDelivery, 0);
    }
}

// ── MAC Filtering ────────────────────────────────────────────────────

bool EspNowTransport::isPeerMac(const uint8_t mac[6]) const {
    return mac != nullptr && m_hasPeer && memcmp(mac, m_peerMac, 6) == 0;
}

// ── Receiving ────────────────────────────────────────────────────────

void EspNowTransport::resetReceiveQueue() {
    portENTER_CRITICAL(&s_rxMux);
    m_rxHead = 0;
    m_rxTail = 0;
    m_rxCount = 0;
    for (auto& slot : m_rxBuf) {
        slot.len = 0;
        slot.used = false;
    }
    portEXIT_CRITICAL(&s_rxMux);
}

void EspNowTransport::_onReceive(const uint8_t* mac,
                                 const uint8_t* data,
                                 int len) {
    if (!isValidUnicastMac(mac) || data == nullptr ||
        len <= 0 || len > RX_SLOT_SIZE) {
        portENTER_CRITICAL_ISR(&s_rxMux);
        ++m_rxRejected;
        portEXIT_CRITICAL_ISR(&s_rxMux);
        return;
    }

    // Once paired, only the exact selected MAC can affect receive freshness or
    // enter the queue. This check is repeated by protocol consumers as well.
    if (m_hasPeer && memcmp(mac, m_peerMac, 6) != 0) {
        portENTER_CRITICAL_ISR(&s_rxMux);
        ++m_rxRejected;
        portEXIT_CRITICAL_ISR(&s_rxMux);
        return;
    }

    portENTER_CRITICAL_ISR(&s_rxMux);

    if (m_rxCount >= RX_SLOTS) {
        // Drop the newest packet rather than overwriting m_rxTail. The oldest
        // unread packets therefore remain in strict FIFO order.
        ++m_rxDrops;
        portEXIT_CRITICAL_ISR(&s_rxMux);
        return;
    }

    RxSlot& slot = m_rxBuf[m_rxHead];
    memcpy(slot.data, data, len);
    memcpy(slot.mac, mac, 6);
    slot.len = static_cast<uint8_t>(len);
    slot.used = true;
    m_rxHead = (m_rxHead + 1) % RX_SLOTS;
    ++m_rxCount;
    m_lastRecvTime = millis();

    portEXIT_CRITICAL_ISR(&s_rxMux);
}

bool EspNowTransport::hasReceived() const {
    portENTER_CRITICAL(&s_rxMux);
    const bool hasPacket = m_rxCount > 0;
    portEXIT_CRITICAL(&s_rxMux);
    return hasPacket;
}

uint8_t EspNowTransport::receive(uint8_t* buf,
                                 uint8_t maxLen,
                                 uint8_t* senderMac) {
    if (buf == nullptr || maxLen == 0) {
        setError(Error::InvalidArgument, ESP_ERR_INVALID_ARG);
        return 0;
    }

    portENTER_CRITICAL(&s_rxMux);

    if (m_rxCount == 0) {
        portEXIT_CRITICAL(&s_rxMux);
        return 0;
    }

    RxSlot& slot = m_rxBuf[m_rxTail];
    const uint8_t copyLen = (slot.len < maxLen) ? slot.len : maxLen;
    memcpy(buf, slot.data, copyLen);
    if (senderMac != nullptr) memcpy(senderMac, slot.mac, 6);

    slot.len = 0;
    slot.used = false;
    m_rxTail = (m_rxTail + 1) % RX_SLOTS;
    --m_rxCount;

    portEXIT_CRITICAL(&s_rxMux);
    return copyLen;
}

uint32_t EspNowTransport::msSinceLastReceive() const {
    return millis() - m_lastRecvTime;
}
