#include <unity.h>

#include "chess_storage_codec.h"
#include "chess960.h"
#include "profile_storage_codec.h"

#include <cstdint>
#include <cstring>

void setUp() {}
void tearDown() {}

namespace {

GameSummary makeSummary(uint32_t gameId = 0x12345678u) {
    GameSummary summary;
    summary.gameId = gameId;
    summary.whiteProfileId = 101;
    std::strncpy(summary.whiteName, "Alice", sizeof(summary.whiteName));
    std::strncpy(summary.blackName, "Guest", sizeof(summary.blackName));
    summary.mode = GameMode::Local;
    summary.variant = ChessVariant::Standard;
    summary.positionIndex = 518;
    summary.timeControl = TimeControl::Blitz3;
    summary.localColor = PieceColor::White;
    summary.outcome = GameOutcome::WhiteWin;
    summary.termination = TerminationReason::Checkmate;
    summary.plyCount = 17;
    summary.whiteRemainingMs = 120500;
    summary.blackRemainingMs = 110250;
    return summary;
}

ActiveGameParticipants makeParticipants(uint32_t gameId = 0x12345678u) {
    ActiveGameParticipants participants;
    participants.valid = true;
    participants.gameId = gameId;
    participants.mode = GameMode::Local;
    participants.whiteProfileId = 101;
    std::strncpy(participants.whiteName, "Alice",
                 sizeof(participants.whiteName));
    std::strncpy(participants.blackName, "Guest",
                 sizeof(participants.blackName));
    return participants;
}

ChessStorageCodec::SaveMetadata makeMetadata(
        const ChessBoard& board, uint32_t gameId = 0x12345678u) {
    ChessStorageCodec::SaveMetadata metadata;
    metadata.variant = board.variant();
    metadata.positionIndex = board.positionIndex();
    metadata.aiDifficulty = AIDifficulty::None;
    metadata.aiColor = PieceColor::Black;
    metadata.localColor = PieceColor::White;
    metadata.timeControl = TimeControl::Blitz3;
    metadata.timeWhiteMs = 180000;
    metadata.timeBlackMs = 180000;
    metadata.timerRunning = true;
    metadata.participants = makeParticipants(gameId);
    return metadata;
}

uint32_t testCrc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u &
                  static_cast<uint32_t>(-static_cast<int32_t>(crc & 1u)));
        }
    }
    return ~crc;
}

void writeU16(uint8_t* destination, uint16_t value) {
    destination[0] = static_cast<uint8_t>(value);
    destination[1] = static_cast<uint8_t>(value >> 8);
}

void writeU32(uint8_t* destination, uint32_t value) {
    destination[0] = static_cast<uint8_t>(value);
    destination[1] = static_cast<uint8_t>(value >> 8);
    destination[2] = static_cast<uint8_t>(value >> 16);
    destination[3] = static_cast<uint8_t>(value >> 24);
}

void refreshV6Crc(uint8_t* buffer, size_t size) {
    writeU32(buffer + size - 4, testCrc32(buffer, size - 4));
}

void assertBoardsEqual(const ChessBoard& expected,
                       const ChessBoard& actual) {
    for (uint8_t row = 0; row < 8; ++row) {
        for (uint8_t column = 0; column < 8; ++column) {
            TEST_ASSERT_TRUE(expected.at(column, row) ==
                             actual.at(column, row));
        }
    }
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected.sideToMove()),
                            static_cast<uint8_t>(actual.sideToMove()));
    TEST_ASSERT_EQUAL_UINT8(expected.castleRights(), actual.castleRights());
    TEST_ASSERT_TRUE(expected.enPassantTarget() == actual.enPassantTarget());
    TEST_ASSERT_EQUAL_UINT16(expected.fullmoveNumber(),
                             actual.fullmoveNumber());
}

void test_profile_codec_round_trip_and_exact_summary() {
    ProfileData profiles;
    GameRecords::initialize(profiles, 101, "Alice");
    const GameSummary summary = makeSummary();
    TEST_ASSERT_TRUE(GameRecords::recordCompletedGame(profiles, summary));

    uint8_t encoded[ProfileStorageCodec::PROFILE_ENCODED_BYTES];
    TEST_ASSERT_TRUE(ProfileStorageCodec::encodeProfiles(
        profiles, encoded, sizeof(encoded)));
    ProfileData decoded;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ProfileStorageCodec::DecodeStatus::Ok),
        static_cast<uint8_t>(ProfileStorageCodec::decodeProfiles(
            encoded, sizeof(encoded), decoded)));
    TEST_ASSERT_EQUAL_UINT8(1, decoded.historyCount);
    TEST_ASSERT_TRUE(ProfileStorageCodec::summariesEqual(
        summary, decoded.history[0]));
    TEST_ASSERT_EQUAL_UINT32(1, decoded.profiles[0].games);
    TEST_ASSERT_EQUAL_UINT32(1, decoded.profiles[0].wins);

    GameSummary conflict = summary;
    conflict.plyCount++;
    TEST_ASSERT_FALSE(ProfileStorageCodec::summariesEqual(summary, conflict));

    conflict = summary;
    conflict.whiteName[6] = 'X'; // Hidden byte after "Alice\0" is significant.
    TEST_ASSERT_FALSE(ProfileStorageCodec::summariesEqual(summary, conflict));
    uint8_t pending[ProfileStorageCodec::PENDING_RESULT_ENCODED_BYTES];
    TEST_ASSERT_FALSE(ProfileStorageCodec::encodePendingResult(
        conflict, pending, sizeof(pending)));
}

