#include "engine_adapter.h"

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include "opening_book_data.h"
#ifdef ARDUINO_ARCH_ESP32
#include <esp_random.h>
#endif

extern "C" {
#include "src/tscp/defs.h"
#include "src/tscp/data.h"

void init_board();
void init_hash();
void gen();
BOOL makemove(move_bytes m);
void takeback();
BOOL in_check(int s);
int reps();
int think_score(int output);
}

#ifndef TSCP_ENGINE_MOVE_TIME_MS
#define TSCP_ENGINE_MOVE_TIME_MS 600
#endif

#ifndef TSCP_ENGINE_MAX_DEPTH
#define TSCP_ENGINE_MAX_DEPTH 6
#endif

#ifndef TSCP_GUESS_TIME_MS
#define TSCP_GUESS_TIME_MS 120
#endif

#ifndef TSCP_GUESS_DEPTH
#define TSCP_GUESS_DEPTH 3
#endif

#ifndef TSCP_ANALYSIS_TIME_MS
#define TSCP_ANALYSIS_TIME_MS 160
#endif

#ifndef TSCP_ANALYSIS_MAX_DEPTH
#define TSCP_ANALYSIS_MAX_DEPTH 4
#endif

static const uint8_t MAX_VOICE_CANDIDATES = 16;
static bool openingBookEnabled = true;

extern "C" int get_ms()
{
  static uint32_t origin = millis();
  return static_cast<int>(millis() - origin);
}

extern "C" void engine_platform_yield()
{
  // checkup() calls this every 1024 searched nodes.
  yield();
}

extern "C" int book_move()
{
  if (!openingBookEnabled || ply != 0 || hply > 25)
    return -1;

  // Match complete move-history tokens, preserving the original book weights.
  // Scan Flash directly; memory consumption does not grow with the book size.
  unsigned matches = 0;
  int selected = -1;
  for (const char* line = openingBook; *line;) {
    const char* end = strchr(line, '\n');
    if (end == NULL) end = line + strlen(line);
    const char* cursor = line;
    bool prefixMatches = true;
    for (int p = 0; p < hply; ++p) {
      const move_bytes previous = hist_dat[p].m.b;
      if (end - cursor < 5 || (previous.bits & 32) ||
          cursor[0] != 'a' + COL(previous.from) ||
          cursor[1] != '0' + 8 - ROW(previous.from) ||
          cursor[2] != 'a' + COL(previous.to) ||
          cursor[3] != '0' + 8 - ROW(previous.to) ||
          cursor[4] != ' ') {
        prefixMatches = false;
        break;
      }
      cursor += 5;
    }
    if (prefixMatches && end - cursor >= 4 &&
        (end - cursor == 4 || cursor[4] == ' ' || cursor[4] == '\r')) {
      for (int i = first_move[0]; i < first_move[1]; ++i) {
        const move candidate = gen_dat[i].m;
        if ((candidate.b.bits & 32) ||
            cursor[0] != 'a' + COL(candidate.b.from) ||
            cursor[1] != '0' + 8 - ROW(candidate.b.from) ||
            cursor[2] != 'a' + COL(candidate.b.to) ||
            cursor[3] != '0' + 8 - ROW(candidate.b.to))
          continue;
        if (!makemove(candidate.b)) continue;
        takeback();
        ++matches;
#ifdef ARDUINO_ARCH_ESP32
        const unsigned randomValue = esp_random();
#else
        const unsigned randomValue = static_cast<unsigned>(rand());
#endif
        if (randomValue % matches == 0) selected = candidate.u;
        break;
      }
    }
    line = *end ? end + 1 : end;
  }
  return selected;
}

extern "C" char* move_str(move_bytes m)
{
  static char text[6];
  char promotion = '\0';

  if (m.bits & 32) {
    switch (m.promote) {
      case KNIGHT: promotion = 'n'; break;
      case BISHOP: promotion = 'b'; break;
      case ROOK: promotion = 'r'; break;
      default: promotion = 'q'; break;
    }
  }

  text[0] = static_cast<char>(COL(m.from) + 'a');
  text[1] = static_cast<char>('0' + 8 - ROW(m.from));
  text[2] = static_cast<char>(COL(m.to) + 'a');
  text[3] = static_cast<char>('0' + 8 - ROW(m.to));
  text[4] = promotion;
  text[5] = '\0';
  return text;
}

static int toEnginePiece(PieceType pieceType)
{
  switch (pieceType) {
    case Pawn: return PAWN;
    case Knight: return KNIGHT;
    case Bishop: return BISHOP;
    case Rook: return ROOK;
    case Queen: return QUEEN;
    case King: return KING;
    default: return EMPTY;
  }
}

static PieceType fromEnginePiece(int enginePiece)
{
  switch (enginePiece) {
    case PAWN: return Pawn;
    case KNIGHT: return Knight;
    case BISHOP: return Bishop;
    case ROOK: return Rook;
    case QUEEN: return Queen;
    case KING: return King;
    default: return DefaultPiece;
  }
}

