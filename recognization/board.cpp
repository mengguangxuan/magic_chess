#include "board.h"

#include "bluetooth_transport.h"
#include "engine_adapter.h"
#include "move_quality.h"
#include "network_config.h"
#include "physical_board_adapter.h"
#include "receive.h"

extern hw_timer_t * clockTimer;
extern volatile bool clockRunning;
extern volatile bool clockWhiteTurn;

static void clockStart(bool whiteToMove) {
  clockWhiteTurn = whiteToMove;
  clockRunning = true;
  timerStart(clockTimer);
}

static void clockStop() {
  clockRunning = false;
  timerStop(clockTimer);

  digitalWrite(PIN_WA, LOW);
  digitalWrite(PIN_WB, LOW);
  digitalWrite(PIN_BA, LOW);
  digitalWrite(PIN_BB, LOW);
}

struct CorrectionRecord {
  bool available;
  bool moverWhite;
  PieceType piece;
  uint8_t targetFile;
  uint8_t targetRank;
  ChessMoveInfo selectedMove;
  bool engineReplyApplied;
};

struct PhysicalTurnRecord {
  PhysicalMoveRecord player;
  PhysicalMoveRecord engine;
};

enum MatchMode : uint8_t {
  TwoPlayerMode,
  EngineModeWaitingForColor,
  EngineModeReady
};

static CorrectionRecord lastCorrection = {};
static PhysicalTurnRecord lastPhysicalTurn = {};
static uint8_t lastTurnPlyCount = 0;
static PhysicalBoardController physicalBoard;
static MatchMode matchMode = TwoPlayerMode;
static bool humanWhite = true;
static bool correctionHold = false;

static void clearPhysicalTurn()
{
  lastPhysicalTurn = {};
}

static void clearTurnHistory()
{
  lastTurnPlyCount = 0;
  lastCorrection.available = false;
  correctionHold = false;
  correctionRequestActive = false;
  clearPhysicalTurn();
}

static bool moveInputAllowed()
{
  if (correctionHold || correctionRequestActive) {
    Serial.println("A correction is pending; normal moves are temporarily "
                   "disabled.");
    return false;
  }
  if (matchMode == EngineModeWaitingForColor) {
    Serial.println("Engine mode is waiting for WHT or BLK; move ignored.");
    return false;
  }
  if (matchMode == EngineModeReady && engineWhiteToMove() != humanWhite) {
    Serial.println("It is the engine's turn; move ignored.");
    return false;
  }
  return true;
}

static uint8_t collectConfusedFiles(uint8_t file, uint8_t alternatives[3])
{
  uint8_t count = 0;
  switch (file) {
    case 1: alternatives[count++] = 5; break;  // a may be e
    case 2: alternatives[count++] = 4; alternatives[count++] = 7; break;
    case 3: alternatives[count++] = 5; break;  // c may be e
    case 4: alternatives[count++] = 2; alternatives[count++] = 5; break;
    case 5: alternatives[count++] = 3; alternatives[count++] = 1;
            alternatives[count++] = 4; break;
    case 7: alternatives[count++] = 2; break;  // g may be b
    default: break;
  }
  return count;
}

static bool executePhysicalMove(const ChessMoveInfo& move, bool moverWhite,
                                PhysicalMoveRecord* record)
{
  if (record != NULL) {
    record->valid = false;
  }
  if (!physicalBoard.execute(move, moverWhite)) {
    Serial.println("Physical movement stopped; inspect the real board because "
                   "a multi-step move may be incomplete.");
    return false;
  }
  if (record != NULL) {
    record->valid = true;
    record->moverWhite = moverWhite;
    record->move = move;
  }
  // Emit events only for committed physical moves, never for search or undo.
  if (move.flags & MoveFlagCapture) {
    uint8_t event = 0;
    switch (move.capturedPiece) {
      case Queen: event = 2; break;
      case Bishop: event = 3; break;
      case Knight: event = 4; break;
      case Pawn: event = 5; break;
      case Rook: event = 6; break;
      default: break;
    }
    if (event != 0) queueAudioEvent(event);
  }
  if (engineGetGameState() == EngineGameCheckmate)
    queueAudioEvent(1);
  return true;
}

static bool isTerminalState(EngineGameState state)
{
  return state == EngineGameCheckmate ||
         state == EngineGameStalemate ||
         state == EngineGameDrawRepetition ||
         state == EngineGameDrawFiftyMove;
}

