#include <Arduino.h>
#include <ESP32Encoder.h>
#include <cmath>
#include "chessBoard.h"

namespace {
#ifndef CHESSBOARD_MOVE_TIMEOUT_MS
#define CHESSBOARD_MOVE_TIMEOUT_MS 20000UL
#endif

#ifndef CHESSBOARD_MOTION_DEBUG
#define CHESSBOARD_MOTION_DEBUG 1
#endif

    void hi(int pin) { digitalWrite(pin, HIGH); }
    void lo(int pin) { digitalWrite(pin, LOW); }

    int crt_x, crt_y;
    int err_x, err_y, lasterr_x = 0, lasterr_y = 0;
    long int_x = 0, int_y = 0;
    int pwm_max = 255, int_max = 3000, dead = 110;
    float Kp = 0.06, Ki = 0.002, Kd = 0.4;
    float ux, uy;
    char on_x = 1, on_y = 1, delta = 20;
    unsigned long lastMvTime = 0;
    unsigned long lastDebugTime = 0;
    ESP32Encoder ex, ey;

    int square = 5000, bias = 2500;
    constexpr int kMinTarget = 0;
    constexpr int kMaxTarget = 45000;
    constexpr int kMinEncoder = -1000;
    constexpr int kMaxEncoder = 46000;
}

void Board::init() {
    pinMode(MX1, OUTPUT);
    pinMode(MX2, OUTPUT);
    pinMode(MY1, OUTPUT);
    pinMode(MY2, OUTPUT);
    pinMode(EX, OUTPUT);
    pinMode(EY, OUTPUT);
    pinMode(EM, OUTPUT);

    ex.attachFullQuad(MXA, MXB);
    ey.attachFullQuad(MYB, MYA);
    ex.clearCount();
    ey.clearCount();
}

void Board::beginSequence() {
    if (sequenceDepth == 0) movementFailed = false;
    ++sequenceDepth;
}

bool Board::endSequence() {
    if (sequenceDepth > 0) --sequenceDepth;
    if (movementFailed || sequenceDepth == 0) lo(EM);
    return !movementFailed;
}

void Board::failMovement(const char* reason, int targetX, int targetY) {
    movementFailed = true;
    analogWrite(EX, 0);
    analogWrite(EY, 0);
    lo(EM);
    Serial.printf("Motor movement failed (%s): current=(%d,%d), "
                  "target=(%d,%d).\n", reason, crt_x, crt_y,
                  targetX, targetY);
}

