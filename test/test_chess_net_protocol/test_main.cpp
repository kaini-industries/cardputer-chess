#include <unity.h>

#include "chess_net_protocol.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace {

void assertGameHeaderDefaults(const NetGameHeader& header,
                              NetMsgType expectedType) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expectedType),
                            static_cast<uint8_t>(header.type));
    TEST_ASSERT_EQUAL_UINT8(NET_PROTOCOL_VERSION, header.version);
    TEST_ASSERT_EQUAL_UINT16(0, header.gameId);
    TEST_ASSERT_EQUAL_UINT32(0, header.sessionId);
}

void test_wire_layout_has_exact_bounded_packet_sizes() {
    TEST_ASSERT_EQUAL_UINT8(5, NET_PROTOCOL_VERSION);
    TEST_ASSERT_EQUAL_UINT8(32, NET_PACKET_MAX_SIZE);

    TEST_ASSERT_EQUAL_UINT32(4, sizeof(NetPairingHeader));
    TEST_ASSERT_EQUAL_UINT32(8, sizeof(NetGameHeader));
    TEST_ASSERT_EQUAL_UINT32(21, sizeof(DiscoveryMsg));
    TEST_ASSERT_EQUAL_UINT32(17, sizeof(AcceptGameMsg));
    TEST_ASSERT_EQUAL_UINT32(13, sizeof(GameStartMsg));
    TEST_ASSERT_EQUAL_UINT32(8, sizeof(GameStartAckMsg));
    TEST_ASSERT_EQUAL_UINT32(28, sizeof(MoveNetMsg));
    TEST_ASSERT_EQUAL_UINT32(15, sizeof(MoveAckMsg));
    TEST_ASSERT_EQUAL_UINT32(21, sizeof(HeartbeatMsg));
    TEST_ASSERT_EQUAL_UINT32(10, sizeof(ResignMsg));
    TEST_ASSERT_EQUAL_UINT32(31, sizeof(ControlNetMsg));
    TEST_ASSERT_EQUAL_UINT32(20, sizeof(ControlAckMsg));

    TEST_ASSERT_EQUAL_UINT32(0, offsetof(DiscoveryMsg, header));
    TEST_ASSERT_EQUAL_UINT32(0, offsetof(AcceptGameMsg, header));
    TEST_ASSERT_EQUAL_UINT32(0, offsetof(GameStartMsg, header));
    TEST_ASSERT_EQUAL_UINT32(0, offsetof(MoveNetMsg, header));
    TEST_ASSERT_EQUAL_UINT32(8, offsetof(MoveNetMsg, sequence));
    TEST_ASSERT_EQUAL_UINT32(16, offsetof(MoveNetMsg, moverRemainingMs));
    TEST_ASSERT_EQUAL_UINT32(20, offsetof(MoveNetMsg, preBoardHash));
    TEST_ASSERT_EQUAL_UINT32(24, offsetof(MoveNetMsg, postBoardHash));
    TEST_ASSERT_EQUAL_UINT32(13, offsetof(HeartbeatMsg, positionEpoch));
    TEST_ASSERT_EQUAL_UINT32(15, offsetof(HeartbeatMsg, lastAppliedSequence));
    TEST_ASSERT_EQUAL_UINT32(16, offsetof(ControlAckMsg, clockRemainingMs));

    TEST_ASSERT_TRUE(sizeof(DiscoveryMsg) <= NET_PACKET_MAX_SIZE);
    TEST_ASSERT_TRUE(sizeof(AcceptGameMsg) <= NET_PACKET_MAX_SIZE);
    TEST_ASSERT_TRUE(sizeof(GameStartMsg) <= NET_PACKET_MAX_SIZE);
    TEST_ASSERT_TRUE(sizeof(GameStartAckMsg) <= NET_PACKET_MAX_SIZE);
    TEST_ASSERT_TRUE(sizeof(MoveNetMsg) <= NET_PACKET_MAX_SIZE);
    TEST_ASSERT_TRUE(sizeof(MoveAckMsg) <= NET_PACKET_MAX_SIZE);
    TEST_ASSERT_TRUE(sizeof(HeartbeatMsg) <= NET_PACKET_MAX_SIZE);
    TEST_ASSERT_TRUE(sizeof(ResignMsg) <= NET_PACKET_MAX_SIZE);
    TEST_ASSERT_TRUE(sizeof(ControlNetMsg) <= NET_PACKET_MAX_SIZE);
    TEST_ASSERT_TRUE(sizeof(ControlAckMsg) <= NET_PACKET_MAX_SIZE);
}