void test_profile_decode_preserves_output_on_corrupt_and_future_data() {
    ProfileData profiles;
    GameRecords::initialize(profiles, 101, "Alice");
    uint8_t encoded[ProfileStorageCodec::PROFILE_ENCODED_BYTES];
    TEST_ASSERT_TRUE(ProfileStorageCodec::encodeProfiles(
        profiles, encoded, sizeof(encoded)));

    ProfileData output;
    GameRecords::initialize(output, 999, "Keep");
    encoded[20] ^= 0x80;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ProfileStorageCodec::DecodeStatus::Corrupt),
        static_cast<uint8_t>(ProfileStorageCodec::decodeProfiles(
            encoded, sizeof(encoded), output)));
    TEST_ASSERT_EQUAL_UINT32(999, output.profiles[0].id);

    TEST_ASSERT_TRUE(ProfileStorageCodec::encodeProfiles(
        profiles, encoded, sizeof(encoded)));
    encoded[4] = 2;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            ProfileStorageCodec::DecodeStatus::UnsupportedVersion),
        static_cast<uint8_t>(ProfileStorageCodec::decodeProfiles(
            encoded, sizeof(encoded), output)));
    TEST_ASSERT_EQUAL_UINT32(999, output.profiles[0].id);
}

void test_pending_result_codec_round_trip_crc_and_validation() {
    const GameSummary summary = makeSummary();
    uint8_t encoded[ProfileStorageCodec::PENDING_RESULT_ENCODED_BYTES];
    TEST_ASSERT_TRUE(ProfileStorageCodec::encodePendingResult(
        summary, encoded, sizeof(encoded)));

    GameSummary decoded;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ProfileStorageCodec::DecodeStatus::Ok),
        static_cast<uint8_t>(ProfileStorageCodec::decodePendingResult(
            encoded, sizeof(encoded), decoded)));
    TEST_ASSERT_TRUE(ProfileStorageCodec::summariesEqual(summary, decoded));

    encoded[12] ^= 1;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ProfileStorageCodec::DecodeStatus::Corrupt),
        static_cast<uint8_t>(ProfileStorageCodec::decodePendingResult(
            encoded, sizeof(encoded), decoded)));

    GameSummary incomplete = summary;
    incomplete.outcome = GameOutcome::Incomplete;
    TEST_ASSERT_FALSE(ProfileStorageCodec::encodePendingResult(
        incomplete, encoded, sizeof(encoded)));
}

void test_crc_valid_nonterminated_fixed_names_are_rejected() {
    ProfileData profiles;
    GameRecords::initialize(profiles, 101, "Alice");
    uint8_t profileBytes[ProfileStorageCodec::PROFILE_ENCODED_BYTES];
    TEST_ASSERT_TRUE(ProfileStorageCodec::encodeProfiles(
        profiles, profileBytes, sizeof(profileBytes)));
    // Header is 8 bytes; first profile is id[4] + name[13].
    profileBytes[8 + 4 + PLAYER_NAME_MAX] = 'X';
    writeU32(profileBytes + sizeof(profileBytes) - 4,
             testCrc32(profileBytes, sizeof(profileBytes) - 4));
    ProfileData decodedProfiles;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ProfileStorageCodec::DecodeStatus::Corrupt),
        static_cast<uint8_t>(ProfileStorageCodec::decodeProfiles(
            profileBytes, sizeof(profileBytes), decodedProfiles)));

    const GameSummary summary = makeSummary();
    uint8_t pending[ProfileStorageCodec::PENDING_RESULT_ENCODED_BYTES];
    TEST_ASSERT_TRUE(ProfileStorageCodec::encodePendingResult(
        summary, pending, sizeof(pending)));
    // magic/version[5], ids[12], then whiteName[13].
    pending[5 + 12 + PLAYER_NAME_MAX] = 'X';
    writeU32(pending + sizeof(pending) - 4,
             testCrc32(pending, sizeof(pending) - 4));
    GameSummary decodedSummary;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ProfileStorageCodec::DecodeStatus::Corrupt),
        static_cast<uint8_t>(ProfileStorageCodec::decodePendingResult(
            pending, sizeof(pending), decodedSummary)));

    ChessBoard board;
    ChessStorageCodec::SaveMetadata metadata = makeMetadata(board);
    uint8_t game[ChessStorageCodec::MAX_ENCODED_BYTES];
    size_t gameSize = 0;
    TEST_ASSERT_TRUE(ChessStorageCodec::encode(
        board, nullptr, 0, metadata, game, sizeof(game), gameSize));
    game[105 + PLAYER_NAME_MAX] = 'X';
    refreshV6Crc(game, gameSize);
    MoveRecord history[ChessStorageCodec::MAX_SAVED_HISTORY];
    ChessBoard decodedBoard;
    uint8_t historyCount = 0;
    ChessStorageCodec::SaveMetadata decodedMetadata;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ChessStorageCodec::DecodeStatus::Corrupt),
        static_cast<uint8_t>(ChessStorageCodec::decode(
            game, gameSize, decodedBoard, history,
            ChessStorageCodec::MAX_SAVED_HISTORY,
            historyCount, decodedMetadata)));
}