static void printMove(const char* owner, const ChessMoveInfo& move)
{
  char moveText[6] = {0};
  engineMoveToString(move, moveText);
  Serial.printf("%s move: %s", owner, moveText);
  if (move.flags & MoveFlagCapture) Serial.print(" capture");
  if (move.flags & MoveFlagCastle) Serial.print(" castle");
  if (move.flags & MoveFlagEnPassant) Serial.print(" en-passant");
  if (move.flags & MoveFlagPromotion) Serial.print(" promotion");
  Serial.println();
}

static void printGameState(EngineGameState state)
{
  switch (state) {
    case EngineGameCheck:
      Serial.println("Game state: check.");
      break;
    case EngineGameCheckmate:
      Serial.println("Game state: checkmate.");
      break;
    case EngineGameStalemate:
      Serial.println("Game state: stalemate.");
      break;
    case EngineGameDrawRepetition:
      Serial.println("Game state: draw by repetition.");
      break;
    case EngineGameDrawFiftyMove:
      Serial.println("Game state: draw by fifty-move rule.");
      break;
    default:
      break;
  }
}

static void printMoveQuality(const EngineAnalysis& before,
                             const EngineAnalysis& after,
                             const ChessMoveInfo& playedMove,
                             PieceType requestedPiece,
                             bool provisional)
{
  PieceType movedPiece = requestedPiece;
  if (playedMove.flags & MoveFlagPromotion) {
    movedPiece = playedMove.promotion;
  }
  const MoveQualityReport report = classifyMoveQuality(
      before, after, playedMove, movedPiece);

  char playedText[6] = {0};
  char bestText[6] = "none";
  engineMoveToString(playedMove, playedText);
  if (before.hasBestMove) {
    engineMoveToString(before.bestMove, bestText);
  }
  Serial.printf("Move quality%s: %s, type=%s, loss=%d cp, "
                "best=%s (%d), played=%s (%d).\n",
                provisional ? " (provisional)" : "",
                moveQualityName(report.quality),
                blunderKindName(report.kind), report.loss,
                bestText, report.bestScore, playedText, report.playedScore);
  if (!provisional) {
    if (report.loss >= CHESS_TAUNT_MIN_LOSS_CP) {
      bluetoothSendTaunt(static_cast<uint8_t>(report.kind), report.loss,
                         playedText, bestText);
    }
  }
}

static bool makeEngineReply(PhysicalMoveRecord* physicalRecord)
{
  if (correctionHold || correctionRequestActive) {
    Serial.println("Engine move deferred while waiting for correction.");
    return false;
  }

  ChessMoveInfo engineMove = {};
  const bool moverWhite = engineWhiteToMove();
  if (physicalRecord != NULL) {
    physicalRecord->valid = false;
  }
  if (!engineChooseMove(&engineMove)) {
    printGameState(engineGetGameState());
    return false;
  }

  // The voice task may receive mod while TSCP is searching. In that case,
  // undo the selected engine move before any motor movement begins.
  if (correctionHold || correctionRequestActive) {
    engineUndoOne();
    Serial.println("Engine move cancelled because correction mode started.");
    return false;
  }

  printMove("Engine", engineMove);
  if (!executePhysicalMove(engineMove, moverWhite, physicalRecord)) {
    engineUndoOne();
    Serial.println("Engine move was rolled back because physical movement failed.");
    return false;
  }
  if (isTerminalState(engineGetGameState())) {
    clockStop();
  } else {
    clockStart(!moverWhite);
  }
  printGameState(engineGetGameState());
  return true;
}

static void resumeAfterCorrectionWait()
{
  correctionHold = false;
  correctionRequestActive = false;
  clockStop();

  const EngineGameState state = engineGetGameState();
  if (isTerminalState(state) || matchMode == EngineModeWaitingForColor) {
    return;
  }

  if (matchMode == EngineModeReady && engineWhiteToMove() != humanWhite) {
    if (makeEngineReply(&lastPhysicalTurn.engine)) {
      lastTurnPlyCount = 2;
      if (lastCorrection.available) {
        lastCorrection.engineReplyApplied = true;
      }
    }
    return;
  }

  clockStart(engineWhiteToMove());
}

static void beginCorrectionWait()
{
  correctionHold = true;
  clockStop();
  Serial.println("Correction hold active: clock stopped and engine movement "
                 "blocked for up to 30 seconds.");
  if (!lastCorrection.available) {
    Serial.println("No ambiguous move is currently available; the hold will "
                   "still remain active until a source square or timeout.");
  }
}