void test_default_messages_have_protocol_version_and_expected_type() {
    const DiscoveryMsg discovery;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NetMsgType::Discovery),
                            static_cast<uint8_t>(discovery.header.type));
    TEST_ASSERT_EQUAL_UINT8(NET_PROTOCOL_VERSION, discovery.header.version);
    TEST_ASSERT_EQUAL_UINT16(0, discovery.header.gameId);
    TEST_ASSERT_EQUAL_UINT8(0, discovery.variant);
    TEST_ASSERT_EQUAL_UINT16(518, discovery.positionIndex);
    TEST_ASSERT_EQUAL_UINT8(0, discovery.timeControl);
    TEST_ASSERT_EQUAL_CHAR('\0', discovery.displayName[0]);

    const AcceptGameMsg accept;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NetMsgType::AcceptGame),
                            static_cast<uint8_t>(accept.header.type));
    TEST_ASSERT_EQUAL_UINT8(NET_PROTOCOL_VERSION, accept.header.version);
    TEST_ASSERT_EQUAL_UINT16(0, accept.header.gameId);
    TEST_ASSERT_EQUAL_CHAR('\0', accept.displayName[0]);

    const GameStartMsg start;
    assertGameHeaderDefaults(start.header, NetMsgType::GameStart);
    TEST_ASSERT_EQUAL_UINT16(518, start.positionIndex);

    const GameStartAckMsg startAck;
    assertGameHeaderDefaults(startAck.header, NetMsgType::GameStartAck);

    const MoveNetMsg move;
    assertGameHeaderDefaults(move.header, NetMsgType::MoveMsg);

    const MoveAckMsg moveAck;
    assertGameHeaderDefaults(moveAck.header, NetMsgType::MoveAck);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NetAckStatus::Accepted),
                            static_cast<uint8_t>(moveAck.status));

    const HeartbeatMsg heartbeat;
    assertGameHeaderDefaults(heartbeat.header, NetMsgType::Heartbeat);
    TEST_ASSERT_EQUAL_UINT16(0, heartbeat.positionEpoch);
    TEST_ASSERT_EQUAL_UINT16(0, heartbeat.lastAppliedSequence);

    const ResignMsg resign;
    assertGameHeaderDefaults(resign.header, NetMsgType::Resign);

    const ControlNetMsg control;
    assertGameHeaderDefaults(control.header, NetMsgType::ControlMsg);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NetControlType::DrawOffer),
                            static_cast<uint8_t>(control.control));

    const ControlAckMsg controlAck;
    assertGameHeaderDefaults(controlAck.header, NetMsgType::ControlAck);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NetAckStatus::Accepted),
                            static_cast<uint8_t>(controlAck.status));
    TEST_ASSERT_EQUAL_UINT32(0, controlAck.clockRemainingMs);
}

void test_pairing_header_validation_rejects_wrong_or_stale_games() {
    const NetPairingHeader current(NetMsgType::Discovery, 0x1234);
    TEST_ASSERT_TRUE(
        isValidPairingHeader(current, NetMsgType::Discovery));
    TEST_ASSERT_TRUE(
        isValidPairingHeader(current, NetMsgType::Discovery, 0x1234));
    TEST_ASSERT_FALSE(
        isValidPairingHeader(current, NetMsgType::AcceptGame, 0x1234));
    TEST_ASSERT_FALSE(
        isValidPairingHeader(current, NetMsgType::Discovery, 0x1235));

    NetPairingHeader invalid = current;
    invalid.version = static_cast<uint8_t>(NET_PROTOCOL_VERSION - 1);
    TEST_ASSERT_FALSE(
        isValidPairingHeader(invalid, NetMsgType::Discovery, 0x1234));

    invalid = current;
    invalid.gameId = 0;
    TEST_ASSERT_FALSE(
        isValidPairingHeader(invalid, NetMsgType::Discovery));
}