void test_chess_v6_round_trip_embeds_participants() {
    ChessBoard board;
    Move move;
    move.from = makeSquare(4, 1);
    move.to = makeSquare(4, 3);
    MoveRecord history[1] = {board.makeMove(move)};
    ChessStorageCodec::SaveMetadata metadata = makeMetadata(board);

    uint8_t encoded[ChessStorageCodec::MAX_ENCODED_BYTES];
    size_t encodedSize = 0;
    TEST_ASSERT_TRUE(ChessStorageCodec::encode(
        board, history, 1, metadata, encoded, sizeof(encoded), encodedSize));
    TEST_ASSERT_EQUAL_UINT8(6, encoded[0]);

    ChessBoard decodedBoard;
    MoveRecord decodedHistory[ChessStorageCodec::MAX_SAVED_HISTORY];
    uint8_t decodedCount = 0;
    ChessStorageCodec::SaveMetadata decodedMetadata;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ChessStorageCodec::DecodeStatus::Ok),
        static_cast<uint8_t>(ChessStorageCodec::decode(
            encoded, encodedSize, decodedBoard, decodedHistory,
            ChessStorageCodec::MAX_SAVED_HISTORY, decodedCount,
            decodedMetadata)));
    TEST_ASSERT_EQUAL_UINT8(1, decodedCount);
    TEST_ASSERT_EQUAL_UINT8(6, decodedMetadata.sourceVersion);
    TEST_ASSERT_TRUE(decodedMetadata.participantsEmbedded);
    TEST_ASSERT_EQUAL_UINT32(metadata.participants.gameId,
                             decodedMetadata.participants.gameId);
    TEST_ASSERT_EQUAL_STRING("Alice",
                             decodedMetadata.participants.whiteName);
    assertBoardsEqual(board, decodedBoard);
    TEST_ASSERT_TRUE(history[0].move == decodedHistory[0].move);
}

void test_chess_v6_rejects_every_truncation_and_trailing_data() {
    ChessBoard board;
    ChessStorageCodec::SaveMetadata metadata = makeMetadata(board);
    uint8_t encoded[ChessStorageCodec::MAX_ENCODED_BYTES + 1];
    size_t encodedSize = 0;
    TEST_ASSERT_TRUE(ChessStorageCodec::encode(
        board, nullptr, 0, metadata, encoded, sizeof(encoded), encodedSize));

    MoveRecord history[ChessStorageCodec::MAX_SAVED_HISTORY];
    for (size_t size = 0; size < encodedSize; ++size) {
        ChessBoard output;
        output.setFullmoveNumber(77);
        uint8_t count = 99;
        ChessStorageCodec::SaveMetadata decoded;
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(ChessStorageCodec::DecodeStatus::Corrupt),
            static_cast<uint8_t>(ChessStorageCodec::decode(
                encoded, size, output, history,
                ChessStorageCodec::MAX_SAVED_HISTORY, count, decoded)));
        TEST_ASSERT_EQUAL_UINT16(77, output.fullmoveNumber());
        TEST_ASSERT_EQUAL_UINT8(99, count);
    }

    encoded[encodedSize] = 0;
    ChessBoard output;
    uint8_t count = 0;
    ChessStorageCodec::SaveMetadata decoded;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ChessStorageCodec::DecodeStatus::Corrupt),
        static_cast<uint8_t>(ChessStorageCodec::decode(
            encoded, encodedSize + 1, output, history,
            ChessStorageCodec::MAX_SAVED_HISTORY, count, decoded)));
}