void Board::_move_(int dest_x, int dest_y) {
    if (movementFailed) {
        analogWrite(EX, 0);
        analogWrite(EY, 0);
        lo(EM);
        return;
    }
    if (dest_x < kMinTarget || dest_x > kMaxTarget ||
        dest_y < kMinTarget || dest_y > kMaxTarget) {
        failMovement("target outside safe range", dest_x, dest_y);
        return;
    }

    const unsigned long moveStarted = millis();
    on_x = on_y = 1;
    for(;;) {
        if (on_x || on_y || millis() - lastMvTime < 100) {
            crt_x = ex.getCount(); crt_y = ey.getCount();
            if (crt_x < kMinEncoder || crt_x > kMaxEncoder ||
                crt_y < kMinEncoder || crt_y > kMaxEncoder) {
                failMovement("encoder outside safe range", dest_x, dest_y);
                return;
            }
            if (millis() - moveStarted >= CHESSBOARD_MOVE_TIMEOUT_MS) {
                failMovement("timeout", dest_x, dest_y);
                return;
            }
            if ((crt_x > dest_x ? crt_x - dest_x : dest_x - crt_x) < delta) {
                on_x = 0;
                analogWrite(EX, 0);
            } else on_x = 1;
            if ((crt_y > dest_y ? crt_y - dest_y : dest_y - crt_y) < delta) {
                on_y = 0;
                analogWrite(EY, 0);
            } else on_y = 1;
            if (on_y || on_x) lastMvTime = millis();


            err_x = dest_x - crt_x; err_y = dest_y - crt_y;

            int_x += err_x; int_y += err_y;
            int_x = int_x > int_max ? int_max : int_x < -int_max ? -int_max : int_x;
            int_y = int_y > int_max ? int_max : int_y < -int_max ? -int_max : int_y;

            ux = Kp*err_x + Ki*int_x + Kd*(err_x - lasterr_x);
            uy = Kp*err_y + Ki*int_y + Kd*(err_y - lasterr_y);

            ux = ux > 0 ? ux + dead : ux < 0 ? ux - dead : 0;
            uy = uy > 0 ? uy + dead : uy < 0 ? uy - dead : 0;

            ux = ux > pwm_max ? pwm_max : ux < -pwm_max ? -pwm_max : ux;
            uy = uy > pwm_max ? pwm_max : uy < -pwm_max ? -pwm_max : uy;

            if (ux > 0 && on_x) {
                hi(MX2); lo(MX1);
                analogWrite(EX, ux);
            } else if (ux < 0 && on_x) {
                hi(MX1); lo(MX2);
                analogWrite(EX, -ux);
            } else if (on_x) {
                hi(MX1); hi(MX2);
                analogWrite(EX, pwm_max);
            }

            if (uy > 0 && on_y) {
                hi(MY2); lo(MY1);
                analogWrite(EY, uy);
            } else if (uy < 0 && on_y) {
                hi(MY1); lo(MY2);
                analogWrite(EY, -uy);
            } else if (on_y) {
                hi(MY1); hi(MY2);
                analogWrite(EY, pwm_max);
            }

            lasterr_x = err_x;
            lasterr_y = err_y;
#if CHESSBOARD_MOTION_DEBUG
            if (millis() - lastDebugTime >= 100) {
                Serial.printf("Motor position: x=%d, y=%d, target=(%d,%d).\n",
                              crt_x, crt_y, dest_x, dest_y);
                lastDebugTime = millis();
            }
#endif
            delay(10);
        } else {
            analogWrite(EX, 0);
            analogWrite(EY, 0);
            return;
        }
    }
}

void Board::_move_(Poz& to) {
    _move_(to.x, to.y);
}

bool Board::move(int from_x, int from_y, int to_x, int to_y) {
    beginSequence();
    lo(EM);
    _move_(from_x*square +bias, from_y*square +2*bias);//
    hi(EM);
    if (from_x == to_x || from_y == to_y || abs(from_x-to_x) == abs(from_y-to_y)) {
        _move_(to_x*square +bias, to_y*square +2*bias);//
    } else {
        if (from_x-to_x==1 || from_x-to_x==-1) {
            _move_(square*(from_x+to_x)/2 +bias, square*from_y +2*bias);//
            _move_(square*(from_x+to_x)/2 +bias, square*to_y +2*bias);//
            _move_(square*to_x +bias, square*to_y +2*bias);//
        } else {
            _move_(square*from_x +bias, square*(from_y+to_y)/2 +2*bias);//
            _move_(square*to_x +bias, square*(from_y+to_y)/2 +2*bias);//
            _move_(square*to_x +bias, square*to_y +2*bias);//
        }
    }
    lo(EM);
    return endSequence();
}

bool Board::move(Poz& from, Poz& to) {
    return move(from.x, from.y, to.x, to.y);
}