static void handleCorrectionTimeout()
{
  correctionRequestActive = false;
  if (!correctionHold) {
    return;
  }
  Serial.println("Correction hold ended without a source square; resuming "
                 "the game.");
  resumeAfterCorrectionWait();
}

static void finishHumanMove(const EngineApplyResult& humanResult,
                            bool canCorrect,
                            bool moverWhite,
                            PieceType requestedPiece,
                            uint8_t targetFile,
                            uint8_t targetRank,
                            const EngineAnalysis& analysisBefore)
{
  clearPhysicalTurn();
  printMove(moverWhite ? "White player" : "Black player", humanResult.move);
  if (!executePhysicalMove(humanResult.move, moverWhite,
                           &lastPhysicalTurn.player)) {
    engineUndoOne();
    lastTurnPlyCount = 0;
    lastCorrection.available = false;
    Serial.println("Player move was rolled back because physical movement failed.");
    return;
  }

  EngineAnalysis analysisAfter = {};
  if (analysisBefore.valid && engineAnalyzeCurrentPosition(&analysisAfter)) {
    printMoveQuality(analysisBefore, analysisAfter, humanResult.move,
                     requestedPiece, canCorrect);
  } else {
    Serial.println("Move quality unavailable: TSCP analysis failed.");
  }
  clockStart(!moverWhite);
  lastTurnPlyCount = 1;

  if (canCorrect) {
    lastCorrection.available = true;
    lastCorrection.moverWhite = moverWhite;
    lastCorrection.piece = requestedPiece;
    lastCorrection.targetFile = targetFile;
    lastCorrection.targetRank = targetRank;
    lastCorrection.selectedMove = humanResult.move;
    lastCorrection.engineReplyApplied = false;

    char moveText[6] = {0};
    engineMoveToString(humanResult.move, moveText);
    Serial.printf("Ambiguous voice move: selected %s from %u candidates. "
                  "Say mod, then the intended source square, to correct it.\n",
                  moveText, humanResult.candidateCount);
  } else {
    lastCorrection.available = false;
  }

  const EngineGameState stateAfterHuman = engineGetGameState();
  printGameState(stateAfterHuman);
  if (isTerminalState(stateAfterHuman)) {
    clockStop();
    return;
  }

  if (matchMode == EngineModeReady &&
      makeEngineReply(&lastPhysicalTurn.engine)) {
    lastTurnPlyCount = 2;
    if (lastCorrection.available) {
      lastCorrection.engineReplyApplied = true;
    }
  }
}

static void handleMoveCommand(const VoiceCommand& command)
{
  if (!moveInputAllowed()) {
    return;
  }

  const bool moverWhite = engineWhiteToMove();
  EngineAnalysis analysisBefore = {};
  engineAnalyzeCurrentPosition(&analysisBefore);
  uint8_t resolvedFile = command.file;
  EngineApplyResult result = engineApplyVoiceMove(command.piece,
                                                   command.file,
                                                   command.rank);
  if (result.status == EngineMoveIllegal) {
    uint8_t alternatives[3] = {0};
    const uint8_t alternativeCount =
        collectConfusedFiles(command.file, alternatives);
    uint8_t legalAlternative = 0;
    uint8_t legalAlternativeCount = 0;
    for (uint8_t i = 0; i < alternativeCount; ++i) {
      if (engineCountVoiceMoveCandidates(command.piece, alternatives[i],
                                         command.rank) > 0) {
        legalAlternative = alternatives[i];
        ++legalAlternativeCount;
      }
    }

    if (legalAlternativeCount > 1) {
      Serial.println("Voice target correction is ambiguous; please repeat the "
                     "piece and square. Board unchanged.");
      return;
    }
    if (legalAlternativeCount == 1) {
      EngineApplyResult corrected = engineApplyVoiceMove(
          command.piece, legalAlternative, command.rank);
      if (corrected.status == EngineMoveApplied ||
          corrected.status == EngineMoveGuessed) {
        Serial.printf("Voice target correction: %c%u -> %c%u.\n",
                      static_cast<char>('a' + command.file - 1), command.rank,
                      static_cast<char>('a' + legalAlternative - 1), command.rank);
        result = corrected;
        resolvedFile = legalAlternative;
      }
    }
  }

  if (result.status == EngineMoveIllegal ||
      result.status == EngineMoveUnavailable) {
    Serial.println("Illegal or unrecognized player move; board unchanged.");
    return;
  }

  finishHumanMove(result, result.status == EngineMoveGuessed,
                  moverWhite, command.piece, resolvedFile, command.rank,
                  analysisBefore);
}