void test_chess_v6_rejects_bad_crc_semantics_and_chess960_index() {
    ChessBoard board;
    ChessStorageCodec::SaveMetadata metadata = makeMetadata(board);
    uint8_t encoded[ChessStorageCodec::MAX_ENCODED_BYTES];
    size_t size = 0;
    TEST_ASSERT_TRUE(ChessStorageCodec::encode(
        board, nullptr, 0, metadata, encoded, sizeof(encoded), size));

    MoveRecord history[ChessStorageCodec::MAX_SAVED_HISTORY];
    ChessBoard output;
    uint8_t count = 0;
    ChessStorageCodec::SaveMetadata decoded;
    encoded[65] ^= 1;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ChessStorageCodec::DecodeStatus::Corrupt),
        static_cast<uint8_t>(ChessStorageCodec::decode(
            encoded, size, output, history,
            ChessStorageCodec::MAX_SAVED_HISTORY, count, decoded)));

    TEST_ASSERT_TRUE(ChessStorageCodec::encode(
        board, nullptr, 0, metadata, encoded, sizeof(encoded), size));
    encoded[78] = static_cast<uint8_t>(ChessVariant::Chess960);
    writeU16(encoded + 79, 960);
    refreshV6Crc(encoded, size);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ChessStorageCodec::DecodeStatus::Corrupt),
        static_cast<uint8_t>(ChessStorageCodec::decode(
            encoded, size, output, history,
            ChessStorageCodec::MAX_SAVED_HISTORY, count, decoded)));

    ChessBoard position959;
    position959.setVariant(ChessVariant::Chess960);
    position959.setPositionIndex(959);
    position959.reset();
    metadata = makeMetadata(position959);
    TEST_ASSERT_TRUE(ChessStorageCodec::encode(
        position959, nullptr, 0, metadata,
        encoded, sizeof(encoded), size));
}

void test_chess_v6_rejects_invalid_fields_and_records() {
    ChessBoard board;
    Move move;
    move.from = makeSquare(4, 1);
    move.to = makeSquare(4, 3);
    MoveRecord sourceHistory[1] = {board.makeMove(move)};
    ChessStorageCodec::SaveMetadata metadata = makeMetadata(board);

    uint8_t valid[ChessStorageCodec::MAX_ENCODED_BYTES];
    size_t size = 0;
    TEST_ASSERT_TRUE(ChessStorageCodec::encode(
        board, sourceHistory, 1, metadata, valid, sizeof(valid), size));

    struct Mutation { size_t offset; uint8_t value; };
    const Mutation mutations[] = {
        {1, 0x70},   // Piece type.
        {2, 0x22},   // Piece color.
        {65, 2},     // Side to move.
        {66, 0x10},  // Castle-rights mask.
        {67, 8},     // En-passant column.
        {73, 2},     // Header flags.
        {74, 4},     // AI difficulty.
        {75, 2},     // AI color.
        {76, 2},     // Local color.
        {77, 2},     // Board-flipped boolean.
        {78, 2},     // Variant.
        {81, 5},     // Time control.
        {90, 2},     // Timer boolean.
        {91, 0},     // Embedded-participant marker.
        {96, 2},     // Online games are not resumable.
        {111, 'X'},  // Noncanonical byte after white-name NUL.
        {131, 8},    // Move source column.
        {135, 1},    // Pawn is not a promotion choice.
        {136, 4},    // Unknown move flag.
        {137, 7},    // Captured-piece type.
        {139, 8},    // Captured-square column.
        {141, 0x10}, // Previous castle-rights mask.
        {144, 4},    // Malformed NO_SQUARE previous EP pair.
        {145, 0},    // Missing moved piece.
    };

    uint8_t mutated[ChessStorageCodec::MAX_ENCODED_BYTES];
    MoveRecord outputHistory[ChessStorageCodec::MAX_SAVED_HISTORY];
    for (const Mutation& mutation : mutations) {
        std::memcpy(mutated, valid, size);
        mutated[mutation.offset] = mutation.value;
        refreshV6Crc(mutated, size);
        ChessBoard output;
        output.setFullmoveNumber(77);
        uint8_t count = 99;
        ChessStorageCodec::SaveMetadata outputMetadata;
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(ChessStorageCodec::DecodeStatus::Corrupt),
            static_cast<uint8_t>(ChessStorageCodec::decode(
                mutated, size, output, outputHistory,
                ChessStorageCodec::MAX_SAVED_HISTORY,
                count, outputMetadata)));
        TEST_ASSERT_EQUAL_UINT16(77, output.fullmoveNumber());
        TEST_ASSERT_EQUAL_UINT8(99, count);
    }

    std::memcpy(mutated, valid, size);
    std::memset(mutated + 92, 0, 4); // gameId must be nonzero.
    refreshV6Crc(mutated, size);
    ChessBoard output;
    uint8_t count = 0;
    ChessStorageCodec::SaveMetadata outputMetadata;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ChessStorageCodec::DecodeStatus::Corrupt),
        static_cast<uint8_t>(ChessStorageCodec::decode(
            mutated, size, output, outputHistory,
            ChessStorageCodec::MAX_SAVED_HISTORY, count, outputMetadata)));
}

Move repeatingKnightMove(uint16_t ply) {
    Move move;
    switch (ply % 4) {
        case 0:
            move.from = makeSquare(6, 0);
            move.to = makeSquare(5, 2);
            break;
        case 1:
            move.from = makeSquare(6, 7);
            move.to = makeSquare(5, 5);
            break;
        case 2:
            move.from = makeSquare(5, 2);
            move.to = makeSquare(6, 0);
            break;
        default:
            move.from = makeSquare(5, 5);
            move.to = makeSquare(6, 7);
            break;
    }
    return move;
}