bool Board::move(int from_x, int from_y, int to_x, int to_y, bool eat) { // eat = 0 -> white is eaten / eat = 1 -> black is eaten ... after all they are actually the same ...
    beginSequence();
    if (from_x == to_x || from_y == to_y || abs(from_x-to_x) == abs(from_y-to_y)) { // go st
        move(from_x, from_y, to_x+(from_x==to_x?0:((from_x-to_x)/abs(from_x-to_x))), to_y+(from_y==to_y?0:((from_y-to_y)/abs(from_y-to_y))));
        _move_(to_x*square + bias, to_y*square + 2*bias);//
        hi(EM);
        if (!eat) {//
            _move_(to_x*square + 2*bias, to_y*square + 3*bias);//
            if (whiteStack/2 < to_x) {
                _move_(to_x*square + 2*bias, 0);//
                _move_(whiteStack*bias, 0);//
            } else {
                _move_(whiteStack/2*square + square, to_y*square + 3*bias);//
                _move_(whiteStack/2*square + square, 0);//
                _move_(whiteStack*bias, 0);//
            }
            move(to_x+(from_x==to_x?0:((from_x-to_x)/abs(from_x-to_x))), to_y+(from_y==to_y?0:((from_y-to_y)/abs(from_y-to_y))), to_x, to_y);
            if (!movementFailed) whiteStack++;
        } else {
            _move_(to_x*square +2*bias, to_y*square + 3*bias);//
            if (blackStack/2 < to_x) {
                _move_(to_x*square + 2*bias, 8*square + 2*bias);//
                _move_(blackStack*bias, 8*square + 2*bias);//
            } else {
                _move_(blackStack/2*square + square, to_y*square + 3*bias);//
                _move_(blackStack/2*square + square, 8*square + 2*bias);//
                _move_(blackStack*bias, 8*square + 2*bias);//
            }
            move(to_x+(from_x==to_x?0:((from_x-to_x)/abs(from_x-to_x))), to_y+(from_y==to_y?0:((from_y-to_y)/abs(from_y-to_y))), to_x, to_y);
            if (!movementFailed) blackStack++;
        }
    } else { // knight
        lo(EM);
        _move_(from_x*square +bias, from_y*square +2*bias);//
        hi(EM);
        int dx = 0, dy = 0;
        if (abs(from_x-to_x) == 1) {
            _move_((from_x+to_x)*square/2 +bias, from_y*square +2*bias);//
            _move_((from_x+to_x)*square/2 +bias, to_y*square +2*bias);//
            lo(EM);
            dx = from_x > to_x ? -1 : 1; // dx = 1 指 from_x < to_x, 因此恢复时需要从小往大，也就是 -dx
        } else {
            _move_(from_x*square +bias, (from_y+to_y)*square/2 +2*bias);//
            _move_(to_x*square +bias, (from_y+to_y)*square/2 +2*bias);//
            lo(EM);
            dy = from_y > to_y ? -1 : 1;
        }
        _move_(to_x*square +bias, to_y*square +2*bias);//
        hi(EM);
        _move_(to_x*square +bias +(dx<0?-1:1)*bias, to_y*square +bias +(dy<0?-1:1)*bias+bias);//
        if (!eat) {//
            if (whiteStack/2 < (dx<0?to_x-1:to_x)) {
                _move_(to_x*square +bias +(dx<0?-1:1)*bias, 0);//
                _move_(whiteStack*bias, 0);//
            } else {
                _move_(whiteStack/2*square + square, to_y*square +bias +(dy<0?-1:1)*bias +bias);//
                _move_(whiteStack/2*square + square, 0);//
                _move_(whiteStack*bias, 0);//
            }
            lo(EM);
            _move_(to_x*square +bias -dx*bias, to_y*square +bias -dy*bias+bias);//
            hi(EM);
            _move_(to_x*square +bias, to_y*square +2*bias);//
            lo(EM);
            if (!movementFailed) whiteStack++;
        } else {
            if (blackStack/2 < (dx<0?to_x-1:to_x)) {
                _move_(to_x*square +bias +(dx<0?-1:1)*bias, 8*square + 2*bias);//
                _move_(blackStack*bias, 8*square + 2*bias);//
            } else {
                _move_(blackStack/2*square + square, to_y*square +2*bias +(dy<0?-1:1)*bias);//
                _move_(blackStack/2*square + square, 8*square + 2*bias);//
                _move_(blackStack*bias, 8*square + 2*bias);//
            }
            lo(EM);
            _move_(to_x*square +bias -dx*bias, to_y*square +2*bias -dy*bias);//
            hi(EM);
            _move_(to_x*square +bias, to_y*square +2*bias);//
            lo(EM);
            if (!movementFailed) blackStack++;
        }
    }
    lo(EM);
    return endSequence();
}