static int toSquare(uint8_t file, uint8_t rank)
{
  if (file < 1 || file > 8 || rank < 1 || rank > 8) {
    return -1;
  }
  return (8 - rank) * 8 + (file - 1);
}

static ChessMoveInfo describeMove(move_bytes selected)
{
  ChessMoveInfo result = {};
  result.fromFile = static_cast<uint8_t>(COL(selected.from) + 1);
  result.fromRank = static_cast<uint8_t>(8 - ROW(selected.from));
  result.toFile = static_cast<uint8_t>(COL(selected.to) + 1);
  result.toRank = static_cast<uint8_t>(8 - ROW(selected.to));
  result.promotion = (selected.bits & 32)
                         ? fromEnginePiece(selected.promote)
                         : DefaultPiece;
  result.flags = MoveFlagNone;
  result.capturedPiece = DefaultPiece;
  if (selected.bits & 1)
    result.capturedPiece = fromEnginePiece(piece[static_cast<unsigned char>(selected.to)]);
  if (selected.bits & 4)
    result.capturedPiece = Pawn;
  if (selected.bits & 1) result.flags |= MoveFlagCapture;
  if (selected.bits & 2) result.flags |= MoveFlagCastle;
  if (selected.bits & 4) {
    result.flags |= MoveFlagEnPassant;
    result.flags |= MoveFlagCapture;
  }
  if (selected.bits & 32) result.flags |= MoveFlagPromotion;
  return result;
}

static uint8_t collectCandidates(PieceType requestedPiece, int targetSquare,
                                 int sourceSquare, move candidates[],
                                 uint8_t capacity)
{
  const int requestedEnginePiece = toEnginePiece(requestedPiece);
  if (requestedEnginePiece == EMPTY || targetSquare < 0) {
    return 0;
  }

  ply = 0;
  gen();
  uint8_t count = 0;

  for (int i = first_move[0]; i < first_move[1]; ++i) {
    move candidate = gen_dat[i].m;
    const int from = static_cast<unsigned char>(candidate.b.from);
    const int to = static_cast<unsigned char>(candidate.b.to);

    if (to != targetSquare || piece[from] != requestedEnginePiece) {
      continue;
    }
    if (sourceSquare >= 0 && from != sourceSquare) {
      continue;
    }

    // Until the voice protocol gains a promotion choice, promote to queen.
    if ((candidate.b.bits & 32) && candidate.b.promote != QUEEN) {
      continue;
    }

    if (makemove(candidate.b)) {
      takeback();
      if (count < capacity) {
        candidates[count++] = candidate;
      }
    }
  }

  return count;
}

static bool applyMove(move selected, ChessMoveInfo* description)
{
  ChessMoveInfo moveDescription = describeMove(selected.b);
  if (!makemove(selected.b)) {
    return false;
  }

  ply = 0;
  gen();
  if (description != NULL) {
    *description = moveDescription;
  }
  return true;
}

bool engineUndoOne()
{
  if (hply <= 0) {
    return false;
  }

  takeback();
  ply = 0;
  gen();
  return true;
}

bool engineWhiteToMove()
{
  return side == LIGHT;
}

static int scoreCandidate(move candidate)
{
  if (!applyMove(candidate, NULL)) {
    return -10001;
  }

  const int savedMaxTime = max_time;
  const int savedMaxDepth = max_depth;
  max_time = TSCP_GUESS_TIME_MS;
  max_depth = TSCP_GUESS_DEPTH;

  // The candidate has switched sides, so negate the opponent's score.
  // Candidate scoring must search even when the continuation is in the book.
  const bool savedBookEnabled = openingBookEnabled;
  openingBookEnabled = false;
  const int score = -think_score(0);
  openingBookEnabled = savedBookEnabled;

  max_time = savedMaxTime;
  max_depth = savedMaxDepth;
  engineUndoOne();
  return score;
}

static EngineApplyResult resolveAndApply(PieceType requestedPiece,
                                         int targetSquare, int sourceSquare)
{
  EngineApplyResult result = {};
  result.status = EngineMoveIllegal;
  result.move.promotion = DefaultPiece;
  result.score = -10001;

  move candidates[MAX_VOICE_CANDIDATES];
  const uint8_t count = collectCandidates(requestedPiece, targetSquare,
                                          sourceSquare, candidates,
                                          MAX_VOICE_CANDIDATES);
  result.candidateCount = count;
  if (count == 0) {
    return result;
  }

  uint8_t selectedIndex = 0;
  if (count > 1) {
    int bestScore = -10001;
    for (uint8_t i = 0; i < count; ++i) {
      const int candidateScore = scoreCandidate(candidates[i]);
      if (candidateScore > bestScore) {
        bestScore = candidateScore;
        selectedIndex = i;
      }
    }
    result.score = bestScore;
  }

  if (!applyMove(candidates[selectedIndex], &result.move)) {
    result.status = EngineMoveUnavailable;
    return result;
  }

  result.status = count > 1 ? EngineMoveGuessed : EngineMoveApplied;
  return result;
}