static void handleCastleCommand(bool kingSide)
{
  if (!moveInputAllowed()) {
    return;
  }

  const bool moverWhite = engineWhiteToMove();
  EngineAnalysis analysisBefore = {};
  engineAnalyzeCurrentPosition(&analysisBefore);
  EngineApplyResult result = engineApplyCastle(kingSide);
  if (result.status == EngineMoveIllegal ||
      result.status == EngineMoveUnavailable) {
    Serial.println("Illegal castle; board unchanged.");
    return;
  }

  finishHumanMove(result, false, moverWhite, King,
                  kingSide ? 7 : 3,
                  result.move.toRank, analysisBefore);
}

static bool restorePreviousGuess(bool engineReplyWasApplied)
{
  const ChessMoveInfo oldMove = lastCorrection.selectedMove;
  EngineApplyResult restored = engineApplyVoiceMoveFrom(
      lastCorrection.piece,
      oldMove.fromFile, oldMove.fromRank,
      lastCorrection.targetFile, lastCorrection.targetRank);

  if (restored.status == EngineMoveIllegal ||
      restored.status == EngineMoveUnavailable) {
    Serial.println("Internal error: could not restore the previous guess.");
    return false;
  }

  clearPhysicalTurn();
  if (!executePhysicalMove(restored.move, lastCorrection.moverWhite,
                           &lastPhysicalTurn.player)) {
    engineUndoOne();
    Serial.println("Could not restore the previous physical player move.");
    return false;
  }

  lastCorrection.selectedMove = restored.move;
  lastTurnPlyCount = 1;
  if (engineReplyWasApplied && !isTerminalState(engineGetGameState())) {
    lastCorrection.engineReplyApplied =
        makeEngineReply(&lastPhysicalTurn.engine);
    if (lastCorrection.engineReplyApplied) {
      lastTurnPlyCount = 2;
    }
  } else {
    lastCorrection.engineReplyApplied = false;
  }
  return true;
}

static void handleCorrection(uint8_t sourceFile, uint8_t sourceRank)
{
  correctionRequestActive = false;
  correctionHold = false;
  clockStop();

  if (!lastCorrection.available) {
    Serial.println("No ambiguous move is available for correction.");
    resumeAfterCorrectionWait();
    return;
  }

  const bool engineReplyWasApplied = lastCorrection.engineReplyApplied;
  if (!physicalBoard.canUndo(lastPhysicalTurn.engine) ||
      !physicalBoard.canUndo(lastPhysicalTurn.player)) {
    Serial.println("This move cannot be corrected automatically because it "
                   "contains a capture, en-passant capture, or promotion.");
    resumeAfterCorrectionWait();
    return;
  }

  if (lastPhysicalTurn.engine.valid &&
      !physicalBoard.undo(lastPhysicalTurn.engine)) {
    Serial.println("Correction failed while restoring the physical engine move.");
    return;
  }
  if (!physicalBoard.undo(lastPhysicalTurn.player)) {
    Serial.println("Correction failed while restoring the physical player move.");
    return;
  }

  if (engineReplyWasApplied && !engineUndoOne()) {
    Serial.println("Correction failed while undoing the engine reply.");
    return;
  }
  if (!engineUndoOne()) {
    Serial.println("Correction failed while undoing the guessed move.");
    return;
  }

  EngineApplyResult corrected = engineApplyVoiceMoveFrom(
      lastCorrection.piece,
      sourceFile, sourceRank,
      lastCorrection.targetFile, lastCorrection.targetRank);

  if (corrected.status == EngineMoveIllegal ||
      corrected.status == EngineMoveUnavailable) {
    Serial.println("That source square does not produce a legal candidate; "
                   "restoring the previous guess.");
    if (restorePreviousGuess(engineReplyWasApplied)) {
      resumeAfterCorrectionWait();
    }
    return;
  }

  clearPhysicalTurn();
  if (!executePhysicalMove(corrected.move, lastCorrection.moverWhite,
                           &lastPhysicalTurn.player)) {
    engineUndoOne();
    Serial.println("Corrected physical move failed; restoring previous guess.");
    if (restorePreviousGuess(engineReplyWasApplied)) {
      resumeAfterCorrectionWait();
    }
    return;
  }

  lastCorrection.selectedMove = corrected.move;
  lastCorrection.engineReplyApplied = false;
  printMove("Corrected player", corrected.move);
  lastTurnPlyCount = 1;

  const EngineGameState stateAfterCorrection = engineGetGameState();
  printGameState(stateAfterCorrection);
  if (isTerminalState(stateAfterCorrection)) {
    clockStop();
  } else {
    resumeAfterCorrectionWait();
  }
}

