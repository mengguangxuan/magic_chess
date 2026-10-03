#pragma once

#include <Arduino.h>

#include "move_quality.h"

bool initDoubaoClient();
bool queueDoubaoTaunt(const MoveQualityReport& report,
                      const char* playedMove,
                      const char* bestMove);
void doubaoTask(void* parameter);

