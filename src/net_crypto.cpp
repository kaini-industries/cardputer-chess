#include "net_crypto.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace {

constexpr uint32_t kSha256Init[8] = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
};

constexpr uint32_t kSha256Round[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

// p = 2^255 - 19, in 16-bit little-endian limbs.
constexpr uint32_t kFieldPrime[16] = {
    0xffedu, 0xffffu, 0xffffu, 0xffffu, 0xffffu, 0xffffu, 0xffffu, 0xffffu,
    0xffffu, 0xffffu, 0xffffu, 0xffffu, 0xffffu, 0xffffu, 0xffffu, 0x7fffu,
};

constexpr int kFieldLimbs = 16;

struct Field {
    uint32_t limb[kFieldLimbs];
};

uint32_t rotateRight(uint32_t value, uint32_t bits) {
    return (value >> bits) | (value << (32u - bits));
}

void wipe(uint8_t* data, size_t length) {
    volatile uint8_t* cursor = data;
    for (size_t i = 0; i < length; ++i) cursor[i] = 0;
}

struct Sha256 {
    uint32_t state[8];
    uint8_t block[64];
    size_t blockLen;
    uint64_t totalBytes;

    void init() {
        for (int i = 0; i < 8; ++i) state[i] = kSha256Init[i];
        blockLen = 0;
        totalBytes = 0;
    }

    void transform(const uint8_t input[64]) {
        uint32_t word[64];
        for (int i = 0; i < 16; ++i) {
            const uint8_t* bytes = input + (i * 4);
            word[i] = (static_cast<uint32_t>(bytes[0]) << 24) |
                      (static_cast<uint32_t>(bytes[1]) << 16) |
                      (static_cast<uint32_t>(bytes[2]) << 8) |
                      static_cast<uint32_t>(bytes[3]);
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotateRight(word[i - 15], 7) ^
                                rotateRight(word[i - 15], 18) ^
                                (word[i - 15] >> 3);
            const uint32_t s1 = rotateRight(word[i - 2], 17) ^
                                rotateRight(word[i - 2], 19) ^
                                (word[i - 2] >> 10);
            word[i] = word[i - 16] + s0 + word[i - 7] + s1;
        }

        uint32_t a = state[0];
        uint32_t b = state[1];
        uint32_t c = state[2];
        uint32_t d = state[3];
        uint32_t e = state[4];
        uint32_t f = state[5];
        uint32_t g = state[6];
        uint32_t h = state[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t sigma1 =
                rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
            const uint32_t choose = (e & f) ^ (~e & g);
            const uint32_t t1 = h + sigma1 + choose + kSha256Round[i] + word[i];
            const uint32_t sigma0 =
                rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = sigma0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    void update(const uint8_t* data, size_t length) {
        if (data == nullptr || length == 0) return;
        totalBytes += length;
        while (length > 0) {
            const size_t room = 64 - blockLen;
            const size_t take = length < room ? length : room;
            std::memcpy(block + blockLen, data, take);
            blockLen += take;
            data += take;
            length -= take;
            if (blockLen == 64) {
                transform(block);
                blockLen = 0;
            }
        }
    }

    void final(uint8_t out[32]) {
        const uint64_t bitLength = totalBytes * 8u;
        block[blockLen++] = 0x80;
        if (blockLen > 56) {
            while (blockLen < 64) block[blockLen++] = 0;
            transform(block);
            blockLen = 0;
        }
        while (blockLen < 56) block[blockLen++] = 0;
        for (int shift = 56; shift >= 0; shift -= 8) {
            block[blockLen++] =
                static_cast<uint8_t>((bitLength >> shift) & 0xffu);
        }
        transform(block);
        for (int i = 0; i < 8; ++i) {
            out[i * 4] = static_cast<uint8_t>(state[i] >> 24);
            out[i * 4 + 1] = static_cast<uint8_t>(state[i] >> 16);
            out[i * 4 + 2] = static_cast<uint8_t>(state[i] >> 8);
            out[i * 4 + 3] = static_cast<uint8_t>(state[i]);
        }
    }
};

void hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* message,
                size_t messageLen, uint8_t out[32]) {
    uint8_t keyBlock[64];
    std::memset(keyBlock, 0, sizeof(keyBlock));
    if (keyLen > sizeof(keyBlock)) {
        Sha256 hashed;
        hashed.init();
        hashed.update(key, keyLen);
        hashed.final(keyBlock);
    } else if (key != nullptr && keyLen > 0) {
        std::memcpy(keyBlock, key, keyLen);
    }

    uint8_t innerPad[64];
    uint8_t outerPad[64];
    for (size_t i = 0; i < sizeof(keyBlock); ++i) {
        innerPad[i] = static_cast<uint8_t>(keyBlock[i] ^ 0x36u);
        outerPad[i] = static_cast<uint8_t>(keyBlock[i] ^ 0x5cu);
    }

    uint8_t innerHash[32];
    Sha256 inner;
    inner.init();
    inner.update(innerPad, sizeof(innerPad));
    inner.update(message, messageLen);
    inner.final(innerHash);

    Sha256 outer;
    outer.init();
    outer.update(outerPad, sizeof(outerPad));
    outer.update(innerHash, sizeof(innerHash));
    outer.final(out);

    wipe(keyBlock, sizeof(keyBlock));
    wipe(innerPad, sizeof(innerPad));
    wipe(outerPad, sizeof(outerPad));
    wipe(innerHash, sizeof(innerHash));
}

void fieldClear(Field& value) {
    std::memset(value.limb, 0, sizeof(value.limb));
}

void fieldSetOne(Field& value) {
    fieldClear(value);
    value.limb[0] = 1;
}

void fieldSetWord(Field& value, uint32_t word) {
    fieldClear(value);
    value.limb[0] = word & 0xffffu;
    value.limb[1] = word >> 16;
}

bool fieldAtLeastPrime(const uint32_t limb[kFieldLimbs]) {
    if (limb[15] != kFieldPrime[15]) return limb[15] > kFieldPrime[15];
    for (int i = 14; i >= 1; --i) {
        if (limb[i] != kFieldPrime[i]) return limb[i] > kFieldPrime[i];
    }
    return limb[0] >= kFieldPrime[0];
}

void fieldSubtractPrime(uint32_t limb[kFieldLimbs]) {
    uint32_t borrow = 0;
    for (int i = 0; i < kFieldLimbs; ++i) {
        const uint32_t subtrahend = kFieldPrime[i] + borrow;
        if (limb[i] < subtrahend) {
            limb[i] = (limb[i] + 65536u) - subtrahend;
            borrow = 1;
        } else {
            limb[i] -= subtrahend;
            borrow = 0;
        }
    }
}

void fieldSubtractWord(uint32_t limb[kFieldLimbs], uint32_t word) {
    uint32_t borrow = 0;
    for (int i = 0; i < kFieldLimbs; ++i) {
        const uint32_t piece = (i == 0 ? word : 0u) + borrow;
        if (limb[i] < piece) {
            limb[i] = (limb[i] + 65536u) - piece;
            borrow = 1;
        } else {
            limb[i] -= piece;
            borrow = 0;
        }
    }
}

void fieldCanonicalize(uint32_t limb[kFieldLimbs]) {
    for (int step = 0; step < 4 && fieldAtLeastPrime(limb); ++step) {
        fieldSubtractPrime(limb);
    }
}

void fieldStore(Field& out, const uint32_t limb[kFieldLimbs]) {
    for (int i = 0; i < kFieldLimbs; ++i) out.limb[i] = limb[i];
}

void fieldAdd(Field& out, const Field& left, const Field& right) {
    uint32_t limb[kFieldLimbs];
    uint32_t carry = 0;
    for (int i = 0; i < kFieldLimbs; ++i) {
        const uint32_t sum = left.limb[i] + right.limb[i] + carry;
        limb[i] = sum & 0xffffu;
        carry = sum >> 16;
    }
    if (carry != 0) {
        uint32_t extra = carry * 38u;
        for (int i = 0; i < kFieldLimbs && extra != 0; ++i) {
            const uint32_t sum = limb[i] + extra;
            limb[i] = sum & 0xffffu;
            extra = sum >> 16;
        }
    }
    fieldCanonicalize(limb);
    fieldStore(out, limb);
}

void fieldSubtract(Field& out, const Field& left, const Field& right) {
    uint32_t limb[kFieldLimbs];
    uint32_t borrow = 0;
    for (int i = 0; i < kFieldLimbs; ++i) {
        const uint32_t subtrahend = right.limb[i] + borrow;
        if (left.limb[i] < subtrahend) {
            limb[i] = (left.limb[i] + 65536u) - subtrahend;
            borrow = 1;
        } else {
            limb[i] = left.limb[i] - subtrahend;
            borrow = 0;
        }
    }
    // A wrapped subtraction is (left - right + 2^256). Since 2^256 ≡ 38,
    // subtract 38 and then one prime to land back in [0, p).
    if (borrow != 0) {
        fieldSubtractWord(limb, 38u);
        fieldSubtractPrime(limb);
    }
    fieldStore(out, limb);
}

void fieldMultiply(Field& out, const Field& left, const Field& right) {
    uint64_t product[32] = {};
    for (int i = 0; i < kFieldLimbs; ++i) {
        for (int j = 0; j < kFieldLimbs; ++j) {
            product[i + j] += static_cast<uint64_t>(left.limb[i]) * right.limb[j];
        }
    }

    uint32_t wide[32];
    uint64_t carry = 0;
    for (int i = 0; i < 32; ++i) {
        carry += product[i];
        wide[i] = static_cast<uint32_t>(carry & 0xffffu);
        carry >>= 16;
    }

    // 2^256 ≡ 38 (mod 2^255 - 19), so fold the high half into the low half.
    uint32_t limb[kFieldLimbs];
    carry = 0;
    for (int i = 0; i < kFieldLimbs; ++i) {
        carry += static_cast<uint64_t>(wide[i]) + 38ull * wide[i + kFieldLimbs];
        limb[i] = static_cast<uint32_t>(carry & 0xffffu);
        carry >>= 16;
    }
    while (carry != 0) {
        uint64_t extra = carry * 38ull;
        carry = 0;
        for (int i = 0; i < kFieldLimbs; ++i) {
            extra += limb[i];
            limb[i] = static_cast<uint32_t>(extra & 0xffffu);
            extra >>= 16;
        }
        carry = extra;
    }
    fieldCanonicalize(limb);
    fieldStore(out, limb);
}

void fieldSwap(Field& left, Field& right, uint32_t swap) {
    const uint32_t mask = 0u - (swap & 1u);
    for (int i = 0; i < kFieldLimbs; ++i) {
        const uint32_t diff = mask & (left.limb[i] ^ right.limb[i]);
        left.limb[i] ^= diff;
        right.limb[i] ^= diff;
    }
}

bool primeMinusTwoBit(int index) {
    // p - 2 = 2^255 - 21. Bits 8..254 are set. The low byte is 0xEB.
    if (index >= 8) return true;
    return ((0xEBu >> index) & 1u) != 0u;
}

void fieldInvert(Field& out, const Field& value) {
    Field result;
    fieldSetOne(result);
    for (int bit = 254; bit >= 0; --bit) {
        Field squared;
        fieldMultiply(squared, result, result);
        if (primeMinusTwoBit(bit)) {
            fieldMultiply(result, squared, value);
        } else {
            result = squared;
        }
    }
    out = result;
}

void fieldFromBytes(Field& out, const uint8_t bytes[32]) {
    uint32_t limb[kFieldLimbs];
    for (int i = 0; i < kFieldLimbs; ++i) {
        limb[i] = static_cast<uint32_t>(bytes[i * 2]) |
                  (static_cast<uint32_t>(bytes[i * 2 + 1]) << 8);
    }
    limb[15] &= 0x7fffu;
    fieldCanonicalize(limb);
    fieldStore(out, limb);
}

void fieldToBytes(uint8_t out[32], const Field& value) {
    for (int i = 0; i < kFieldLimbs; ++i) {
        out[i * 2] = static_cast<uint8_t>(value.limb[i] & 0xffu);
        out[i * 2 + 1] = static_cast<uint8_t>((value.limb[i] >> 8) & 0xffu);
    }
}

// RFC 7748 section 5 Montgomery ladder. `scalar` and `point` are 32-byte
// little-endian strings. The scalar is clamped here; the unused high bit of
// the u-coordinate is ignored.
void x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]) {
    uint8_t clamped[32];
    std::memcpy(clamped, scalar, 32);
    clamped[0] &= 248u;
    clamped[31] &= 127u;
    clamped[31] |= 64u;

    uint8_t uCoordinate[32];
    std::memcpy(uCoordinate, point, 32);
    uCoordinate[31] &= 127u;

    Field x1;
    fieldFromBytes(x1, uCoordinate);
    Field x2;
    fieldSetOne(x2);
    Field z2;
    fieldClear(z2);
    Field x3 = x1;
    Field z3;
    fieldSetOne(z3);
    Field a24;
    fieldSetWord(a24, 121665u);

    uint32_t swap = 0;
    for (int bit = 254; bit >= 0; --bit) {
        const uint32_t keyBit =
            (static_cast<uint32_t>(clamped[bit >> 3]) >> (bit & 7)) & 1u;
        swap ^= keyBit;
        fieldSwap(x2, x3, swap);
        fieldSwap(z2, z3, swap);
        swap = keyBit;

        Field a;
        Field aa;
        Field b;
        Field bb;
        Field e;
        Field c;
        Field d;
        Field da;
        Field cb;
        Field sum;
        Field diff;
        Field squaredDiff;
        Field scaled;
        fieldAdd(a, x2, z2);
        fieldMultiply(aa, a, a);
        fieldSubtract(b, x2, z2);
        fieldMultiply(bb, b, b);
        fieldSubtract(e, aa, bb);
        fieldAdd(c, x3, z3);
        fieldSubtract(d, x3, z3);
        fieldMultiply(da, d, a);
        fieldMultiply(cb, c, b);
        fieldAdd(sum, da, cb);
        fieldMultiply(x3, sum, sum);
        fieldSubtract(diff, da, cb);
        fieldMultiply(squaredDiff, diff, diff);
        fieldMultiply(z3, x1, squaredDiff);
        fieldMultiply(x2, aa, bb);
        fieldMultiply(scaled, a24, e);
        fieldAdd(sum, aa, scaled);
        fieldMultiply(z2, e, sum);
    }

    fieldSwap(x2, x3, swap);
    fieldSwap(z2, z3, swap);

    Field inverseZ;
    fieldInvert(inverseZ, z2);
    Field result;
    fieldMultiply(result, x2, inverseZ);
    fieldToBytes(out, result);
    wipe(clamped, sizeof(clamped));
}

