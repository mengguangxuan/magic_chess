#include "move_quality.h"

namespace {
const int INACCURACY_THRESHOLD = 50;
const int MISTAKE_THRESHOLD = 120;
const int BLUNDER_THRESHOLD = 250;
const int DISASTER_THRESHOLD = 500;
const int FORCED_MATE_SCORE = 9000;

bool capturesOnSquare(const EngineAnalysis& analysis,
                      uint8_t file, uint8_t rank, PieceType capturedPiece)
{
  return analysis.hasBestMove &&
         (analysis.bestMove.flags & MoveFlagCapture) &&
         analysis.bestMove.toFile == file &&
         analysis.bestMove.toRank == rank &&
         analysis.bestMove.capturedPiece == capturedPiece;
}
}

MoveQualityReport classifyMoveQuality(const EngineAnalysis& before,
                                      const EngineAnalysis& after,
                                      const ChessMoveInfo& playedMove,
                                      PieceType movedPiece)
{
  MoveQualityReport report = {};
  report.quality = MoveQualitySound;
  report.kind = BlunderNone;
  report.bestScore = before.score;
  // The move changes the side to move; negate the opponent-relative score to
  // express both values from the original player's point of view.
  report.playedScore = -after.score;
  report.loss = report.bestScore - report.playedScore;
  if (report.loss < 0) report.loss = 0;

  if (report.loss >= DISASTER_THRESHOLD) {
    report.quality = MoveQualityDisaster;
  } else if (report.loss >= BLUNDER_THRESHOLD) {
    report.quality = MoveQualityBlunder;
  } else if (report.loss >= MISTAKE_THRESHOLD) {
    report.quality = MoveQualityMistake;
  } else if (report.loss >= INACCURACY_THRESHOLD) {
    report.quality = MoveQualityInaccuracy;
  }

  const bool allowedMate = after.score >= FORCED_MATE_SCORE;
  const bool missedMate = before.score >= FORCED_MATE_SCORE &&
                          report.playedScore < FORCED_MATE_SCORE;
  if (allowedMate) {
    report.kind = BlunderAllowedMate;
    if (report.quality < MoveQualityBlunder)
      report.quality = MoveQualityBlunder;
  } else if (missedMate) {
    report.kind = BlunderMissedMate;
    if (report.quality < MoveQualityBlunder)
      report.quality = MoveQualityBlunder;
  } else if (report.quality != MoveQualitySound &&
             capturesOnSquare(after, playedMove.toFile, playedMove.toRank,
                              Queen) && movedPiece == Queen) {
    report.kind = BlunderHungQueen;
  } else if (report.quality != MoveQualitySound &&
             capturesOnSquare(after, playedMove.toFile, playedMove.toRank,
                              Rook) && movedPiece == Rook) {
    report.kind = BlunderHungRook;
  } else if (report.quality != MoveQualitySound && before.hasBestMove &&
             (before.bestMove.flags & MoveFlagCapture) &&
             before.bestMove.capturedPiece == Queen &&
             (!(playedMove.flags & MoveFlagCapture) ||
              playedMove.capturedPiece != Queen)) {
    report.kind = BlunderMissedQueenCapture;
  } else if (report.quality != MoveQualitySound && before.hasBestMove &&
             (before.bestMove.flags & MoveFlagCapture) &&
             before.bestMove.capturedPiece == Rook &&
             (!(playedMove.flags & MoveFlagCapture) ||
              playedMove.capturedPiece != Rook)) {
    report.kind = BlunderMissedRookCapture;
  } else if (report.quality >= MoveQualityBlunder && after.hasBestMove &&
             (after.bestMove.flags & MoveFlagCapture)) {
    report.kind = BlunderLostMaterial;
  } else if (report.quality != MoveQualitySound) {
    report.kind = BlunderGeneric;
  }

  return report;
}

const char* moveQualityName(MoveQuality quality)
{
  switch (quality) {
    case MoveQualitySound: return "sound";
    case MoveQualityInaccuracy: return "inaccuracy";
    case MoveQualityMistake: return "mistake";
    case MoveQualityBlunder: return "blunder";
    case MoveQualityDisaster: return "disaster";
    default: return "unknown";
  }
}

const char* blunderKindName(BlunderKind kind)
{
  switch (kind) {
    case BlunderNone: return "none";
    case BlunderGeneric: return "generic";
    case BlunderAllowedMate: return "allowed forced mate";
    case BlunderMissedMate: return "missed forced mate";
    case BlunderHungQueen: return "hung queen";
    case BlunderHungRook: return "hung rook";
    case BlunderMissedQueenCapture: return "missed queen capture";
    case BlunderMissedRookCapture: return "missed rook capture";
    case BlunderLostMaterial: return "lost material";
    default: return "unknown";
  }
}
