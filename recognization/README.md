# ESP32 voice chess controller

`recognization.ino` is the Arduino entry point. The UART parser runs in
`receiveTask`; `boardTask` is the only task allowed to access the embedded
TSCP engine and the physical chess board.

## Phone-offloaded Doubao taunts

Wi-Fi, HTTPS/TLS, Ark JSON handling and arbitrary-text speech now run in the
Android app. They are deliberately absent from the active ESP32 sketch to save
Flash. After a confirmed player move loses at least 120 centipawns, the board
sends a compact BLE notification containing the mistake kind, score loss,
played move and best move. The app requests a short comment from Doubao and
speaks it with Android text-to-speech. If the key, network or cloud request is
unavailable, the app immediately speaks a local fallback comment.

The previous on-board implementations are preserved outside the sketch in
`recognization_1/phone_offloaded_components`; Arduino does not compile them.

## Bluetooth transport

The ESP32-S3 advertises as `SmartChessboard` and exposes the Nordic UART
Service UUIDs. Write command data to RX characteristic
`6E400002-B5A3-F393-E0A9-E50E24DCCA9E` and subscribe to notifications on TX
characteristic `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`.

Phone-taunt notifications use ASCII `T|kind|loss|played|best`. Ordinary UART1
capture/checkmate frames remain the original nine-byte binary messages.

Bluetooth input uses exactly the same period-terminated frames as UART1, for
example `PIP.` followed by `Ce4.`. Bluetooth packets may contain partial or
multiple frames; `bluetoothTask` reassembles them and submits complete frames
to the same parser used by the voice module. Capture and checkmate output is
also identical: every committed 9-byte frame is written to UART1 and notified
over Bluetooth when a client is connected. Bluetooth disconnects automatically
restart advertising.

## Physical board execution

TSCP squares use files/ranks `1..8`; `PhysicalBoardController` converts them
to the `Board` coordinates `0..7`. Piece color is taken from TSCP's current
side, so captures use the correct white/black stack in both match modes.

- Normal moves call `Board::move`.
- Captures call the capture overload; captured white pieces use the white
  stack and captured black pieces use the black stack.
- Castling moves the king first, then calls `Board::moveR` to take the rook
  around the king.
- En passant calls `Board::moveE` for the captured pawn, then moves the
  capturing pawn normally.
- Promotion completes the pawn move and prints which piece must be installed
  manually. Voice promotions currently default to a queen.

Because the current `Board` interface cannot return a captured piece from its
stack, `undo` and ambiguous-move correction are refused if either half-move
contains a capture, en-passant capture, or promotion. This keeps the TSCP
position synchronized with the real board.

## SU-03T frames

Outgoing UART1 TX events follow `audio.txt`: each binary frame is nine bytes,
`AA 55 ID ID 00 00 00 55 AA`, with no ASCII encoding or added newline.
IDs: checkmate = 0x01, captured queen = 0x02, bishop = 0x03, knight = 0x04,
pawn = 0x05, rook = 0x06.
Events apply to either color in both modes and are sent after the Board
movement call completes. En passant reports a captured pawn; a promoted
piece reports its current type. A capture delivering checkmate sends the
capture frame followed by the checkmate frame. A dedicated queue preserves
their order and leaves a 30 ms gap between frames. The debug serial prints the
event name, all nine bytes, and the number of bytes written. Search, ordinary
check, and undo send no events.

Every frame ends with `.`.

- `PIK`, `PIQ`, `PIR`, `PIB`, `PIN`, `PIP`: select a piece, then send a square.
- `Ca1` ... `Ch8`: target square. A piece frame must arrive first, including
  `PIP` for a pawn; a bare square is rejected instead of being assumed a pawn.
- `scs`, `lcs`: king-side and queen-side castle.
- `mod`: correct the most recent ambiguous move; send its intended source square next.
- `undo`: undo the latest player/engine turn.
- `new` or `nes`: start a new game. `nes` is accepted for the current voice
  module frame `6E 65 73 2E`; the preferred `new.` bytes are
  `6E 65 77 2E`.
- `AIO`: enable the engine opponent and wait for a color selection.
- `WHT`: after `AIO`, start with the player as white.
- `BLK`: after `AIO`, start with the player as black; the engine immediately
  makes white's opening move.