bool readSystemRandom(uint8_t* out, size_t length) {
    std::ifstream entropy("/dev/urandom", std::ios::in | std::ios::binary);
    if (!entropy) return false;
    entropy.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(length));
    return entropy.gcount() == static_cast<std::streamsize>(length);
}

void fillWeakRandom(uint8_t out[32]) {
    uint32_t state = 0x6a09e667u;
    state ^= static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&fillWeakRandom));
    const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    state ^= static_cast<uint32_t>(ticks);
    for (int i = 0; i < 32; ++i) {
        state = state * 1664525u + 1013904223u;
        out[i] = static_cast<uint8_t>(state >> 16);
    }
}

void writeSixDigits(char out[7], uint32_t value) {
    uint32_t divisor = 100000u;
    for (int i = 0; i < 6; ++i) {
        out[i] = static_cast<char>('0' + ((value / divisor) % 10u));
        divisor /= 10u;
    }
    out[6] = '\0';
}

} // namespace

namespace NetCrypto {

KeyPair generateKeyPair() {
    KeyPair keys{};
    if (!readSystemRandom(keys.secret, KEY_SIZE)) fillWeakRandom(keys.secret);
    derivePublic(keys.secret, keys.publicKey);
    return keys;
}

void derivePublic(const uint8_t secret[32], uint8_t publicKey[32]) {
    uint8_t basePoint[32] = {};
    basePoint[0] = 9;
    x25519(publicKey, secret, basePoint);
}

bool sharedSecret(const uint8_t ourSecret[32], const uint8_t theirPublic[32],
                  uint8_t out[32]) {
    x25519(out, ourSecret, theirPublic);
    uint8_t combined = 0;
    for (size_t i = 0; i < KEY_SIZE; ++i) combined |= out[i];
    return combined != 0;
}

void deriveMacKey(const uint8_t sharedSecret[32], uint8_t macKey[32]) {
    static const char label[] = "cardputer-chess-mac-v1";
    static_assert(sizeof(label) == 23, "MAC domain label is 22 bytes plus NUL");
    hmacSha256(sharedSecret, KEY_SIZE, reinterpret_cast<const uint8_t*>(label),
               sizeof(label) - 1, macKey);
}

void deriveSas(const uint8_t sharedSecret[32], const uint8_t hostPublic[32],
               const uint8_t joinerPublic[32], uint16_t gameId, uint32_t sessionId,
               char out[7]) {
    static const char label[] = "cardputer-chess-sas-v1";
    static_assert(sizeof(label) == 23, "SAS domain label is 22 bytes plus NUL");
    constexpr size_t labelLen = sizeof(label) - 1;
    uint8_t message[labelLen + KEY_SIZE + KEY_SIZE + 2 + 4];
    size_t offset = 0;
    std::memcpy(message + offset, label, labelLen);
    offset += labelLen;
    std::memcpy(message + offset, hostPublic, KEY_SIZE);
    offset += KEY_SIZE;
    std::memcpy(message + offset, joinerPublic, KEY_SIZE);
    offset += KEY_SIZE;
    message[offset++] = static_cast<uint8_t>(gameId);
    message[offset++] = static_cast<uint8_t>(gameId >> 8);
    message[offset++] = static_cast<uint8_t>(sessionId);
    message[offset++] = static_cast<uint8_t>(sessionId >> 8);
    message[offset++] = static_cast<uint8_t>(sessionId >> 16);
    message[offset++] = static_cast<uint8_t>(sessionId >> 24);

    uint8_t mac[KEY_SIZE];
    hmacSha256(sharedSecret, KEY_SIZE, message, offset, mac);
    const uint32_t bits20 = (static_cast<uint32_t>(mac[0]) << 12) |
                            (static_cast<uint32_t>(mac[1]) << 4) |
                            (static_cast<uint32_t>(mac[2]) >> 4);
    writeSixDigits(out, bits20 % 1000000u);
    wipe(mac, sizeof(mac));
    wipe(message, sizeof(message));
}

uint32_t packetTag(const uint8_t macKey[32], const uint8_t* bytes, size_t length) {
    uint8_t mac[KEY_SIZE];
    hmacSha256(macKey, KEY_SIZE, bytes, length, mac);
    const uint32_t tag = (static_cast<uint32_t>(mac[0]) << 24) |
                         (static_cast<uint32_t>(mac[1]) << 16) |
                         (static_cast<uint32_t>(mac[2]) << 8) |
                         static_cast<uint32_t>(mac[3]);
    wipe(mac, sizeof(mac));
    return tag;
}

bool tagsEqual(uint32_t a, uint32_t b) {
    uint32_t diff = a ^ b;
    diff |= diff >> 16;
    diff |= diff >> 8;
    diff |= diff >> 4;
    diff |= diff >> 2;
    diff |= diff >> 1;
    return (diff & 1u) == 0u;
}

} // namespace NetCrypto
