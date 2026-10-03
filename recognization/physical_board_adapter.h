#pragma once

#include "chessBoard.h"
#include "chess_types.h"

struct PhysicalMoveRecord {
  bool valid;
  bool moverWhite;
  ChessMoveInfo move;
};

class PhysicalBoardController {
public:
  void begin();
  bool reset();
  bool execute(const ChessMoveInfo& move, bool moverWhite);
  bool canUndo(const PhysicalMoveRecord& record) const;
  bool undo(const PhysicalMoveRecord& record);

private:
  Board board_;

  static bool coordinatesValid(const ChessMoveInfo& move);
  bool executeCastle(const ChessMoveInfo& move);
  bool undoCastle(const ChessMoveInfo& move);
};