void test_overflow_requires_full_legal_prefix_and_later_final_board() {
    ChessBoard finalBoard;
    MoveRecord history[ChessStorageCodec::MAX_SAVED_HISTORY];
    for (uint16_t ply = 0; ply < ChessStorageCodec::MAX_SAVED_HISTORY;
         ++ply) {
        history[ply] = finalBoard.makeMove(repeatingKnightMove(ply));
    }
    finalBoard.makeMove(
        repeatingKnightMove(ChessStorageCodec::MAX_SAVED_HISTORY));
    ChessStorageCodec::SaveMetadata metadata = makeMetadata(finalBoard);
    metadata.historyOverflow = true;

    uint8_t encoded[ChessStorageCodec::MAX_ENCODED_BYTES];
    size_t size = 0;
    TEST_ASSERT_TRUE(ChessStorageCodec::encode(
        finalBoard, history, ChessStorageCodec::MAX_SAVED_HISTORY,
        metadata, encoded, sizeof(encoded), size));

    ChessBoard decoded;
    MoveRecord decodedHistory[ChessStorageCodec::MAX_SAVED_HISTORY];
    uint8_t decodedCount = 0;
    ChessStorageCodec::SaveMetadata decodedMetadata;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ChessStorageCodec::DecodeStatus::Ok),
        static_cast<uint8_t>(ChessStorageCodec::decode(
            encoded, size, decoded, decodedHistory,
            ChessStorageCodec::MAX_SAVED_HISTORY,
            decodedCount, decodedMetadata)));
    TEST_ASSERT_TRUE(decodedMetadata.historyOverflow);
    TEST_ASSERT_EQUAL_UINT8(ChessStorageCodec::MAX_SAVED_HISTORY,
                            decodedCount);
    assertBoardsEqual(finalBoard, decoded);

    TEST_ASSERT_FALSE(ChessStorageCodec::encode(
        finalBoard, history, ChessStorageCodec::MAX_SAVED_HISTORY - 1,
        metadata, encoded, sizeof(encoded), size));

    // A full prefix marked overflow must have at least one later final ply.
    ChessBoard prefixBoard;
    for (uint16_t ply = 0; ply < ChessStorageCodec::MAX_SAVED_HISTORY;
         ++ply) {
        prefixBoard.makeMove(repeatingKnightMove(ply));
    }
    metadata = makeMetadata(prefixBoard);
    metadata.historyOverflow = true;
    TEST_ASSERT_FALSE(ChessStorageCodec::encode(
        prefixBoard, history, ChessStorageCodec::MAX_SAVED_HISTORY,
        metadata, encoded, sizeof(encoded), size));

    ChessBoard shortBoard;
    for (uint16_t ply = 0;
         ply < ChessStorageCodec::MAX_SAVED_HISTORY - 1; ++ply) {
        shortBoard.makeMove(repeatingKnightMove(ply));
    }
    metadata = makeMetadata(shortBoard);
    TEST_ASSERT_TRUE(ChessStorageCodec::encode(
        shortBoard, history, ChessStorageCodec::MAX_SAVED_HISTORY - 1,
        metadata, encoded, sizeof(encoded), size));
    encoded[73] = 1;
    refreshV6Crc(encoded, size);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ChessStorageCodec::DecodeStatus::Corrupt),
        static_cast<uint8_t>(ChessStorageCodec::decode(
            encoded, size, decoded, decodedHistory,
            ChessStorageCodec::MAX_SAVED_HISTORY,
            decodedCount, decodedMetadata)));
}

void encodeLegacyBoardHeader(uint8_t* buffer, const ChessBoard& board) {
    for (uint8_t row = 0; row < 8; ++row) {
        for (uint8_t column = 0; column < 8; ++column) {
            const Piece piece = board.at(column, row);
            buffer[1 + row * 8 + column] =
                (static_cast<uint8_t>(piece.type) << 4) |
                static_cast<uint8_t>(piece.color);
        }
    }
    buffer[65] = static_cast<uint8_t>(board.sideToMove());
    buffer[66] = board.castleRights();
    buffer[67] = board.enPassantTarget().col;
    buffer[68] = board.enPassantTarget().row;
    buffer[69] = board.halfmoveClock();
    writeU16(buffer + 70, board.fullmoveNumber());
    buffer[73] = 0;
    buffer[74] = 0;
    buffer[75] = 1;
    buffer[76] = 0;
    buffer[77] = 0;
}