At power-on, the controller is in two-player mode and accepts alternating
white/black moves without `AIO`. After `AIO`, no move is accepted until an
exact `WHT` or `BLK` frame arrives. In engine mode, `new` returns to the color
selection state so the real pieces can be reset before an engine opening move.
Every `new` command also releases the electromagnet, moves both axes to encoder
position `(0,0)`, clears the captured-piece stack counters, and resets TSCP to
the initial position. If homing fails, the new game is aborted and TSCP remains
unchanged. The chess pieces themselves must still be placed back on their
initial squares manually.

For target-square recognition errors, the controller uses `b <-> d`,
`c <-> e`, `a <-> e`, `b <-> g`, and `d <-> e`. It always tries the received
square first. Only when that target produces no legal move does it inspect the
confusable files on the same rank. It automatically substitutes a target only
when exactly one alternative is legal. Zero or multiple legal alternatives
leave the real board unchanged and require the command to be repeated.

The `mod.` frame is `6D 6F 64 2E`. It starts a 30-second window in which the
intended source-square frame must arrive. During this window the chess clock
is paused and an engine reply cannot start; after a correction or timeout,
normal engine play resumes. It only applies to the most recent
move where multiple same-type pieces could legally reach the target. `undo.`
and `new.` also need voice actions if those commands should be spoken rather
than injected over serial.

When more than one same-type piece can legally reach the requested target,
TSCP performs a short search and selects the candidate with the best score.
The complete selected move is printed. In engine mode this is followed by the
normal engine reply; in two-player mode the controller waits for the other
player. A later `mod` command rolls back the selected move and any automatic
engine reply, applies the requested source, and recalculates the reply when
engine mode is active.

The embedded build omits TSCP's desktop `main.c` and XBoard UI.
The original TSCP book.txt is embedded in `opening_book_data.h` as const data
mapped into ESP32 Flash. It needs no filesystem or separate upload. Engine
moves use legal book continuations weighted by line frequency; outside the
book (or after 26 half-moves) normal search takes over. Undo and correction
automatically use the updated move history. Voice ambiguity scoring bypasses
the book and continues to search. Search defaults are configured near the
top of `engine_adapter.cpp`.

Each motor waypoint has a 20-second timeout and encoder safety bounds. On a
failure both PWM outputs are stopped, the electromagnet is released, and the
move is reported back so the just-applied TSCP move can be rolled back. Motor
position diagnostics are rate-limited to one line per 100 ms; set
`CHESSBOARD_MOTION_DEBUG` to `0` at compile time to disable them.

## Move-quality classification

For every accepted human move, TSCP performs the same short search before and
after the move (160 ms, depth 4, with the opening book disabled). The
opponent-relative post-move score is negated, and the difference is reported
in centipawns. Thresholds are: below 50 sound, 50 inaccuracy, 120 mistake, 250
blunder, and 500 disaster. Strong rules additionally identify allowing forced
mate, missing forced mate, hanging a queen or rook, missing a queen or rook
capture, and likely material loss. Everything else remains `generic` so the
program does not invent a tactical explanation that the shallow search cannot
prove. Ambiguous voice moves are marked `provisional` because a later `mod`
command may replace them.

## WT3000T-M01+ TTS taunts

Bad-move comments returned by Doubao are now queued to a dedicated WT3000T
task and synthesized over UART2. This path is independent of UART1, which is
still shared by the voice-recognition input and the existing capture/checkmate
audio protocol. If Wi-Fi, Ark, or the API key is unavailable, a short local
comment selected from the TSCP move-quality result is spoken instead.

Default wiring (change `TTS_UART_RX` and `TTS_UART_TX` in `tts_output.h` if
the PCB uses different free pins):

- WT3000T pin 1 `5V` -> regulated 5 V supply
- WT3000T pin 2 `GND` -> ESP32 GND (common ground is required)
- WT3000T pin 3 `TX` -> ESP32 GPIO15 (`TTS_UART_RX`)
- WT3000T pin 4 `RX` -> ESP32 GPIO16 (`TTS_UART_TX`)
- WT3000T `SPK-` / `SPK+` -> 8-ohm speaker

The UART is configured for the module default of 9600 baud, 8 data bits, no
parity, and 1 stop bit. Set `CHESS_TTS_STARTUP_TEST` to `1` in
`network_config.h` for one upload to verify wiring; set it back to `0` for
normal play.