void test_game_header_validation_rejects_stale_game_and_session_ids() {
    const uint16_t currentGame = 0xCAFE;
    const uint32_t currentSession = 0xDEADBEEF;
    const NetGameHeader current(NetMsgType::MoveMsg, currentGame,
                                currentSession);

    TEST_ASSERT_TRUE(isValidGameHeader(current, NetMsgType::MoveMsg,
                                       currentGame, currentSession));
    TEST_ASSERT_TRUE(isValidGameHeader(current, NetMsgType::MoveMsg,
                                       currentGame));
    TEST_ASSERT_FALSE(isValidGameHeader(current, NetMsgType::MoveAck,
                                        currentGame, currentSession));
    TEST_ASSERT_FALSE(isValidGameHeader(current, NetMsgType::MoveMsg,
                                        static_cast<uint16_t>(currentGame - 1),
                                        currentSession));
    TEST_ASSERT_FALSE(isValidGameHeader(current, NetMsgType::MoveMsg,
                                        currentGame, currentSession - 1));

    NetGameHeader invalid = current;
    invalid.version = static_cast<uint8_t>(NET_PROTOCOL_VERSION + 1);
    TEST_ASSERT_FALSE(isValidGameHeader(invalid, NetMsgType::MoveMsg,
                                        currentGame, currentSession));

    invalid = current;
    invalid.gameId = 0;
    TEST_ASSERT_FALSE(isValidGameHeader(invalid, NetMsgType::MoveMsg, 0,
                                        currentSession));

    invalid = current;
    invalid.sessionId = 0;
    TEST_ASSERT_FALSE(isValidGameHeader(invalid, NetMsgType::MoveMsg,
                                        currentGame));
}

void test_copy_display_name_is_bounded_sanitized_and_terminated() {
    char destination[NET_DISPLAY_NAME_BYTES];
    std::memset(destination, 'X', sizeof(destination));

    copyNetDisplayName(destination, nullptr);
    for (uint8_t i = 0; i < NET_DISPLAY_NAME_BYTES; ++i) {
        TEST_ASSERT_EQUAL_CHAR('\0', destination[i]);
    }
    TEST_ASSERT_TRUE(isValidNetDisplayName(destination));

    copyNetDisplayName(destination, "Ada");
    TEST_ASSERT_EQUAL_STRING("Ada", destination);
    for (uint8_t i = 4; i < NET_DISPLAY_NAME_BYTES; ++i) {
        TEST_ASSERT_EQUAL_CHAR('\0', destination[i]);
    }

    copyNetDisplayName(destination, "123456789012EXTRA");
    TEST_ASSERT_EQUAL_STRING("123456789012", destination);
    TEST_ASSERT_EQUAL_CHAR('\0', destination[NET_DISPLAY_NAME_MAX]);
    TEST_ASSERT_TRUE(isValidNetDisplayName(destination));

    const char controls[] = {'A', '\t', static_cast<char>(0x7F),
                             static_cast<char>(0x80), 'Z', '\0'};
    copyNetDisplayName(destination, controls);
    TEST_ASSERT_EQUAL_STRING("A???Z", destination);
    TEST_ASSERT_TRUE(isValidNetDisplayName(destination));
}

void test_display_name_validation_rejects_bad_bytes_and_missing_terminator() {
    char exact[NET_DISPLAY_NAME_BYTES] = "123456789012";
    TEST_ASSERT_TRUE(isValidNetDisplayName(exact));

    char empty[NET_DISPLAY_NAME_BYTES] = {};
    TEST_ASSERT_TRUE(isValidNetDisplayName(empty));

    char embeddedNull[NET_DISPLAY_NAME_BYTES] = {};
    embeddedNull[0] = 'A';
    embeddedNull[1] = '\0';
    embeddedNull[2] = static_cast<char>(0x80);
    TEST_ASSERT_TRUE(isValidNetDisplayName(embeddedNull));

    char unterminated[NET_DISPLAY_NAME_BYTES];
    std::memset(unterminated, 'A', sizeof(unterminated));
    TEST_ASSERT_FALSE(isValidNetDisplayName(unterminated));

    char control[NET_DISPLAY_NAME_BYTES] = "Good";
    control[2] = '\n';
    TEST_ASSERT_FALSE(isValidNetDisplayName(control));

    char del[NET_DISPLAY_NAME_BYTES] = "Good";
    del[1] = static_cast<char>(0x7F);
    TEST_ASSERT_FALSE(isValidNetDisplayName(del));

    char highBit[NET_DISPLAY_NAME_BYTES] = "Good";
    highBit[1] = static_cast<char>(0x80);
    TEST_ASSERT_FALSE(isValidNetDisplayName(highBit));
}