void encodeLegacyRecordV2Plus(uint8_t* record,
                              const MoveRecord& original) {
    record[0] = original.move.from.col;
    record[1] = original.move.from.row;
    record[2] = original.move.to.col;
    record[3] = original.move.to.row;
    record[4] = static_cast<uint8_t>(original.move.promotion);
    record[5] = (original.move.isCastle ? 1 : 0) |
                (original.move.isEnPassant ? 2 : 0);
    record[6] = static_cast<uint8_t>(original.captured.type);
    record[7] = static_cast<uint8_t>(original.captured.color);
    record[8] = original.capturedSquare.col;
    record[9] = original.capturedSquare.row;
    record[10] = original.prevCastleRights;
    record[11] = original.prevHalfmoveClock;
    record[12] = original.prevEnPassantTarget.col;
    record[13] = original.prevEnPassantTarget.row;
    record[14] = static_cast<uint8_t>(original.movedPiece.type);
    record[15] = static_cast<uint8_t>(original.movedPiece.color);
}

void encodeLegacyRecordV1(uint8_t* record, const MoveRecord& original) {
    record[0] = original.move.from.col;
    record[1] = original.move.from.row;
    record[2] = original.move.to.col;
    record[3] = original.move.to.row;
    record[4] = static_cast<uint8_t>(original.move.promotion);
    record[5] = (original.move.isCastle ? 1 : 0) |
                (original.move.isEnPassant ? 2 : 0);
    record[6] = static_cast<uint8_t>(original.captured.type);
    record[7] = static_cast<uint8_t>(original.captured.color);
    record[8] = original.capturedSquare.col;
    record[9] = original.capturedSquare.row;
    record[10] = original.prevCastleRights;
    record[11] = original.prevHalfmoveClock;
    record[12] = original.prevEnPassantTarget.col;
    record[13] = original.prevEnPassantTarget.row;
}

size_t makeLegacyV2ToV5(uint8_t version, const ChessBoard& board,
                        const MoveRecord& original,
                        uint8_t* bytes, size_t capacity) {
    const size_t headerSize = version == 2 ? 79 : 91;
    const size_t recordSize = version <= 3 ? 33 : 16;
    const size_t checksumSize = version == 5 ? 1 : 0;
    const size_t size = headerSize + recordSize + checksumSize;
    if (capacity < size) return 0;
    std::memset(bytes, 0, size);
    bytes[0] = version;
    encodeLegacyBoardHeader(bytes, board);
    bytes[72] = 1;
    bytes[78] = 0; // Standard (all historical enum layouts).
    if (version >= 3) {
        writeU16(bytes + 79, 518);
        bytes[81] = static_cast<uint8_t>(TimeControl::Blitz3);
        writeU32(bytes + 82, 170000);
        writeU32(bytes + 86, 165000);
        bytes[90] = 1;
    }
    encodeLegacyRecordV2Plus(bytes + headerSize, original);
    if (version == 5) {
        uint8_t checksum = 0;
        for (size_t i = 0; i < size - 1; ++i) checksum ^= bytes[i];
        bytes[size - 1] = checksum;
    }
    return size;
}

void test_golden_v2_through_v5_records_decode_exactly() {
    ChessBoard finalBoard;
    Move move;
    move.from = makeSquare(4, 1);
    move.to = makeSquare(4, 3);
    const MoveRecord original = finalBoard.makeMove(move);

    uint8_t bytes[128];
    MoveRecord history[ChessStorageCodec::MAX_SAVED_HISTORY];
    for (uint8_t version = 2; version <= 5; ++version) {
        const size_t size = makeLegacyV2ToV5(
            version, finalBoard, original, bytes, sizeof(bytes));
        TEST_ASSERT_NOT_EQUAL(0, size);
        ChessBoard decoded;
        uint8_t count = 0;
        ChessStorageCodec::SaveMetadata metadata;
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(ChessStorageCodec::DecodeStatus::Ok),
            static_cast<uint8_t>(ChessStorageCodec::decode(
                bytes, size, decoded, history,
                ChessStorageCodec::MAX_SAVED_HISTORY, count, metadata)));
        TEST_ASSERT_EQUAL_UINT8(version, metadata.sourceVersion);
        TEST_ASSERT_FALSE(metadata.participantsEmbedded);
        TEST_ASSERT_EQUAL_UINT8(1, count);
        TEST_ASSERT_TRUE(history[0].move == original.move);
        assertBoardsEqual(finalBoard, decoded);

        // Exact-size validation rejects even a checksum-neutral trailing byte.
        bytes[size] = 0;
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(ChessStorageCodec::DecodeStatus::Corrupt),
            static_cast<uint8_t>(ChessStorageCodec::decode(
                bytes, size + 1, decoded, history,
                ChessStorageCodec::MAX_SAVED_HISTORY, count, metadata)));
    }
}

