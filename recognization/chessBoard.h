#ifndef __CHESSBOARD_HXX__
#define __CHESSBOARD_HXX__

#include <stdint.h>

#define MXA 5
#define MXB 4
#define MX1 7
#define MX2 6
#define EX 39
#define MYA 1
#define MYB 2
#define EY 42
#define MY1 41
#define MY2 40

#define EM 14

struct Poz {
    int x;
    int y;
};

class Board {
public:
    Board() = default;
    ~Board() = default;
    void init();
    bool reset();

    bool move(Poz&, Poz&);
    bool move(Poz&, Poz&, bool);
    bool move(int, int, int, int);
    bool move(int, int, int, int, bool);

    bool moveR(Poz&, Poz&);
    bool moveR(int, int, int, int);

    bool moveE(int, int, bool);

    bool returnToZero();
    void resetStack();
private:
    int whiteStack = 0;
    int blackStack = 0;
    int dest_x = 0, dest_y = 0;
    bool movementFailed = false;
    uint8_t sequenceDepth = 0;
    void beginSequence();
    bool endSequence();
    void failMovement(const char* reason, int targetX, int targetY);
    void _move_(int, int);
    void _move_(Poz&);
};


#endif
