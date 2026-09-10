// Hardware boundaries only. Engine, scene routing, clock, SAN, storage codecs,
// and profile transactions are the production implementations.
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include <cstring>
#include "cardgfx.h"
#include "M5Cardputer.h"
#include "nvs_blob_store.h"
#include "esp_now_transport.h"
#include "puzzle_storage.h"

uint32_t auditNow = 1000;
MockCardputer M5Cardputer;
std::vector<std::vector<uint8_t>> auditSent, auditIncoming;

namespace CardGFX { namespace HAL {
bool init(uint8_t, uint8_t) { return true; }
void pushRegion(int16_t, int16_t, uint16_t, uint16_t, const uint16_t*) {}
void fillRect(int16_t, int16_t, uint16_t, uint16_t, uint16_t) {}
void clear(uint16_t) {}
void setBrightness(uint8_t) {}
void* driverHandle() { return nullptr; }
}}

namespace NvsBlobStore {
std::map<std::string, std::vector<uint8_t>> db;
Blob::~Blob() { delete[] data; }
ReadStatus read(const char* ns, const char* key, size_t max, Blob& blob) {
    auto entry = db.find(std::string(ns) + "/" + key);
    if (entry == db.end()) return ReadStatus::Missing;
    if (entry->second.size() > max) return ReadStatus::TooLarge;
    delete[] blob.data;
    blob.size = entry->second.size();
    blob.data = new uint8_t[blob.size];
    std::memcpy(blob.data, entry->second.data(), blob.size);
    return ReadStatus::Ok;
}
bool write(const char* ns, const char* key, const uint8_t* data, size_t size) {
    db[std::string(ns) + "/" + key] = std::vector<uint8_t>(data, data + size);
    return true;
}
bool erase(const char* ns, const char* key) {
    db.erase(std::string(ns) + "/" + key);
    return true;
}
}

namespace PuzzleStorage {
void loadProgress(PuzzleProgress& progress) { progress = PuzzleProgress{}; }
void saveProgress(const PuzzleProgress&) {}
bool isPuzzleCompleted(const PuzzleProgress& progress, uint8_t index) {
    return progress.completed[index / 8] & (1 << (index % 8));
}
void markPuzzleCompleted(PuzzleProgress& progress, uint8_t index) {
    progress.completed[index / 8] |= 1 << (index % 8);
}
uint16_t completedCount(const PuzzleProgress& progress, uint16_t count) {
    uint16_t completed = 0;
    for (uint16_t i = 0; i < count; ++i) completed += isPuzzleCompleted(progress, i);
    return completed;
}
}

EspNowTransport& EspNowTransport::instance() {
    static EspNowTransport transport;
    return transport;
}
bool EspNowTransport::init() { m_state = State::Ready; return true; }
void EspNowTransport::shutdown() { m_state = State::Idle; }
bool EspNowTransport::addPeer(const uint8_t mac[6]) {
    std::memcpy(m_peerMac, mac, 6);
    m_hasPeer = true;
    m_state = State::Paired;
    return true;
}
bool EspNowTransport::send(const uint8_t* data, uint8_t size) {
    auditSent.emplace_back(data, data + size);
    return true;
}
bool EspNowTransport::broadcast(const uint8_t* data, uint8_t size) {
    return send(data, size);
}
bool EspNowTransport::hasReceived() const { return !auditIncoming.empty(); }
uint8_t EspNowTransport::receive(uint8_t* buffer, uint8_t max, uint8_t* mac) {
    if (auditIncoming.empty()) return 0;
    auto packet = auditIncoming.front();
    auditIncoming.erase(auditIncoming.begin());
    const auto size = std::min<size_t>(max, packet.size());
    std::memcpy(buffer, packet.data(), size);
    if (mac) std::memcpy(mac, m_peerMac, 6);
    return size;
}
bool EspNowTransport::isPeerMac(const uint8_t* mac) const {
    return !std::memcmp(mac, m_peerMac, 6);
}
const char* EspNowTransport::lastErrorName() const { return "none"; }
