#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../engine_adapter.h"
#include "../move_quality.h"
#include "../opening_book_data.h"

extern "C" {
#include "../src/tscp/defs.h"
#include "../src/tscp/data.h"

void gen();
void set_hash();
int book_move();
}

static void expectMove(const ChessMoveInfo& move, const char* expected)
{
  char text[6] = {0};
  engineMoveToString(move, text);
  assert(strcmp(text, expected) == 0);
}

static void setAmbiguousRookPosition()
{
  for (int square = 0; square < 64; ++square) {
    color[square] = EMPTY;
    piece[square] = EMPTY;
  }

  // White: king e2, rooks a1/h1. Black: king e8. Both rooks can reach d1.
  color[52] = LIGHT;
  piece[52] = KING;
  color[56] = LIGHT;
  piece[56] = ROOK;
  color[63] = LIGHT;
  piece[63] = ROOK;
  color[4] = DARK;
  piece[4] = KING;

  side = LIGHT;
  xside = DARK;
  castle = 0;
  ep = -1;
  fifty = 0;
  ply = 0;
  hply = 0;
  first_move[0] = 0;
  set_hash();
  gen();
}

static void clearPosition()
{
  for (int square = 0; square < 64; ++square) {
    color[square] = EMPTY;
    piece[square] = EMPTY;
  }
  side = LIGHT;
  xside = DARK;
  castle = 0;
  ep = -1;
  fifty = 0;
  ply = 0;
  hply = 0;
  first_move[0] = 0;
}

static void finishPosition()
{
  set_hash();
  gen();
}

static void testSpecialMoveFlags()
{
  clearPosition();
  color[60] = LIGHT; piece[60] = KING;  // e1
  color[63] = LIGHT; piece[63] = ROOK;  // h1
  color[4] = DARK; piece[4] = KING;     // e8
  castle = 1;
  finishPosition();

  EngineApplyResult castleMove = engineApplyCastle(true);
  assert(castleMove.status == EngineMoveApplied);
  expectMove(castleMove.move, "e1g1");
  assert(castleMove.move.flags & MoveFlagCastle);

  clearPosition();
  color[60] = LIGHT; piece[60] = KING;  // e1
  color[4] = DARK; piece[4] = KING;     // e8
  color[28] = LIGHT; piece[28] = PAWN;  // e5
  color[27] = DARK; piece[27] = PAWN;   // d5
  ep = 19;                              // d6
  finishPosition();

  EngineApplyResult enPassantMove = engineApplyVoiceMove(Pawn, 4, 6);
  assert(enPassantMove.status == EngineMoveApplied);
  expectMove(enPassantMove.move, "e5d6");
  assert(enPassantMove.move.flags & MoveFlagCapture);
  assert(enPassantMove.move.flags & MoveFlagEnPassant);
  assert(enPassantMove.move.capturedPiece == Pawn);

  clearPosition();
  color[60] = LIGHT; piece[60] = KING;  // e1
  color[4] = DARK; piece[4] = KING;     // e8
  color[8] = LIGHT; piece[8] = PAWN;    // a7
  finishPosition();

  EngineApplyResult promotionMove = engineApplyVoiceMove(Pawn, 1, 8);
  assert(promotionMove.status == EngineMoveApplied);
  expectMove(promotionMove.move, "a7a8q");
  assert(promotionMove.move.flags & MoveFlagPromotion);
  assert(promotionMove.move.promotion == Queen);
}

static void testCapturedPiece()
{
  const PieceType types[] = {Pawn, Knight, Bishop, Rook, Queen};
  for (int captured = PAWN; captured <= QUEEN; ++captured) {
    clearPosition();
    color[60] = LIGHT; piece[60] = KING;
    color[4] = DARK; piece[4] = KING;
    color[56] = LIGHT; piece[56] = ROOK;  // a1 captures a4
    color[32] = DARK; piece[32] = captured;
    finishPosition();
    EngineApplyResult result = engineApplyVoiceMoveFrom(Rook, 1, 1, 1, 4);
    assert(result.status == EngineMoveApplied);
    assert(result.move.flags & MoveFlagCapture);
    assert(result.move.capturedPiece == types[captured]);
    assert(piece[32] == ROOK); // Recorded type survives destination overwrite.
    assert(engineUndoOne());
    assert(piece[32] == captured);
  }
}

static void testFlashBook()
{
  engineNewGame();
  assert(book_move() != -1);
  assert(engineHistoryPly() == 0);
  assert(engineWhiteToMove());
  EngineApplyResult outside = engineApplyVoiceMove(Pawn, 1, 3);
  assert(outside.status == EngineMoveApplied);
  assert(book_move() == -1);
  ChessMoveInfo searched = {};
  assert(engineChooseMove(&searched));
  assert(engineUndoOne());
  assert(engineUndoOne());
  assert(book_move() != -1);

  // Replay every shipped book line. Every prefix must offer a legal book move.
  unsigned lines = 0;
  for (const char* cursor = openingBook; *cursor;) {
    if (*cursor == '\n' || *cursor == '\r') { ++cursor; continue; }
    engineNewGame();
    while (*cursor && *cursor != '\n' && *cursor != '\r') {
      assert(strlen(cursor) >= 4);
      assert(book_move() != -1);
      const int from = (8 - (cursor[1] - '0')) * 8 + cursor[0] - 'a';
      const PieceType types[] = {Pawn, Knight, Bishop, Rook, Queen, King};
      assert(piece[from] >= PAWN && piece[from] <= KING);
      EngineApplyResult applied = engineApplyVoiceMoveFrom(
          types[piece[from]], cursor[0] - 'a' + 1, cursor[1] - '0',
          cursor[2] - 'a' + 1, cursor[3] - '0');
      assert(applied.status == EngineMoveApplied);
      cursor += 4;
      if (*cursor == ' ') ++cursor;
    }
    ++lines;
  }
  printf("Flash book: %u lines validated, %zu bytes.\n", lines, sizeof(openingBook));
}