bool Board::move(Poz& from, Poz& to, bool eat) {
    return move(from.x, from.y, to.x, to.y, eat);
}

bool Board::moveR(int from_x, int from_y, int to_x, int to_y) {
    beginSequence();
    lo(EM);
    _move_(from_x*square + bias, from_y*square + 2*bias);//
    hi(EM);
    if (from_x == to_x) {
        if (from_x == 0) {
            _move_(square, from_y*square +2*bias +(from_y>to_y?-1:1)*bias);//
            _move_(square, to_y*square +2*bias +(from_y>to_y?1:-1)*bias);  // smooth ... he-he//
            _move_(bias, to_y*square +2*bias);//
        } else {
            _move_(from_x*square, from_y*square +2*bias +(from_y>to_y?-1:1)*bias);//
            _move_(from_x*square, to_y*square +2*bias +
                   (from_y>to_y?1:-1)*bias);//
            _move_(to_x*square +bias, to_y*square +2*bias);//
        }
    } else if (from_y == to_y) {
        if (from_y == 0) {
            _move_(from_x*square +bias +(from_x>to_x?-1:1)*bias, square+bias);//
            _move_(to_x*square +bias +(from_x>to_x?1:-1)*bias, square+bias);//
            _move_(to_x*square +bias, 2*bias);//
        } else {
            _move_(from_x*square +bias +(from_x>to_x?-1:1)*bias, from_y*square+bias);//
            _move_(to_x*square +bias +(from_x>to_x?1:-1)*bias, from_y*square+bias);//
            _move_(to_x*square +bias, to_y*square +2*bias);//
        }
    }
    lo(EM);
    return endSequence();
}

bool Board::moveR(Poz& from, Poz& to) {
    return moveR(from.x, from.y, to.x, to.y);
}

bool Board::moveE(int x, int y, bool eat) {
    beginSequence();
    lo(EM);
    _move_(x*square +bias, y*square +2*bias);//
    hi(EM);
    _move_(x*square +2*bias, y*square +3*bias);//
    if (!eat) {//
        if (whiteStack/2 < x) {
            _move_(x*square + 2*bias, 0);//
            _move_(whiteStack*bias, 0);//
        } else {
            _move_(whiteStack/2*square + square, y*square + 3*bias);//
            _move_(whiteStack/2*square + square, 0);//
            _move_(whiteStack*bias, 0);//
        }
        if (!movementFailed) whiteStack++;
    } else {
        if (blackStack/2 < x) {
            _move_(x*square + 2*bias, 8*square + 2*bias);//
            _move_(blackStack*bias, 8*square + 2*bias);//
        } else {
            _move_(blackStack/2*square + square, y*square + 3*bias);//
            _move_(blackStack/2*square + square, 8*square + 2*bias);//
            _move_(blackStack*bias, 8*square + 2*bias);//
        }
        if (!movementFailed) blackStack++;
    }
    lo(EM);
    return endSequence();
}

bool Board::returnToZero() {
    beginSequence();
    lo(EM);
    int_x = int_y = 0;
    lasterr_x = lasterr_y = 0;
    _move_(0, 0);
    int_x = int_y = 0;
    lasterr_x = lasterr_y = 0;
    return endSequence();
}

void Board::resetStack() {
    whiteStack = blackStack = 0;
}

bool Board::reset() {
    const bool homed = returnToZero();
    if (homed) resetStack();
    return homed;
}




