void test_v4_rejects_causally_inconsistent_history() {
    ChessBoard finalBoard;
    Move move;
    move.from = makeSquare(4, 1);
    move.to = makeSquare(4, 3);
    const MoveRecord original = finalBoard.makeMove(move);

    uint8_t bytes[128];
    size_t size = makeLegacyV2ToV5(
        4, finalBoard, original, bytes, sizeof(bytes));
    TEST_ASSERT_NOT_EQUAL(0, size);
    MoveRecord history[ChessStorageCodec::MAX_SAVED_HISTORY];
    ChessBoard output;
    uint8_t count = 0;
    ChessStorageCodec::SaveMetadata metadata;

    // d2-d4 is structurally legal, but it cannot produce the serialized e4
    // final board. v4 has no checksum, so only causal reconciliation catches
    // this mutation.
    bytes[91] = 3;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ChessStorageCodec::DecodeStatus::Corrupt),
        static_cast<uint8_t>(ChessStorageCodec::decode(
            bytes, size, output, history,
            ChessStorageCodec::MAX_SAVED_HISTORY, count, metadata)));

    size = makeLegacyV2ToV5(
        4, finalBoard, original, bytes, sizeof(bytes));
    bytes[91 + 6] = static_cast<uint8_t>(PieceType::Pawn);
    bytes[91 + 7] = static_cast<uint8_t>(PieceColor::Black);
    // Capture metadata is shape-valid but disagrees with generated e2-e4.
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ChessStorageCodec::DecodeStatus::Corrupt),
        static_cast<uint8_t>(ChessStorageCodec::decode(
            bytes, size, output, history,
            ChessStorageCodec::MAX_SAVED_HISTORY, count, metadata)));

    ChessBoard illegalCastleBoard;
    Move castle;
    castle.from = makeSquare(4, 0);
    castle.to = makeSquare(6, 0);
    castle.isCastle = true;
    const MoveRecord illegalCastle = illegalCastleBoard.makeMove(castle);
    size = makeLegacyV2ToV5(
        4, illegalCastleBoard, illegalCastle, bytes, sizeof(bytes));
    // The metadata is internally shaped like castling, but the move is not
    // legal from the initial position while f1/g1 remain occupied.
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ChessStorageCodec::DecodeStatus::Corrupt),
        static_cast<uint8_t>(ChessStorageCodec::decode(
            bytes, size, output, history,
            ChessStorageCodec::MAX_SAVED_HISTORY, count, metadata)));
}

void test_v1_castling_reconstructs_moved_king_for_review_undo() {
    ChessBoard finalBoard;
    Move moves[7];
    moves[0].from = makeSquare(6, 0); // Ng1-f3
    moves[0].to = makeSquare(5, 2);
    moves[1].from = makeSquare(0, 6); // ...a7-a6
    moves[1].to = makeSquare(0, 5);
    moves[2].from = makeSquare(6, 1); // g2-g3
    moves[2].to = makeSquare(6, 2);
    moves[3].from = makeSquare(0, 5); // ...a6-a5
    moves[3].to = makeSquare(0, 4);
    moves[4].from = makeSquare(5, 0); // Bf1-g2
    moves[4].to = makeSquare(6, 1);
    moves[5].from = makeSquare(0, 4); // ...a5-a4
    moves[5].to = makeSquare(0, 3);
    moves[6].from = makeSquare(4, 0); // O-O
    moves[6].to = makeSquare(6, 0);
    moves[6].isCastle = true;
    MoveRecord originals[7];
    for (uint8_t i = 0; i < 7; ++i) {
        originals[i] = finalBoard.makeMove(moves[i]);
    }

    constexpr size_t v1Size = 78 + 7 * 14;
    uint8_t bytes[v1Size] = {};
    bytes[0] = 1;
    encodeLegacyBoardHeader(bytes, finalBoard);
    bytes[72] = 7;
    for (uint8_t i = 0; i < 7; ++i) {
        encodeLegacyRecordV1(bytes + 78 + static_cast<size_t>(i) * 14,
                             originals[i]);
    }

    ChessBoard decoded;
    MoveRecord history[ChessStorageCodec::MAX_SAVED_HISTORY];
    uint8_t count = 0;
    ChessStorageCodec::SaveMetadata metadata;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ChessStorageCodec::DecodeStatus::Ok),
        static_cast<uint8_t>(ChessStorageCodec::decode(
            bytes, sizeof(bytes), decoded, history,
            ChessStorageCodec::MAX_SAVED_HISTORY, count, metadata)));
    TEST_ASSERT_EQUAL_UINT8(7, count);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PieceType::King),
        static_cast<uint8_t>(history[6].movedPiece.type));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PieceColor::White),
        static_cast<uint8_t>(history[6].movedPiece.color));

    decoded.unmakeMove(history[6]);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PieceType::King),
        static_cast<uint8_t>(decoded.at(4, 0).type));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PieceType::Rook),
        static_cast<uint8_t>(decoded.at(7, 0).type));
}