void test_variant_color_time_and_position_values_are_bounded() {
    TEST_ASSERT_TRUE(isValidChessVariantValue(
        static_cast<uint8_t>(ChessVariant::Standard)));
    TEST_ASSERT_TRUE(isValidChessVariantValue(
        static_cast<uint8_t>(ChessVariant::Chess960)));
    TEST_ASSERT_FALSE(isValidChessVariantValue(2));
    TEST_ASSERT_FALSE(isValidChessVariantValue(0xFF));

    TEST_ASSERT_TRUE(isValidColorValue(
        static_cast<uint8_t>(PieceColor::White)));
    TEST_ASSERT_TRUE(isValidColorValue(
        static_cast<uint8_t>(PieceColor::Black)));
    TEST_ASSERT_FALSE(isValidColorValue(2));

    for (uint8_t value = static_cast<uint8_t>(TimeControl::None);
         value <= static_cast<uint8_t>(TimeControl::Rapid10); ++value) {
        TEST_ASSERT_TRUE(isValidTimeControlValue(value));
    }
    TEST_ASSERT_FALSE(isValidTimeControlValue(
        static_cast<uint8_t>(TimeControl::Rapid10) + 1));
    TEST_ASSERT_FALSE(isValidTimeControlValue(0xFF));

    const uint8_t standard = static_cast<uint8_t>(ChessVariant::Standard);
    const uint8_t chess960 = static_cast<uint8_t>(ChessVariant::Chess960);
    TEST_ASSERT_TRUE(isValidPositionIndex(standard, 518));
    TEST_ASSERT_FALSE(isValidPositionIndex(standard, 0));
    TEST_ASSERT_FALSE(isValidPositionIndex(standard, 517));
    TEST_ASSERT_FALSE(isValidPositionIndex(standard, 519));
    TEST_ASSERT_FALSE(isValidPositionIndex(standard, 959));

    TEST_ASSERT_TRUE(isValidPositionIndex(chess960, 0));
    TEST_ASSERT_TRUE(isValidPositionIndex(chess960, 518));
    TEST_ASSERT_TRUE(isValidPositionIndex(chess960, 959));
    TEST_ASSERT_FALSE(isValidPositionIndex(chess960, 960));
    TEST_ASSERT_FALSE(isValidPositionIndex(0xFF, 518));
}

void test_move_wire_roundtrip_preserves_sequence_clock_hashes_and_flags() {
    Move source;
    source.from = Square{7, 6};
    source.to = Square{7, 7};
    source.promotion = PieceType::Queen;
    source.isCastle = true;
    source.isEnPassant = true;

    const uint16_t sequence = std::numeric_limits<uint16_t>::max();
    const uint16_t gameId = 0xFFFE;
    const uint32_t sessionId = 0xFEDCBA98;
    const uint32_t remainingMs = 0xFFFFFFFE;
    const uint32_t preHash = 0x01234567;
    const uint32_t postHash = 0x89ABCDEF;
    const MoveNetMsg encoded = moveToNetMsg(
        source, sequence, gameId, sessionId, remainingMs, preHash, postHash);

    TEST_ASSERT_TRUE(isValidGameHeader(encoded.header, NetMsgType::MoveMsg,
                                       gameId, sessionId));
    TEST_ASSERT_EQUAL_UINT16(sequence, encoded.sequence);
    TEST_ASSERT_EQUAL_UINT32(remainingMs, encoded.moverRemainingMs);
    TEST_ASSERT_EQUAL_UINT32(preHash, encoded.preBoardHash);
    TEST_ASSERT_EQUAL_UINT32(postHash, encoded.postBoardHash);
    TEST_ASSERT_EQUAL_HEX8(0x03, encoded.flags);

    uint8_t wire[sizeof(MoveNetMsg)] = {};
    std::memcpy(wire, &encoded, sizeof(encoded));
    MoveNetMsg decoded;
    std::memcpy(&decoded, wire, sizeof(decoded));

    TEST_ASSERT_EQUAL_UINT16(sequence, decoded.sequence);
    TEST_ASSERT_EQUAL_UINT16(gameId, decoded.header.gameId);
    TEST_ASSERT_EQUAL_UINT32(sessionId, decoded.header.sessionId);
    TEST_ASSERT_EQUAL_UINT32(remainingMs, decoded.moverRemainingMs);
    TEST_ASSERT_EQUAL_UINT32(preHash, decoded.preBoardHash);
    TEST_ASSERT_EQUAL_UINT32(postHash, decoded.postBoardHash);
    TEST_ASSERT_TRUE(source == netMsgToMove(decoded));
}

