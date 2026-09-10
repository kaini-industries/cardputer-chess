#include "chess_storage_codec.h"

#include "chess_rules.h"

#include <cstring>
#include <new>

namespace ChessStorageCodec {
namespace {

static constexpr size_t HEADER_SIZE_V1 = 78;
static constexpr size_t HEADER_SIZE_V2 = 79;
static constexpr size_t HEADER_SIZE_V3 = 91;
static constexpr size_t HEADER_SIZE_V4_V5 = 91;
static constexpr size_t HEADER_SIZE_V6_V7 = 131;
static constexpr size_t RECORD_SIZE_V1 = 14;
static constexpr size_t RECORD_SIZE_V2_V3 = 33;
static constexpr size_t RECORD_SIZE_V4_V7 = 16;

uint32_t crc32(const uint8_t* data, size_t length) {
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

void writeU16LE(uint8_t* destination, uint16_t value) {
    destination[0] = static_cast<uint8_t>(value);
    destination[1] = static_cast<uint8_t>(value >> 8);
}

void writeU32LE(uint8_t* destination, uint32_t value) {
    destination[0] = static_cast<uint8_t>(value);
    destination[1] = static_cast<uint8_t>(value >> 8);
    destination[2] = static_cast<uint8_t>(value >> 16);
    destination[3] = static_cast<uint8_t>(value >> 24);
}

uint16_t readU16LE(const uint8_t* source) {
    return static_cast<uint16_t>(source[0]) |
           (static_cast<uint16_t>(source[1]) << 8);
}

uint32_t readU32LE(const uint8_t* source) {
    return static_cast<uint32_t>(source[0]) |
           (static_cast<uint32_t>(source[1]) << 8) |
           (static_cast<uint32_t>(source[2]) << 16) |
           (static_cast<uint32_t>(source[3]) << 24);
}

bool validName(const char name[PLAYER_NAME_MAX + 1]) {
    char clean[PLAYER_NAME_MAX + 1];
    return GameRecords::sanitizeName(name, clean) &&
           std::memcmp(name, clean, PLAYER_NAME_MAX + 1) == 0;
}

bool validEnPassantSquare(const Square& square) {
    return (square.valid() && (square.row == 2 || square.row == 5)) ||
           (square.col == NO_SQUARE.col && square.row == NO_SQUARE.row);
}

bool validPiece(const Piece& piece, bool allowEmpty) {
    const uint8_t type = static_cast<uint8_t>(piece.type);
    const uint8_t color = static_cast<uint8_t>(piece.color);
    if (color > static_cast<uint8_t>(PieceColor::Black) ||
        type > static_cast<uint8_t>(PieceType::King)) {
        return false;
    }
    return allowEmpty || piece.type != PieceType::None;
}

bool validPromotion(PieceType promotion) {
    return promotion == PieceType::None || promotion == PieceType::Knight ||
           promotion == PieceType::Bishop || promotion == PieceType::Rook ||
           promotion == PieceType::Queen;
}

bool validParticipants(const ActiveGameParticipants& participants,
                       AIDifficulty difficulty) {
    if (!participants.valid || participants.gameId == 0 ||
        !validName(participants.whiteName) ||
        !validName(participants.blackName)) {
        return false;
    }
    const GameMode expectedMode = difficulty == AIDifficulty::None
        ? GameMode::Local : GameMode::AI;
    return participants.mode == expectedMode;
}

bool validBoard(const ChessBoard& board, const SaveMetadata& metadata) {
    if (board.variant() != metadata.variant ||
        board.positionIndex() != metadata.positionIndex ||
        static_cast<uint8_t>(metadata.variant) > 1 ||
        (metadata.variant == ChessVariant::Standard
             ? metadata.positionIndex != 518
             : metadata.positionIndex >= 960) ||
        static_cast<uint8_t>(board.sideToMove()) > 1 ||
        (board.castleRights() & ~CastleRights::All) != 0 ||
        !validEnPassantSquare(board.enPassantTarget()) ||
        board.fullmoveNumber() == 0) {
        return false;
    }
    uint8_t whiteKings = 0;
    uint8_t blackKings = 0;
    for (uint8_t row = 0; row < 8; ++row) {
        for (uint8_t column = 0; column < 8; ++column) {
            const Piece piece = board.at(column, row);
            if (!validPiece(piece, true)) return false;
            if (piece.type == PieceType::King) {
                if (piece.color == PieceColor::White) ++whiteKings;
                else ++blackKings;
            }
        }
    }
    return whiteKings == 1 && blackKings == 1;
}

bool validMetadata(const SaveMetadata& metadata) {
    return static_cast<uint8_t>(metadata.aiDifficulty) <=
               static_cast<uint8_t>(AIDifficulty::Hard) &&
           static_cast<uint8_t>(metadata.aiColor) <= 1 &&
           static_cast<uint8_t>(metadata.localColor) <= 1 &&
           static_cast<uint8_t>(metadata.timeControl) <=
               static_cast<uint8_t>(TimeControl::Rapid10) &&
           validParticipants(metadata.participants, metadata.aiDifficulty);
}

bool validRecord(const MoveRecord& record, bool movedPieceStored,
                 uint8_t initialKingColumn) {
    const Move& move = record.move;
    if (!move.from.valid() || !move.to.valid() ||
        (!move.isCastle && move.from == move.to) ||
        !validPromotion(move.promotion) ||
        (move.isCastle && move.isEnPassant) ||
        !validPiece(record.captured, true) ||
        !record.capturedSquare.valid() ||
        (record.prevCastleRights & ~CastleRights::All) != 0 ||
        !validEnPassantSquare(record.prevEnPassantTarget)) {
        return false;
    }
    if (movedPieceStored && !validPiece(record.movedPiece, false)) {
        return false;
    }
    if (!record.captured.empty() && movedPieceStored &&
        record.captured.color == record.movedPiece.color) {
        return false;
    }
    if (move.promotion != PieceType::None &&
        ((movedPieceStored && record.movedPiece.type != PieceType::Pawn) ||
         (move.to.row != 0 && move.to.row != 7))) {
        return false;
    }
    if (move.isCastle) {
        if (!movedPieceStored || record.movedPiece.type != PieceType::King ||
            !record.captured.empty() || move.from.col != initialKingColumn ||
            move.from.row != move.to.row ||
            (move.from.row != 0 && move.from.row != 7) ||
            (move.to.col != 2 && move.to.col != 6) ||
            record.capturedSquare != move.to ||
            move.promotion != PieceType::None) {
            return false;
        }
    } else if (move.isEnPassant) {
        if ((movedPieceStored && record.movedPiece.type != PieceType::Pawn) ||
            record.captured.type != PieceType::Pawn ||
            record.capturedSquare.col != move.to.col ||
            record.capturedSquare.row != move.from.row ||
            move.promotion != PieceType::None) {
            return false;
        }
    } else if (record.capturedSquare != move.to) {
        return false;
    }
    return true;
}

bool recordsEqualForVersion(const MoveRecord& serialized,
                            const MoveRecord& generated,
                            uint8_t sourceVersion) {
    const bool compareMovedPiece = sourceVersion >= 2 ||
                                   serialized.move.isCastle;
    return serialized.move == generated.move &&
           (!compareMovedPiece ||
            serialized.movedPiece == generated.movedPiece) &&
           serialized.captured == generated.captured &&
           serialized.capturedSquare == generated.capturedSquare &&
           serialized.prevCastleRights == generated.prevCastleRights &&
           serialized.prevHalfmoveClock == generated.prevHalfmoveClock &&
           serialized.prevEnPassantTarget ==
               generated.prevEnPassantTarget;
}

bool boardsEqual(const ChessBoard& left, const ChessBoard& right) {
    if (left.variant() != right.variant() ||
        left.positionIndex() != right.positionIndex() ||
        left.sideToMove() != right.sideToMove() ||
        left.castleRights() != right.castleRights() ||
        left.enPassantTarget() != right.enPassantTarget() ||
        left.halfmoveClock() != right.halfmoveClock() ||
        left.fullmoveNumber() != right.fullmoveNumber()) {
        return false;
    }
    for (uint8_t row = 0; row < 8; ++row) {
        for (uint8_t column = 0; column < 8; ++column) {
            if (left.at(column, row) != right.at(column, row)) return false;
        }
    }
    return true;
}

bool validateHistoryCausally(const ChessBoard& finalBoard,
                             const MoveRecord* history,
                             uint8_t historyCount, bool historyOverflow,
                             ChessVariant variant, uint16_t positionIndex,
                             uint8_t sourceVersion,
                             MoveRecord* normalizedHistory = nullptr) {
    if ((historyCount != 0 && !history) ||
        (historyOverflow && sourceVersion < 7 &&
         historyCount != MAX_SAVED_HISTORY)) {
        return false;
    }

    ChessBoard replay;
    replay.setVariant(variant);
    replay.setPositionIndex(positionIndex);
    replay.reset();
    const uint32_t finalPly =
        (static_cast<uint32_t>(finalBoard.fullmoveNumber()) - 1u) * 2u +
        (finalBoard.sideToMove() == PieceColor::Black ? 1u : 0u);
    if (historyOverflow && sourceVersion >= 7) {
        // v7 retains the most recent plies. Legacy overflow saves contain a
        // prefix instead; they are validated separately below before migration.
        if (finalPly <= MAX_SAVED_HISTORY || finalPly <= historyCount) return false;
        replay = finalBoard;
        for (int i = historyCount - 1; i >= 0; --i) replay.unmakeMove(history[i]);
        SaveMetadata positionMetadata;
        positionMetadata.variant = variant;
        positionMetadata.positionIndex = positionIndex;
        if (!validBoard(replay, positionMetadata) ||
            ChessRules::isInCheck(replay, opponent(replay.sideToMove()))) return false;
    }
    for (uint8_t i = 0; i < historyCount; ++i) {
        MoveList legalMoves;
        ChessRules::generateLegal(replay, legalMoves);
        const Move* legalMove = nullptr;
        for (uint16_t candidate = 0; candidate < legalMoves.count;
             ++candidate) {
            if (legalMoves.moves[candidate] == history[i].move) {
                legalMove = &legalMoves.moves[candidate];
                break;
            }
        }
        if (!legalMove) return false;
        const MoveRecord generated = replay.makeMove(*legalMove);
        if (!recordsEqualForVersion(history[i], generated, sourceVersion)) {
            return false;
        }
        // v1 omitted movedPiece. Materialize the complete, validated record so
        // that undo, repetition, and migration can use it immediately.
        if (normalizedHistory) normalizedHistory[i] = generated;
    }

    if (!historyOverflow || sourceVersion >= 7) return boardsEqual(replay, finalBoard);

    // Legacy firmware retained the first 250 plies, then marked overflow on the next
    // legal move. Unknown suffix moves make full reconciliation impossible,
    // but the retained prefix must be legal and the final board must be later
    // than that prefix. Also reject positions where the player who just moved
    // illegally left their own king in check.
    return finalPly > historyCount &&
           !ChessRules::isInCheck(
               finalBoard, opponent(finalBoard.sideToMove()));
}

void packRecord(uint8_t* destination, const MoveRecord& record) {
    destination[0] = record.move.from.col;
    destination[1] = record.move.from.row;
    destination[2] = record.move.to.col;
    destination[3] = record.move.to.row;
    destination[4] = static_cast<uint8_t>(record.move.promotion);
    destination[5] = (record.move.isCastle ? 0x01 : 0) |
                     (record.move.isEnPassant ? 0x02 : 0);
    destination[6] = static_cast<uint8_t>(record.captured.type);
    destination[7] = static_cast<uint8_t>(record.captured.color);
    destination[8] = record.capturedSquare.col;
    destination[9] = record.capturedSquare.row;
    destination[10] = record.prevCastleRights;
    destination[11] = record.prevHalfmoveClock;
    destination[12] = record.prevEnPassantTarget.col;
    destination[13] = record.prevEnPassantTarget.row;
    destination[14] = static_cast<uint8_t>(record.movedPiece.type);
    destination[15] = static_cast<uint8_t>(record.movedPiece.color);
}

bool unpackRecord(const uint8_t* source, uint8_t version,
                  uint8_t initialKingColumn, MoveRecord& record) {
    const uint8_t flags = source[5];
    if ((flags & ~0x03u) != 0) return false;
    record = MoveRecord{};
    record.move.from = makeSquare(source[0], source[1]);
    record.move.to = makeSquare(source[2], source[3]);
    record.move.promotion = static_cast<PieceType>(source[4]);
    record.move.isCastle = (flags & 0x01) != 0;
    record.move.isEnPassant = (flags & 0x02) != 0;
    record.captured = Piece(static_cast<PieceType>(source[6]),
                            static_cast<PieceColor>(source[7]));
    record.capturedSquare = makeSquare(source[8], source[9]);
    record.prevCastleRights = source[10];
    record.prevHalfmoveClock = source[11];
    record.prevEnPassantTarget = makeSquare(source[12], source[13]);
    if (version >= 2) {
        record.movedPiece = Piece(static_cast<PieceType>(source[14]),
                                  static_cast<PieceColor>(source[15]));
    } else if (record.move.isCastle && record.move.from.col == 4 &&
               record.move.from.row == record.move.to.row &&
               (record.move.from.row == 0 || record.move.from.row == 7) &&
               (record.move.to.col == 2 || record.move.to.col == 6)) {
        // v1 omitted movedPiece. Castling needs this field during validation;
        // other records are completed by replay before returning decoded history.
        record.movedPiece = makePiece(
            PieceType::King,
            record.move.from.row == 0 ? PieceColor::White : PieceColor::Black);
    }
    return validRecord(record, version >= 2 || record.move.isCastle,
                       initialKingColumn);
}

bool versionLayout(uint8_t version, size_t& headerSize, size_t& recordSize,
                   size_t& checksumSize) {
    checksumSize = 0;
    switch (version) {
        case 1:
            headerSize = HEADER_SIZE_V1;
            recordSize = RECORD_SIZE_V1;
            return true;
        case 2:
            headerSize = HEADER_SIZE_V2;
            recordSize = RECORD_SIZE_V2_V3;
            return true;
        case 3:
            headerSize = HEADER_SIZE_V3;
            recordSize = RECORD_SIZE_V2_V3;
            return true;
        case 4:
            headerSize = HEADER_SIZE_V4_V5;
            recordSize = RECORD_SIZE_V4_V7;
            return true;
        case 5:
            headerSize = HEADER_SIZE_V4_V5;
            recordSize = RECORD_SIZE_V4_V7;
            checksumSize = 1;
            return true;
        case 6:
        case 7:
            headerSize = HEADER_SIZE_V6_V7;
            recordSize = RECORD_SIZE_V4_V7;
            checksumSize = 4;
            return true;
        default:
            return false;
    }
}

} // namespace

size_t encodedSize(uint8_t historyCount) {
    if (historyCount > MAX_SAVED_HISTORY) return 0;
    return HEADER_SIZE_V6_V7 + static_cast<size_t>(historyCount) *
           RECORD_SIZE_V4_V7 + 4;
}

bool encode(const ChessBoard& board,
            const MoveRecord* history, uint8_t historyCount,
            const SaveMetadata& metadata,
            uint8_t* buffer, size_t capacity, size_t& bytesWritten) {
    bytesWritten = 0;
    const size_t required = encodedSize(historyCount);
    if (!buffer || required == 0 || capacity < required ||
        (historyCount != 0 && !history) || !validMetadata(metadata) ||
        !validBoard(board, metadata)) {
        return false;
    }
    for (uint8_t i = 0; i < historyCount; ++i) {
        if (!validRecord(history[i], true, board.initKingCol())) return false;
    }
    if (!validateHistoryCausally(
            board, history, historyCount, metadata.historyOverflow,
            metadata.variant, metadata.positionIndex, CURRENT_VERSION)) {
        return false;
    }

    std::memset(buffer, 0, required);
    buffer[0] = CURRENT_VERSION;
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
    writeU16LE(buffer + 70, board.fullmoveNumber());
    buffer[72] = historyCount;
    buffer[73] = metadata.historyOverflow ? 0x01 : 0;
    buffer[74] = static_cast<uint8_t>(metadata.aiDifficulty);
    buffer[75] = static_cast<uint8_t>(metadata.aiColor);
    buffer[76] = static_cast<uint8_t>(metadata.localColor);
    buffer[77] = metadata.boardFlipped ? 1 : 0;
    buffer[78] = static_cast<uint8_t>(metadata.variant);
    writeU16LE(buffer + 79, metadata.positionIndex);
    buffer[81] = static_cast<uint8_t>(metadata.timeControl);
    writeU32LE(buffer + 82, metadata.timeWhiteMs);
    writeU32LE(buffer + 86, metadata.timeBlackMs);
    buffer[90] = metadata.timerRunning ? 1 : 0;

    buffer[91] = 1; // Embedded participants are mandatory since v6.
    writeU32LE(buffer + 92, metadata.participants.gameId);
    buffer[96] = static_cast<uint8_t>(metadata.participants.mode);
    writeU32LE(buffer + 97, metadata.participants.whiteProfileId);
    writeU32LE(buffer + 101, metadata.participants.blackProfileId);
    std::memcpy(buffer + 105, metadata.participants.whiteName,
                PLAYER_NAME_MAX + 1);
    std::memcpy(buffer + 118, metadata.participants.blackName,
                PLAYER_NAME_MAX + 1);

    for (uint8_t i = 0; i < historyCount; ++i) {
        packRecord(buffer + HEADER_SIZE_V6_V7 +
                   static_cast<size_t>(i) * RECORD_SIZE_V4_V7, history[i]);
    }
    writeU32LE(buffer + required - 4, crc32(buffer, required - 4));
    bytesWritten = required;
    return true;
}

DecodeStatus decode(const uint8_t* buffer, size_t size,
                    ChessBoard& board,
                    MoveRecord* history, size_t historyCapacity,
                    uint8_t& historyCount,
                    SaveMetadata& metadata) {
    if (!buffer || size == 0) return DecodeStatus::Corrupt;
    const uint8_t version = buffer[0];
    if (version > CURRENT_VERSION) return DecodeStatus::UnsupportedVersion;

    size_t headerSize = 0;
    size_t recordSize = 0;
    size_t checksumSize = 0;
    if (!versionLayout(version, headerSize, recordSize, checksumSize) ||
        size < headerSize + checksumSize) {
        return DecodeStatus::Corrupt;
    }

    const uint8_t decodedHistoryCount = buffer[72];
    if (decodedHistoryCount > MAX_SAVED_HISTORY ||
        decodedHistoryCount > historyCapacity ||
        (decodedHistoryCount != 0 && !history)) {
        return DecodeStatus::Corrupt;
    }
    const size_t expectedSize = headerSize +
        static_cast<size_t>(decodedHistoryCount) * recordSize + checksumSize;
    if (size != expectedSize) return DecodeStatus::Corrupt;

    if (version == 5) {
        uint8_t checksum = 0;
        for (size_t i = 0; i < size - 1; ++i) checksum ^= buffer[i];
        if (checksum != buffer[size - 1]) return DecodeStatus::Corrupt;
    } else if (version >= 6 &&
               readU32LE(buffer + size - 4) != crc32(buffer, size - 4)) {
        return DecodeStatus::Corrupt;
    }

    uint8_t rawVariant = 0;
    ChessVariant decodedVariant = ChessVariant::Standard;
    if (version >= 2) {
        rawVariant = buffer[78];
        if (version < 4 && rawVariant == 1) {
            // Removed Atomic variant.
            return DecodeStatus::UnsupportedVersion;
        }
        if (version < 4 && rawVariant == 2) {
            decodedVariant = ChessVariant::Chess960;
        } else if (rawVariant <= 1) {
            decodedVariant = static_cast<ChessVariant>(rawVariant);
        } else {
            return DecodeStatus::Corrupt;
        }
    }
    const uint16_t decodedPositionIndex = version >= 3
        ? readU16LE(buffer + 79) : 518;
    if ((decodedVariant == ChessVariant::Standard &&
         decodedPositionIndex != 518) ||
        (decodedVariant == ChessVariant::Chess960 &&
         decodedPositionIndex >= 960)) {
        return DecodeStatus::Corrupt;
    }
    if (buffer[65] > 1 || (buffer[66] & ~CastleRights::All) != 0 ||
        (buffer[73] & ~0x01u) != 0 || buffer[74] > 3 ||
        buffer[75] > 1 || buffer[76] > 1 || buffer[77] > 1 ||
        readU16LE(buffer + 70) == 0) {
        return DecodeStatus::Corrupt;
    }
    const Square enPassant = makeSquare(buffer[67], buffer[68]);
    if (!validEnPassantSquare(enPassant)) return DecodeStatus::Corrupt;

    SaveMetadata decodedMetadata;
    decodedMetadata.sourceVersion = version;
    decodedMetadata.historyOverflow = (buffer[73] & 0x01) != 0;
    decodedMetadata.aiDifficulty = static_cast<AIDifficulty>(buffer[74]);
    decodedMetadata.aiColor = static_cast<PieceColor>(buffer[75]);
    decodedMetadata.localColor = static_cast<PieceColor>(buffer[76]);
    decodedMetadata.boardFlipped = buffer[77] != 0;
    decodedMetadata.variant = decodedVariant;
    decodedMetadata.positionIndex = decodedPositionIndex;
    if (version >= 3) {
        if (buffer[81] > static_cast<uint8_t>(TimeControl::Rapid10) ||
            buffer[90] > 1) {
            return DecodeStatus::Corrupt;
        }
        decodedMetadata.timeControl = static_cast<TimeControl>(buffer[81]);
        decodedMetadata.timeWhiteMs = readU32LE(buffer + 82);
        decodedMetadata.timeBlackMs = readU32LE(buffer + 86);
        decodedMetadata.timerRunning = buffer[90] != 0;
    }

    if (version >= 6) {
        if (buffer[91] != 1 || buffer[96] >
                static_cast<uint8_t>(GameMode::AI)) {
            return DecodeStatus::Corrupt;
        }
        ActiveGameParticipants participants;
        participants.valid = true;
        participants.gameId = readU32LE(buffer + 92);
        participants.mode = static_cast<GameMode>(buffer[96]);
        participants.whiteProfileId = readU32LE(buffer + 97);
        participants.blackProfileId = readU32LE(buffer + 101);
        std::memcpy(participants.whiteName, buffer + 105,
                    PLAYER_NAME_MAX + 1);
        std::memcpy(participants.blackName, buffer + 118,
                    PLAYER_NAME_MAX + 1);
        if (participants.whiteName[PLAYER_NAME_MAX] != '\0' ||
            participants.blackName[PLAYER_NAME_MAX] != '\0') {
            return DecodeStatus::Corrupt;
        }
        participants.whiteName[PLAYER_NAME_MAX] = '\0';
        participants.blackName[PLAYER_NAME_MAX] = '\0';
        if (!validParticipants(participants,
                               decodedMetadata.aiDifficulty)) {
            return DecodeStatus::Corrupt;
        }
        decodedMetadata.participants = participants;
        decodedMetadata.participantsEmbedded = true;
    }

    ChessBoard decodedBoard;
    decodedBoard.setVariant(decodedVariant);
    decodedBoard.setPositionIndex(decodedPositionIndex);
    decodedBoard.reset();
    uint8_t whiteKings = 0;
    uint8_t blackKings = 0;
    for (uint8_t row = 0; row < 8; ++row) {
        for (uint8_t column = 0; column < 8; ++column) {
            const uint8_t packed = buffer[1 + row * 8 + column];
            const uint8_t rawType = packed >> 4;
            const uint8_t rawColor = packed & 0x0F;
            if (rawType > static_cast<uint8_t>(PieceType::King) ||
                rawColor > static_cast<uint8_t>(PieceColor::Black)) {
                return DecodeStatus::Corrupt;
            }
            const PieceType type = static_cast<PieceType>(rawType);
            const PieceColor color = static_cast<PieceColor>(rawColor);
            if (type == PieceType::King) {
                if (color == PieceColor::White) ++whiteKings;
                else ++blackKings;
            }
            decodedBoard.set(column, row,
                type == PieceType::None ? Piece{} : Piece(type, color));
        }
    }
    if (whiteKings != 1 || blackKings != 1) return DecodeStatus::Corrupt;
    decodedBoard.setSideToMove(static_cast<PieceColor>(buffer[65]));
    decodedBoard.setCastleRights(buffer[66]);
    decodedBoard.setEnPassantTarget(enPassant);
    decodedBoard.setHalfmoveClock(buffer[69]);
    decodedBoard.setFullmoveNumber(readU16LE(buffer + 70));

    MoveRecord* decodedHistory = nullptr;
    if (decodedHistoryCount != 0) {
        decodedHistory = new (std::nothrow) MoveRecord[decodedHistoryCount];
        if (!decodedHistory) return DecodeStatus::NoMemory;
    }
    for (uint8_t i = 0; i < decodedHistoryCount; ++i) {
        if (!unpackRecord(buffer + headerSize +
                          static_cast<size_t>(i) * recordSize,
                          version, decodedBoard.initKingCol(),
                          decodedHistory[i])) {
            delete[] decodedHistory;
            return DecodeStatus::Corrupt;
        }
    }
    if (!validateHistoryCausally(
            decodedBoard, decodedHistory, decodedHistoryCount,
            decodedMetadata.historyOverflow, decodedVariant,
            decodedPositionIndex, version, decodedHistory)) {
        delete[] decodedHistory;
        return DecodeStatus::Corrupt;
    }

    // The unknown suffix of old overflow saves cannot be recovered from a
    // retained prefix. Start tracking fresh at this board, never replay that
    // prefix backward against a different position.
    const uint8_t retainedCount = decodedMetadata.historyOverflow && version < 7
        ? 0 : decodedHistoryCount;
    board = decodedBoard;
    if (retainedCount != 0) {
        std::memcpy(history, decodedHistory,
                    static_cast<size_t>(retainedCount) *
                    sizeof(MoveRecord));
    }
    delete[] decodedHistory;
    historyCount = retainedCount;
    metadata = decodedMetadata;
    return DecodeStatus::Ok;
}

} // namespace ChessStorageCodec
