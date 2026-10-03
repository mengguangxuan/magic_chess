#pragma once

#include <stdint.h>

#include "chess_types.h"

enum EngineApplyStatus : uint8_t {
  EngineMoveApplied,
  EngineMoveGuessed,
  EngineMoveIllegal,
  EngineMoveUnavailable
};

enum EngineGameState : uint8_t {
  EngineGameOngoing,
  EngineGameCheck,
  EngineGameCheckmate,
  EngineGameStalemate,
  EngineGameDrawRepetition,
  EngineGameDrawFiftyMove
};

struct EngineApplyResult {
  EngineApplyStatus status;
  ChessMoveInfo move;
  uint8_t candidateCount;
  int score;
};

struct EngineAnalysis {
  bool valid;
  bool hasBestMove;
  int score;
  ChessMoveInfo bestMove;
};

void engineBegin();
void engineNewGame();

EngineApplyResult engineApplyVoiceMove(PieceType piece, uint8_t targetFile,
                                       uint8_t targetRank);
uint8_t engineCountVoiceMoveCandidates(PieceType piece, uint8_t targetFile,
                                       uint8_t targetRank);
EngineApplyResult engineApplyVoiceMoveFrom(PieceType piece, uint8_t sourceFile,
                                           uint8_t sourceRank, uint8_t targetFile,
                                           uint8_t targetRank);
EngineApplyResult engineApplyCastle(bool kingSide);

bool engineChooseMove(ChessMoveInfo* result);
bool engineAnalyzeCurrentPosition(EngineAnalysis* result);
bool engineUndoOne();
bool engineWhiteToMove();
int engineHistoryPly();
EngineGameState engineGetGameState();

void engineMoveToString(const ChessMoveInfo& move, char output[6]);