void test_move_roundtrip_keeps_each_flag_independent() {
    Move castle;
    castle.from = Square{4, 0};
    castle.to = Square{6, 0};
    castle.isCastle = true;
    const MoveNetMsg castleMsg = moveToNetMsg(castle, 1, 1, 1, 100, 2, 3);
    TEST_ASSERT_EQUAL_HEX8(0x01, castleMsg.flags);
    TEST_ASSERT_TRUE(castle == netMsgToMove(castleMsg));

    Move enPassant;
    enPassant.from = Square{4, 4};
    enPassant.to = Square{3, 5};
    enPassant.isEnPassant = true;
    const MoveNetMsg enPassantMsg =
        moveToNetMsg(enPassant, 2, 1, 1, 99, 3, 4);
    TEST_ASSERT_EQUAL_HEX8(0x02, enPassantMsg.flags);
    TEST_ASSERT_TRUE(enPassant == netMsgToMove(enPassantMsg));

    Move ordinary;
    ordinary.from = Square{4, 1};
    ordinary.to = Square{4, 3};
    const MoveNetMsg ordinaryMsg =
        moveToNetMsg(ordinary, 3, 1, 1, 98, 4, 5);
    TEST_ASSERT_EQUAL_HEX8(0x00, ordinaryMsg.flags);
    TEST_ASSERT_TRUE(ordinary == netMsgToMove(ordinaryMsg));
}

void test_position_epoch_classifies_match_and_same_epoch_hash_mismatch() {
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(NetPositionRelation::Match),
        static_cast<uint8_t>(classifyNetPosition(42, 0x12345678,
                                                42, 0x12345678)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(NetPositionRelation::Diverged),
        static_cast<uint8_t>(classifyNetPosition(42, 0x12345678,
                                                42, 0x87654321)));
}

void test_position_epoch_classifies_one_ahead_behind_and_forward_jump() {
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(NetPositionRelation::PeerAheadOne),
        static_cast<uint8_t>(classifyNetPosition(42, 0x11111111,
                                                43, 0x22222222)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(NetPositionRelation::PeerBehind),
        static_cast<uint8_t>(classifyNetPosition(42, 0x11111111,
                                                41, 0x22222222)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(NetPositionRelation::PeerBehind),
        static_cast<uint8_t>(classifyNetPosition(42, 0x11111111,
                                                12, 0x22222222)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(NetPositionRelation::Diverged),
        static_cast<uint8_t>(classifyNetPosition(42, 0x11111111,
                                                44, 0x22222222)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(NetPositionRelation::Diverged),
        static_cast<uint8_t>(classifyNetPosition(0, 0x11111111,
                                                0x8000, 0x22222222)));
}

void test_position_epoch_classification_is_wrap_safe() {
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(NetPositionRelation::PeerAheadOne),
        static_cast<uint8_t>(classifyNetPosition(0xFFFF, 0x11111111,
                                                0, 0x22222222)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(NetPositionRelation::PeerBehind),
        static_cast<uint8_t>(classifyNetPosition(0, 0x11111111,
                                                0xFFFF, 0x22222222)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(NetPositionRelation::Diverged),
        static_cast<uint8_t>(classifyNetPosition(0xFFFE, 0x11111111,
                                                1, 0x22222222)));
}

void test_move_clock_value_is_canonical_for_timed_and_untimed_games() {
    TEST_ASSERT_FALSE(isValidMoveClockValue(true, 0));
    TEST_ASSERT_TRUE(isValidMoveClockValue(true, 1));
    TEST_ASSERT_TRUE(isValidMoveClockValue(
        true, std::numeric_limits<uint32_t>::max()));

    TEST_ASSERT_TRUE(isValidMoveClockValue(false, 0));
    TEST_ASSERT_FALSE(isValidMoveClockValue(false, 1));
    TEST_ASSERT_FALSE(isValidMoveClockValue(
        false, std::numeric_limits<uint32_t>::max()));
}

} // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_wire_layout_has_exact_bounded_packet_sizes);
    RUN_TEST(test_default_messages_have_protocol_version_and_expected_type);
    RUN_TEST(test_pairing_header_validation_rejects_wrong_or_stale_games);
    RUN_TEST(test_game_header_validation_rejects_stale_game_and_session_ids);
    RUN_TEST(test_copy_display_name_is_bounded_sanitized_and_terminated);
    RUN_TEST(
        test_display_name_validation_rejects_bad_bytes_and_missing_terminator);
    RUN_TEST(test_variant_color_time_and_position_values_are_bounded);
    RUN_TEST(
        test_move_wire_roundtrip_preserves_sequence_clock_hashes_and_flags);
    RUN_TEST(test_move_roundtrip_keeps_each_flag_independent);
    RUN_TEST(
        test_position_epoch_classifies_match_and_same_epoch_hash_mismatch);
    RUN_TEST(
        test_position_epoch_classifies_one_ahead_behind_and_forward_jump);
    RUN_TEST(test_position_epoch_classification_is_wrap_safe);
    RUN_TEST(
        test_move_clock_value_is_canonical_for_timed_and_untimed_games);
    return UNITY_END();
}
