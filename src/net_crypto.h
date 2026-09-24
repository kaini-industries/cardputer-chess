#ifndef NET_CRYPTO_H
#define NET_CRYPTO_H

#include <cstddef>
#include <cstdint>

// Portable session cryptography. X25519 (RFC 7748) and HMAC-SHA256 live in
// this module so host tests can compile them with no ESP-IDF, Arduino, or
// mbedtls dependency.
//
// Public keys may travel in the clear. The raw shared secret must stay off
// the wire; packet authentication uses deriveMacKey, and the short
// authentication string uses deriveSas.

namespace NetCrypto {

static constexpr size_t KEY_SIZE = 32;

struct KeyPair {
    uint8_t secret[KEY_SIZE];
    uint8_t publicKey[KEY_SIZE];
};

// Reads 32 secret bytes from the system RNG and sets publicKey = X25519(secret, 9).
KeyPair generateKeyPair();

// publicKey = X25519(secret, 9). Clamping is applied inside the scalar multiply.
void derivePublic(const uint8_t secret[32], uint8_t publicKey[32]);

// out = X25519(ourSecret, theirPublic). Returns false when that shared secret
// is the all-zero value, so a low-order public key cannot become a session key.
bool sharedSecret(const uint8_t ourSecret[32], const uint8_t theirPublic[32],
                  uint8_t out[32]);

// macKey = HMAC-SHA256(sharedSecret, "cardputer-chess-mac-v1").
void deriveMacKey(const uint8_t sharedSecret[32], uint8_t macKey[32]);

// Writes a NUL-terminated 6-digit code. The code is the first 20 bits of
// HMAC-SHA256(sharedSecret,
//   "cardputer-chess-sas-v1" || hostPublic || joinerPublic ||
//   gameId_le16 || sessionId_le32),
// reduced modulo 1000000 so it always fits in six decimal digits.
void deriveSas(const uint8_t sharedSecret[32], const uint8_t hostPublic[32],
               const uint8_t joinerPublic[32], uint16_t gameId,
               uint32_t sessionId, char out[7]);

// Leftmost 32 bits of HMAC-SHA256(macKey, bytes[0, length)).
uint32_t packetTag(const uint8_t macKey[32], const uint8_t* bytes, size_t length);

// Constant-time equality for the 32-bit packet tags.
bool tagsEqual(uint32_t a, uint32_t b);

} // namespace NetCrypto

#endif // NET_CRYPTO_H