static void testMoveQualityClassifier()
{
  EngineAnalysis before = {};
  before.valid = true;
  before.hasBestMove = true;
  before.score = 300;
  before.bestMove = {4, 1, 4, 8, DefaultPiece, MoveFlagCapture, Queen};

  EngineAnalysis after = {};
  after.valid = true;
  after.hasBestMove = true;
  after.score = 50;
  after.bestMove = {1, 8, 1, 1, DefaultPiece, MoveFlagCapture, Rook};

  ChessMoveInfo played = {1, 1, 1, 3, DefaultPiece, MoveFlagNone,
                          DefaultPiece};
  MoveQualityReport missedQueen = classifyMoveQuality(
      before, after, played, Rook);
  assert(missedQueen.loss == 350);
  assert(missedQueen.quality == MoveQualityBlunder);
  assert(missedQueen.kind == BlunderMissedQueenCapture);

  before.score = 100;
  after.score = 500;
  after.bestMove = {2, 8, 4, 4, DefaultPiece, MoveFlagCapture, Queen};
  played = {4, 1, 4, 4, DefaultPiece, MoveFlagNone, DefaultPiece};
  MoveQualityReport hungQueen = classifyMoveQuality(
      before, after, played, Queen);
  assert(hungQueen.loss == 600);
  assert(hungQueen.quality == MoveQualityDisaster);
  assert(hungQueen.kind == BlunderHungQueen);

  before.score = 100;
  after.score = 9500;
  MoveQualityReport allowedMate = classifyMoveQuality(
      before, after, played, Queen);
  assert(allowedMate.kind == BlunderAllowedMate);
  assert(allowedMate.quality >= MoveQualityBlunder);

  before.score = 9500;
  after.score = -200;
  MoveQualityReport missedMate = classifyMoveQuality(
      before, after, played, Queen);
  assert(missedMate.kind == BlunderMissedMate);
  assert(missedMate.quality >= MoveQualityBlunder);
}

int main()
{
  engineBegin();
  assert(engineHistoryPly() == 0);
  assert(engineWhiteToMove());
  assert(engineCountVoiceMoveCandidates(Pawn, 5, 4) == 1);
  assert(engineCountVoiceMoveCandidates(Pawn, 5, 5) == 0);

  EngineAnalysis openingAnalysis = {};
  assert(engineAnalyzeCurrentPosition(&openingAnalysis));
  assert(openingAnalysis.valid);
  assert(openingAnalysis.hasBestMove);
  assert(engineHistoryPly() == 0);
  assert(engineWhiteToMove());

  EngineApplyResult e4 = engineApplyVoiceMove(Pawn, 5, 4);
  assert(e4.status == EngineMoveApplied);
  expectMove(e4.move, "e2e4");
  assert(engineHistoryPly() == 1);
  assert(!engineWhiteToMove());

  ChessMoveInfo reply = {};
  assert(engineChooseMove(&reply));
  assert(engineHistoryPly() == 2);
  assert(engineWhiteToMove());
  assert(engineUndoOne());
  assert(!engineWhiteToMove());
  assert(engineUndoOne());
  assert(engineHistoryPly() == 0);
  assert(engineWhiteToMove());

  EngineApplyResult illegal = engineApplyVoiceMove(Pawn, 5, 5);
  assert(illegal.status == EngineMoveIllegal);
  assert(engineHistoryPly() == 0);

  EngineApplyResult knight = engineApplyVoiceMoveFrom(Knight, 7, 1, 6, 3);
  assert(knight.status == EngineMoveApplied);
  expectMove(knight.move, "g1f3");

  engineNewGame();
  setAmbiguousRookPosition();
  EngineApplyResult guessed = engineApplyVoiceMove(Rook, 4, 1);
  assert(guessed.status == EngineMoveGuessed);
  assert(guessed.candidateCount == 2);
  assert(guessed.move.toFile == 4 && guessed.move.toRank == 1);

  const uint8_t otherFile = guessed.move.fromFile == 1 ? 8 : 1;
  ChessMoveInfo guessedReply = {};
  assert(engineChooseMove(&guessedReply));
  assert(engineHistoryPly() == 2);
  assert(engineUndoOne());
  assert(engineUndoOne());
  EngineApplyResult corrected = engineApplyVoiceMoveFrom(
      Rook, otherFile, 1, 4, 1);
  assert(corrected.status == EngineMoveApplied);
  assert(corrected.move.fromFile == otherFile);
  ChessMoveInfo correctedReply = {};
  assert(engineChooseMove(&correctedReply));
  assert(engineHistoryPly() == 2);

  testSpecialMoveFlags();
  testCapturedPiece();
  testFlashBook();
  testMoveQualityClassifier();

  puts("engine smoke tests passed");
  return 0;
}