void test_v1_ordinary_non_capture_uses_destination_capture_square() {
    ChessBoard finalBoard;
    Move move;
    move.from = makeSquare(4, 1);
    move.to = makeSquare(4, 3);
    const MoveRecord original = finalBoard.makeMove(move);

    constexpr size_t v1Size = 78 + 14;
    uint8_t bytes[v1Size] = {};
    bytes[0] = 1;
    encodeLegacyBoardHeader(bytes, finalBoard);
    bytes[72] = 1;
    uint8_t* record = bytes + 78;
    record[0] = original.move.from.col;
    record[1] = original.move.from.row;
    record[2] = original.move.to.col;
    record[3] = original.move.to.row;
    record[4] = 0;
    record[5] = 0;
    record[6] = 0;
    record[7] = 0;
    record[8] = original.capturedSquare.col;
    record[9] = original.capturedSquare.row;
    record[10] = original.prevCastleRights;
    record[11] = original.prevHalfmoveClock;
    record[12] = original.prevEnPassantTarget.col;
    record[13] = original.prevEnPassantTarget.row;

    ChessBoard decoded;
    MoveRecord history[ChessStorageCodec::MAX_SAVED_HISTORY];
    uint8_t count = 0;
    ChessStorageCodec::SaveMetadata metadata;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ChessStorageCodec::DecodeStatus::Ok),
        static_cast<uint8_t>(ChessStorageCodec::decode(
            bytes, sizeof(bytes), decoded, history,
            ChessStorageCodec::MAX_SAVED_HISTORY, count, metadata)));
    TEST_ASSERT_EQUAL_UINT8(1, count);
    TEST_ASSERT_TRUE(history[0].capturedSquare == move.to);
    decoded.unmakeMove(history[0]);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PieceType::Pawn),
        static_cast<uint8_t>(decoded.at(4, 1).type));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PieceType::None),
        static_cast<uint8_t>(decoded.at(4, 3).type));
}

void test_future_chess_version_and_fuzz_lengths_are_safe() {
    uint8_t future[1] = {7};
    ChessBoard output;
    MoveRecord history[ChessStorageCodec::MAX_SAVED_HISTORY];
    uint8_t count = 42;
    ChessStorageCodec::SaveMetadata metadata;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            ChessStorageCodec::DecodeStatus::UnsupportedVersion),
        static_cast<uint8_t>(ChessStorageCodec::decode(
            future, sizeof(future), output, history,
            ChessStorageCodec::MAX_SAVED_HISTORY, count, metadata)));
    TEST_ASSERT_EQUAL_UINT8(42, count);

    uint32_t state = 0xC001D00Du;
    uint8_t fuzz[256];
    for (uint16_t iteration = 0; iteration < 512; ++iteration) {
        const size_t size = iteration % sizeof(fuzz);
        for (size_t i = 0; i < size; ++i) {
            state = state * 1664525u + 1013904223u;
            fuzz[i] = static_cast<uint8_t>(state >> 24);
        }
        uint8_t fuzzCount = 77;
        ChessStorageCodec::SaveMetadata fuzzMetadata;
        (void)ChessStorageCodec::decode(
            fuzz, size, output, history,
            ChessStorageCodec::MAX_SAVED_HISTORY,
            fuzzCount, fuzzMetadata);
    }
}

void test_chess960_generator_guards_invalid_index() {
    const Chess960Position fallback = chess960Generate(518);
    const Chess960Position invalid = chess960Generate(65535);
    TEST_ASSERT_EQUAL_UINT8(fallback.kingCol, invalid.kingCol);
    TEST_ASSERT_EQUAL_UINT8(fallback.rookKS, invalid.rookKS);
    TEST_ASSERT_EQUAL_UINT8(fallback.rookQS, invalid.rookQS);
    for (uint8_t i = 0; i < 8; ++i) {
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(fallback.backRank[i]),
            static_cast<uint8_t>(invalid.backRank[i]));
    }
}

} // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_profile_codec_round_trip_and_exact_summary);
    RUN_TEST(test_profile_decode_preserves_output_on_corrupt_and_future_data);
    RUN_TEST(test_pending_result_codec_round_trip_crc_and_validation);
    RUN_TEST(test_crc_valid_nonterminated_fixed_names_are_rejected);
    RUN_TEST(test_chess_v6_round_trip_embeds_participants);
    RUN_TEST(test_chess_v6_rejects_every_truncation_and_trailing_data);
    RUN_TEST(test_chess_v6_rejects_bad_crc_semantics_and_chess960_index);
    RUN_TEST(test_chess_v6_rejects_invalid_fields_and_records);
    RUN_TEST(test_overflow_requires_full_legal_prefix_and_later_final_board);
    RUN_TEST(test_golden_v2_through_v5_records_decode_exactly);
    RUN_TEST(test_v4_rejects_causally_inconsistent_history);
    RUN_TEST(test_v1_castling_reconstructs_moved_king_for_review_undo);
    RUN_TEST(test_v1_ordinary_non_capture_uses_destination_capture_square);
    RUN_TEST(test_future_chess_version_and_fuzz_lengths_are_safe);
    RUN_TEST(test_chess960_generator_guards_invalid_index);
    return UNITY_END();
}