static void handleUndoTurn()
{
  if (lastTurnPlyCount == 0) {
    Serial.println("No completed turn is available to undo.");
    return;
  }

  if (matchMode == EngineModeReady && lastPhysicalTurn.engine.valid &&
      !lastPhysicalTurn.player.valid && lastTurnPlyCount == 1) {
    Serial.println("The engine's opening move cannot be undone by itself; "
                   "send new to restart the game.");
    return;
  }

  if (!physicalBoard.canUndo(lastPhysicalTurn.engine) ||
      !physicalBoard.canUndo(lastPhysicalTurn.player)) {
    Serial.println("This turn cannot be undone automatically because it "
                   "contains a capture, en-passant capture, or promotion.");
    return;
  }

  if (lastPhysicalTurn.engine.valid &&
      !physicalBoard.undo(lastPhysicalTurn.engine)) {
    Serial.println("Could not undo the physical engine move.");
    return;
  }
  if (!physicalBoard.undo(lastPhysicalTurn.player)) {
    Serial.println("Could not undo the physical player move.");
    return;
  }

  uint8_t undone = 0;
  while (undone < lastTurnPlyCount && engineUndoOne()) {
    ++undone;
  }

  Serial.printf("Undid %u half-move(s).\n", undone);
  lastTurnPlyCount = 0;
  lastCorrection.available = false;
  clearPhysicalTurn();
  clockStop();
}

static void enterEngineMode()
{
  engineNewGame();
  clearTurnHistory();
  matchMode = EngineModeWaitingForColor;
  clockStop();
  Serial.println("Engine opponent enabled. Reset the real board, then send "
                 "WHT or BLK to choose the player's color.");
}

static void choosePlayerColor(bool chooseWhite)
{
  if (matchMode != EngineModeWaitingForColor) {
    Serial.println("Color selection ignored; send AIO first.");
    return;
  }

  engineNewGame();
  clearTurnHistory();
  humanWhite = chooseWhite;
  matchMode = EngineModeReady;

  clockStop();

  if (humanWhite) {
    Serial.println("Engine game started: player is white and moves first.");
    return;
  }

  Serial.println("Engine game started: player is black; engine makes the "
                 "opening white move.");
  if (makeEngineReply(&lastPhysicalTurn.engine)) {
    lastTurnPlyCount = 1;
  }
}

static void startNewGame()
{
  Serial.println("New command received: returning the motors to (0,0).");
  if (!physicalBoard.reset()) {
    Serial.println("New game aborted: motor homing failed; engine state was "
                   "left unchanged.");
    return;
  }
  engineNewGame();
  clearTurnHistory();

  clockStop();

  if (matchMode == TwoPlayerMode) {
    Serial.println("New two-player game started. Reset all real pieces to "
                   "their initial squares; white moves first.");
    return;
  }

  matchMode = EngineModeWaitingForColor;
  Serial.println("New engine game prepared. Reset all real pieces, then "
                 "send WHT or BLK again.");
}

static void processCommand(const VoiceCommand& command)
{
  switch (command.command) {
    case MoveToSquare:
      handleMoveCommand(command);
      break;
    case CastleKingSide:
      handleCastleCommand(true);
      break;
    case CastleQueenSide:
      handleCastleCommand(false);
      break;
    case BeginCorrectionWait:
      beginCorrectionWait();
      break;
    case CorrectSourceSquare:
      handleCorrection(command.file, command.rank);
      break;
    case CorrectionWaitTimedOut:
      handleCorrectionTimeout();
      break;
    case UndoTurn:
      handleUndoTurn();
      break;
    case StartNewGame:
      startNewGame();
      break;
    case EnableEngineOpponent:
      enterEngineMode();
      break;
    case ChoosePlayerWhite:
      choosePlayerColor(true);
      break;
    case ChoosePlayerBlack:
      choosePlayerColor(false);
      break;
  }
}

void boardTask(void* parameter)
{
  (void)parameter;
  physicalBoard.begin();
  engineBegin();
  Serial.println("Physical board and TSCP initialized. Default mode is "
                 "two-player; white moves first. Send AIO for engine mode.");

  VoiceCommand command = {};
  while (true) {
    if (xQueueReceive(movequeue, &command, portMAX_DELAY) == pdPASS) {
      processCommand(command);
    }
  }
}
