#pragma once

#include <stdint.h>

#include "engine_adapter.h"

enum MoveQuality : uint8_t {
  MoveQualitySound,
  MoveQualityInaccuracy,
  MoveQualityMistake,
  MoveQualityBlunder,
  MoveQualityDisaster
};

enum BlunderKind : uint8_t {
  BlunderNone,
  BlunderGeneric,
  BlunderAllowedMate,
  BlunderMissedMate,
  BlunderHungQueen,
  BlunderHungRook,
  BlunderMissedQueenCapture,
  BlunderMissedRookCapture,
  BlunderLostMaterial
};

struct MoveQualityReport {
  MoveQuality quality;
  BlunderKind kind;
  int loss;
  int bestScore;
  int playedScore;
};

MoveQualityReport classifyMoveQuality(const EngineAnalysis& before,
                                      const EngineAnalysis& after,
                                      const ChessMoveInfo& playedMove,
                                      PieceType movedPiece);
const char* moveQualityName(MoveQuality quality);
const char* blunderKindName(BlunderKind kind);