void engineBegin()
{
  init_hash();
  init_board();
  ply = 0;
  gen();
  max_time = TSCP_ENGINE_MOVE_TIME_MS;
  max_depth = TSCP_ENGINE_MAX_DEPTH;
}

void engineNewGame()
{
  init_board();
  ply = 0;
  gen();
}

EngineApplyResult engineApplyVoiceMove(PieceType requestedPiece,
                                       uint8_t targetFile,
                                       uint8_t targetRank)
{
  return resolveAndApply(requestedPiece,
                         toSquare(targetFile, targetRank), -1);
}

uint8_t engineCountVoiceMoveCandidates(PieceType requestedPiece,
                                       uint8_t targetFile,
                                       uint8_t targetRank)
{
  move candidates[MAX_VOICE_CANDIDATES];
  return collectCandidates(requestedPiece,
                           toSquare(targetFile, targetRank), -1,
                           candidates, MAX_VOICE_CANDIDATES);
}

EngineApplyResult engineApplyVoiceMoveFrom(PieceType requestedPiece,
                                           uint8_t sourceFile,
                                           uint8_t sourceRank,
                                           uint8_t targetFile,
                                           uint8_t targetRank)
{
  return resolveAndApply(requestedPiece,
                         toSquare(targetFile, targetRank),
                         toSquare(sourceFile, sourceRank));
}

EngineApplyResult engineApplyCastle(bool kingSide)
{
  const uint8_t rank = side == LIGHT ? 1 : 8;
  return engineApplyVoiceMoveFrom(King, 5, rank,
                                  kingSide ? 7 : 3, rank);
}

bool engineChooseMove(ChessMoveInfo* result)
{
  const EngineGameState state = engineGetGameState();
  if (state != EngineGameOngoing && state != EngineGameCheck) {
    return false;
  }

  max_time = TSCP_ENGINE_MOVE_TIME_MS;
  max_depth = TSCP_ENGINE_MAX_DEPTH;
  think_score(0);

  move selected = pv[0][0];
  if (selected.u == 0) {
    return false;
  }
  return applyMove(selected, result);
}

bool engineAnalyzeCurrentPosition(EngineAnalysis* result)
{
  if (result == NULL) {
    return false;
  }

  *result = {};
  const int savedMaxTime = max_time;
  const int savedMaxDepth = max_depth;
  const bool savedBookEnabled = openingBookEnabled;

  // Use a real search for both sides. A book move has no comparable search
  // score and would make the before/after loss calculation inconsistent.
  openingBookEnabled = false;
  max_time = TSCP_ANALYSIS_TIME_MS;
  max_depth = TSCP_ANALYSIS_MAX_DEPTH;
  result->score = think_score(0);

  const move selected = pv[0][0];
  result->hasBestMove = selected.u != 0 && selected.u != -1;
  if (result->hasBestMove) {
    result->bestMove = describeMove(selected.b);
  }
  result->valid = true;

  openingBookEnabled = savedBookEnabled;
  max_time = savedMaxTime;
  max_depth = savedMaxDepth;
  ply = 0;
  gen();
  return true;
}

int engineHistoryPly()
{
  return hply;
}

EngineGameState engineGetGameState()
{
  ply = 0;
  gen();
  bool hasLegalMove = false;

  for (int i = first_move[0]; i < first_move[1]; ++i) {
    move candidate = gen_dat[i].m;
    if (makemove(candidate.b)) {
      takeback();
      hasLegalMove = true;
      break;
    }
  }

  if (!hasLegalMove) {
    return in_check(side) ? EngineGameCheckmate : EngineGameStalemate;
  }
  if (reps() >= 2) {
    return EngineGameDrawRepetition;
  }
  if (fifty >= 100) {
    return EngineGameDrawFiftyMove;
  }
  return in_check(side) ? EngineGameCheck : EngineGameOngoing;
}

void engineMoveToString(const ChessMoveInfo& move, char output[6])
{
  char promotion = '\0';
  if (move.flags & MoveFlagPromotion) {
    switch (move.promotion) {
      case Knight: promotion = 'n'; break;
      case Bishop: promotion = 'b'; break;
      case Rook: promotion = 'r'; break;
      default: promotion = 'q'; break;
    }
  }

  output[0] = static_cast<char>('a' + move.fromFile - 1);
  output[1] = static_cast<char>('0' + move.fromRank);
  output[2] = static_cast<char>('a' + move.toFile - 1);
  output[3] = static_cast<char>('0' + move.toRank);
  output[4] = promotion;
  output[5] = '\0';
}
