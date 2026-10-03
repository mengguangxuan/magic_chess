#pragma once

#include <stdint.h>

enum PieceType : uint8_t {
  King,
  Queen,
  Rook,
  Bishop,
  Knight,
  Pawn,
  DefaultPiece
};

enum VoiceCommandType : uint8_t {
  MoveToSquare,
  CastleKingSide,
  CastleQueenSide,
  BeginCorrectionWait,
  CorrectSourceSquare,
  CorrectionWaitTimedOut,
  UndoTurn,
  StartNewGame,
  EnableEngineOpponent,
  ChoosePlayerWhite,
  ChoosePlayerBlack
};

struct VoiceCommand {
  VoiceCommandType command;
  PieceType piece;
  uint8_t file;  // 1..8 means a..h
  uint8_t rank;  // 1..8
};

enum ChessMoveFlags : uint8_t {
  MoveFlagNone = 0,
  MoveFlagCapture = 1 << 0,
  MoveFlagCastle = 1 << 1,
  MoveFlagEnPassant = 1 << 2,
  MoveFlagPromotion = 1 << 3
};

struct ChessMoveInfo {
  uint8_t fromFile;
  uint8_t fromRank;
  uint8_t toFile;
  uint8_t toRank;
  PieceType promotion;
  uint8_t flags;
  PieceType capturedPiece;  // DefaultPiece for non-captures.
};
