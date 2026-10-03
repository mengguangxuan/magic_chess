#include "physical_board_adapter.h"

#include <Arduino.h>

static int boardX(uint8_t file)
{
  return static_cast<int>(file) - 1;
}

static int boardY(uint8_t rank)
{
  return static_cast<int>(rank) - 1;
}

static const char* promotionName(PieceType piece)
{
  switch (piece) {
    case Knight: return "knight";
    case Bishop: return "bishop";
    case Rook: return "rook";
    default: return "queen";
  }
}

bool PhysicalBoardController::coordinatesValid(const ChessMoveInfo& move)
{
  return move.fromFile >= 1 && move.fromFile <= 8 &&
         move.toFile >= 1 && move.toFile <= 8 &&
         move.fromRank >= 1 && move.fromRank <= 8 &&
         move.toRank >= 1 && move.toRank <= 8;
}

void PhysicalBoardController::begin()
{
  board_.init();
}

bool PhysicalBoardController::reset()
{
  return board_.reset();
}

bool PhysicalBoardController::executeCastle(const ChessMoveInfo& move)
{
  const int fromX = boardX(move.fromFile);
  const int fromY = boardY(move.fromRank);
  const int toX = boardX(move.toFile);
  const int toY = boardY(move.toRank);

  // Move the king first. moveR then takes the rook around the king.
  if (!board_.move(fromX, fromY, toX, toY)) {
    return false;
  }
  if (move.toFile == 7) {
    return board_.moveR(7, toY, 5, toY);  // h-file rook to f-file
  } else {
    return board_.moveR(0, toY, 3, toY);  // a-file rook to d-file
  }
}

bool PhysicalBoardController::undoCastle(const ChessMoveInfo& move)
{
  const int fromX = boardX(move.fromFile);
  const int fromY = boardY(move.fromRank);
  const int toX = boardX(move.toFile);
  const int toY = boardY(move.toRank);

  // Move the rook out of the king's return path before returning the king.
  if (move.toFile == 7) {
    if (!board_.moveR(5, toY, 7, toY)) return false;
  } else {
    if (!board_.moveR(3, toY, 0, toY)) return false;
  }
  return board_.move(toX, toY, fromX, fromY);
}

bool PhysicalBoardController::execute(const ChessMoveInfo& move,
                                      bool moverWhite)
{
  if (!coordinatesValid(move)) {
    Serial.println("Physical board rejected invalid coordinates.");
    return false;
  }

  const int fromX = boardX(move.fromFile);
  const int fromY = boardY(move.fromRank);
  const int toX = boardX(move.toFile);
  const int toY = boardY(move.toRank);
  const bool capturedPieceIsBlack = moverWhite;

  if (move.flags & MoveFlagCastle) {
    if (!executeCastle(move)) return false;
  } else if (move.flags & MoveFlagEnPassant) {
    const int capturedY = moverWhite ? toY - 1 : toY + 1;
    if (!board_.moveE(toX, capturedY, capturedPieceIsBlack)) return false;
    if (!board_.move(fromX, fromY, toX, toY)) return false;
  } else if (move.flags & MoveFlagCapture) {
    if (!board_.move(fromX, fromY, toX, toY, capturedPieceIsBlack)) return false;
  } else {
    if (!board_.move(fromX, fromY, toX, toY)) return false;
  }

  if (move.flags & MoveFlagPromotion) {
    Serial.printf("Promotion reached: replace the pawn with a %s manually.\n",
                  promotionName(move.promotion));
  }
  return true;
}

bool PhysicalBoardController::canUndo(const PhysicalMoveRecord& record) const
{
  if (!record.valid) {
    return true;
  }

  // The current Board API can put captured pieces into a stack, but cannot
  // restore them. Promotion also requires a manual piece replacement.
  return !(record.move.flags & (MoveFlagCapture |
                                MoveFlagEnPassant |
                                MoveFlagPromotion));
}

bool PhysicalBoardController::undo(const PhysicalMoveRecord& record)
{
  if (!record.valid) {
    return true;
  }
  if (!canUndo(record)) {
    return false;
  }

  if (record.move.flags & MoveFlagCastle) {
    return undoCastle(record.move);
  }

  return board_.move(boardX(record.move.toFile), boardY(record.move.toRank),
                     boardX(record.move.fromFile), boardY(record.move.fromRank));
}
