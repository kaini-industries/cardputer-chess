#include <unity.h>

#include "net_crypto.h"

#include <cstdint>
#include <cstring>

namespace {

// RFC 7748 section 6.1. Byte strings are little-endian, in the order printed
// by the RFC.
const uint8_t kAliceSecret[32] = {
    0x77, 0x07, 0x6d, 0x0a, 0x73, 0x18, 0xa5, 0x7d, 0x3c, 0x16, 0xc1,
    0x72, 0x51, 0xb2, 0x66, 0x45, 0xdf, 0x4c, 0x2f, 0x87, 0xeb, 0xc0,
    0x99, 0x2a, 0xb1, 0x77, 0xfb, 0xa5, 0x1d, 0xb9, 0x2c, 0x2a,
};
const uint8_t kAlicePublic[32] = {
    0x85, 0x20, 0xf0, 0x09, 0x89, 0x30, 0xa7, 0x54, 0x74, 0x8b, 0x7d,
    0xdc, 0xb4, 0x3e, 0xf7, 0x5a, 0x0d, 0xbf, 0x3a, 0x0d, 0x26, 0x38,
    0x1a, 0xf4, 0xeb, 0xa4, 0xa9, 0x8e, 0xaa, 0x9b, 0x4e, 0x6a,
};
const uint8_t kBobSecret[32] = {
    0x5d, 0xab, 0x08, 0x7e, 0x62, 0x4a, 0x8a, 0x4b, 0x79, 0xe1, 0x7f,
    0x8b, 0x83, 0x80, 0x0e, 0xe6, 0x6f, 0x3b, 0xb1, 0x29, 0x26, 0x18,
    0xb6, 0xfd, 0x1c, 0x2f, 0x8b, 0x27, 0xff, 0x88, 0xe0, 0xeb,
};
const uint8_t kBobPublic[32] = {
    0xde, 0x9e, 0xdb, 0x7d, 0x7b, 0x7d, 0xc1, 0xb4, 0xd3, 0x5b, 0x61,
    0xc2, 0xec, 0xe4, 0x35, 0x37, 0x3f, 0x83, 0x43, 0xc8, 0x5b, 0x78,
    0x67, 0x4d, 0xad, 0xfc, 0x7e, 0x14, 0x6f, 0x88, 0x2b, 0x4f,
};
const uint8_t kSharedSecret[32] = {
    0x4a, 0x5d, 0x9d, 0x5b, 0xa4, 0xce, 0x2d, 0xe1, 0x72, 0x8e, 0x3b,
    0xf4, 0x80, 0x35, 0x0f, 0x25, 0xe0, 0x7e, 0x21, 0xc9, 0x47, 0xd1,
    0x9e, 0x33, 0x76, 0xf0, 0x9b, 0x3c, 0x1e, 0x16, 0x17, 0x42,
};

void assertSixDigitCode(const char text[7]) {
    TEST_ASSERT_EQUAL_CHAR('\0', text[6]);
    for (int i = 0; i < 6; ++i) {
        TEST_ASSERT_TRUE(text[i] >= '0' && text[i] <= '9');
    }
    TEST_ASSERT_EQUAL_UINT32(6u, static_cast<uint32_t>(std::strlen(text)));
}

void test_rfc7748_section_6_1_public_keys_and_shared_secret() {
    uint8_t alicePublic[32];
    uint8_t bobPublic[32];
    NetCrypto::derivePublic(kAliceSecret, alicePublic);
    NetCrypto::derivePublic(kBobSecret, bobPublic);
    TEST_ASSERT_EQUAL_MEMORY(kAlicePublic, alicePublic, sizeof(alicePublic));
    TEST_ASSERT_EQUAL_MEMORY(kBobPublic, bobPublic, sizeof(bobPublic));

    uint8_t shared[32];
    TEST_ASSERT_TRUE(NetCrypto::sharedSecret(kAliceSecret, kBobPublic, shared));
    TEST_ASSERT_EQUAL_MEMORY(kSharedSecret, shared, sizeof(shared));
}

void test_shared_secret_matches_in_both_directions() {
    uint8_t aliceToBob[32];
    uint8_t bobToAlice[32];
    TEST_ASSERT_TRUE(
        NetCrypto::sharedSecret(kAliceSecret, kBobPublic, aliceToBob));
    TEST_ASSERT_TRUE(
        NetCrypto::sharedSecret(kBobSecret, kAlicePublic, bobToAlice));
    TEST_ASSERT_EQUAL_MEMORY(aliceToBob, bobToAlice, sizeof(aliceToBob));
    TEST_ASSERT_EQUAL_MEMORY(kSharedSecret, aliceToBob, sizeof(aliceToBob));
}

void test_shared_secret_rejects_all_zero_low_order_point() {
    uint8_t zeroPublic[32] = {};
    uint8_t rejected[32];
    std::memset(rejected, 0xa5, sizeof(rejected));
    TEST_ASSERT_FALSE(
        NetCrypto::sharedSecret(kAliceSecret, zeroPublic, rejected));

    uint8_t accepted[32];
    TEST_ASSERT_TRUE(
        NetCrypto::sharedSecret(kAliceSecret, kBobPublic, accepted));
}

void test_derive_sas_is_six_digits_stable_and_session_sensitive() {
    char first[7];
    char second[7];
    char otherSession[7];
    std::memset(first, 'x', sizeof(first));
    std::memset(second, 'x', sizeof(second));
    std::memset(otherSession, 'x', sizeof(otherSession));

    const uint16_t gameId = 0x1234;
    const uint32_t sessionId = 0x89abcdefu;
    NetCrypto::deriveSas(kSharedSecret, kAlicePublic, kBobPublic, gameId,
                         sessionId, first);
    NetCrypto::deriveSas(kSharedSecret, kAlicePublic, kBobPublic, gameId,
                         sessionId, second);
    NetCrypto::deriveSas(kSharedSecret, kAlicePublic, kBobPublic, gameId,
                         sessionId ^ 0x01010101u, otherSession);

    TEST_ASSERT_EQUAL_STRING(first, second);
    assertSixDigitCode(first);
    assertSixDigitCode(otherSession);
    TEST_ASSERT_FALSE(std::strcmp(first, otherSession) == 0);
}

void test_packet_tag_is_stable_and_tag_compare_rejects_a_flip() {
    uint8_t macKey[32];
    NetCrypto::deriveMacKey(kSharedSecret, macKey);

    const uint8_t message[] = {0x10, 0x22, 0x30, 0x44, 0x55, 0x66};
    uint8_t changed[sizeof(message)];
    std::memcpy(changed, message, sizeof(message));
    changed[sizeof(changed) - 1] =
        static_cast<uint8_t>(changed[sizeof(changed) - 1] ^ 0x01u);

    const uint32_t tag = NetCrypto::packetTag(macKey, message, sizeof(message));
    const uint32_t again = NetCrypto::packetTag(macKey, message, sizeof(message));
    const uint32_t different =
        NetCrypto::packetTag(macKey, changed, sizeof(changed));

    TEST_ASSERT_EQUAL_UINT32(tag, again);
    TEST_ASSERT_NOT_EQUAL(tag, different);
    TEST_ASSERT_TRUE(NetCrypto::tagsEqual(tag, tag));
    TEST_ASSERT_FALSE(NetCrypto::tagsEqual(tag, tag ^ 1u));
}

void test_derive_mac_key_is_32_bytes_and_differs_from_shared_secret() {
    uint8_t macKey[32];
    uint8_t again[32];
    std::memset(macKey, 0xa5, sizeof(macKey));
    NetCrypto::deriveMacKey(kSharedSecret, macKey);
    NetCrypto::deriveMacKey(kSharedSecret, again);

    TEST_ASSERT_EQUAL_MEMORY(macKey, again, sizeof(macKey));
    TEST_ASSERT_NOT_EQUAL(0, std::memcmp(macKey, kSharedSecret, sizeof(macKey)));

    bool anyNonZero = false;
    for (size_t i = 0; i < sizeof(macKey); ++i) {
        if (macKey[i] != 0) anyNonZero = true;
    }
    TEST_ASSERT_TRUE(anyNonZero);
}

} // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_rfc7748_section_6_1_public_keys_and_shared_secret);
    RUN_TEST(test_shared_secret_matches_in_both_directions);
    RUN_TEST(test_shared_secret_rejects_all_zero_low_order_point);
    RUN_TEST(test_derive_sas_is_six_digits_stable_and_session_sensitive);
    RUN_TEST(test_packet_tag_is_stable_and_tag_compare_rejects_a_flip);
    RUN_TEST(test_derive_mac_key_is_32_bytes_and_differs_from_shared_secret);
    return UNITY_END();
}
