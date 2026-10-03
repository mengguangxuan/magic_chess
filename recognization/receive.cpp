#include "receive.h"

#include "bluetooth_transport.h"
#include "network_config.h"

#include <string.h>

QueueHandle_t voicequeue = NULL;
QueueHandle_t rawmsgqueue = NULL;
QueueHandle_t movequeue = NULL;
QueueHandle_t audioqueue = NULL;
TimerHandle_t order_timer = NULL;
HardwareSerial uart1(1);
volatile bool correctionRequestActive = false;

static char lineBuf[RAW_MESSAGE_SIZE] = {0};
static uint8_t lineIdx = 0;
static bool lineOverflow = false;
static PieceType pendingPiece = DefaultPiece;
static bool waitingForTarget = false;
static bool waitingForCorrectionSource = false;
static const uint32_t MOVE_COMMAND_TIMEOUT_MS = 3000;
static const uint32_t CORRECTION_COMMAND_TIMEOUT_MS = 30000;

static void clearPendingInput()
{
  pendingPiece = DefaultPiece;
  waitingForTarget = false;
  waitingForCorrectionSource = false;
}

static void startCommandTimer(uint32_t timeoutMs)
{
  if (xTimerChangePeriod(order_timer, pdMS_TO_TICKS(timeoutMs),
                         pdMS_TO_TICKS(10)) != pdPASS) {
    Serial.println("Failed to start voice command timer.");
  }
}

static bool sendVoiceCommand(VoiceCommandType command, PieceType piece,
                             uint8_t file = 0, uint8_t rank = 0)
{
  VoiceCommand message = {command, piece, file, rank};
  if (xQueueSend(movequeue, &message, pdMS_TO_TICKS(20)) != pdPASS) {
    Serial.println("Move queue full; command dropped.");
    return false;
  }
  return true;
}

bool queueProtocolFrame(const char* frame, const char* source)
{
  if (frame == NULL || frame[0] == '\0') {
    return false;
  }

  const size_t length = strnlen(frame, RAW_MESSAGE_SIZE);
  if (length >= RAW_MESSAGE_SIZE) {
    Serial.printf("%s frame too long; discarded.\n", source);
    return false;
  }

  char queuedFrame[RAW_MESSAGE_SIZE] = {0};
  memcpy(queuedFrame, frame, length);
  if (xQueueSend(rawmsgqueue, queuedFrame, 0) != pdPASS) {
    Serial.printf("Raw message queue full; %s frame dropped.\n", source);
    return false;
  }

  Serial.printf("%s frame: %s\n", source, queuedFrame);
  return true;
}

void uartreceive()
{
  // ESP32 HardwareSerial invokes onReceive from its UART event task, not an ISR.
  while (uart1.available() > 0) {
    uint8_t ch = static_cast<uint8_t>(uart1.read());
    if (xQueueSend(voicequeue, &ch, 0) != pdPASS) {
      Serial.println("UART receive queue full; byte dropped.");
      break;
    }
  }
}

void ordertimercallback(TimerHandle_t timer)
{
  (void)timer;
  // rawmsgqueue items are 64 bytes, so always enqueue a full-size object.
  static const char timeoutMessage[RAW_MESSAGE_SIZE] = "ETO";
  xQueueSend(rawmsgqueue, timeoutMessage, 0);
}

static void receiveRawMessage()
{
  uint8_t ch;
  while (xQueueReceive(voicequeue, &ch, 0) == pdPASS) {
    if (ch == '\r' || ch == '\n') {
      continue;
    }

    if (ch == '.') {
      if (!lineOverflow && lineIdx > 0) {
        lineBuf[lineIdx] = '\0';
        queueProtocolFrame(lineBuf, "Voice");
      }
      memset(lineBuf, 0, sizeof(lineBuf));
      lineIdx = 0;
      lineOverflow = false;
      continue;
    }

    if (lineOverflow) {
      continue;
    }

    if (lineIdx < sizeof(lineBuf) - 1) {
      lineBuf[lineIdx++] = static_cast<char>(ch);
    } else {
      memset(lineBuf, 0, sizeof(lineBuf));
      lineIdx = 0;
      lineOverflow = true;
      Serial.println("Voice frame too long; discarded.");
    }
  }
}

static PieceType decodePiece(char code)
{
  switch (code) {
    case 'K': return King;
    case 'Q': return Queen;
    case 'R': return Rook;
    case 'B': return Bishop;
    case 'N': return Knight;
    case 'P': return Pawn;
    default: return DefaultPiece;
  }
}

static bool decodeSquare(const char* rawmsg, uint8_t* file, uint8_t* rank)
{
  if (rawmsg[0] != 'C' || rawmsg[1] < 'a' || rawmsg[1] > 'h' ||
      rawmsg[2] < '1' || rawmsg[2] > '8' || rawmsg[3] != '\0') {
    return false;
  }

  *file = static_cast<uint8_t>(rawmsg[1] - 'a' + 1);
  *rank = static_cast<uint8_t>(rawmsg[2] - '0');
  return true;
}

static void processRawMessage()
{
  char rawmsg[RAW_MESSAGE_SIZE] = {0};
  if (xQueueReceive(rawmsgqueue, rawmsg, pdMS_TO_TICKS(10)) != pdPASS) {
    return;
  }

  if (strcmp(rawmsg, "ETO") == 0) {
    const bool correctionTimedOut = waitingForCorrectionSource;
    if (correctionTimedOut) {
      if (sendVoiceCommand(CorrectionWaitTimedOut, DefaultPiece)) {
        clearPendingInput();
        Serial.println("Correction command timed out after 30 seconds.");
      } else {
        startCommandTimer(CORRECTION_COMMAND_TIMEOUT_MS);
        Serial.println("Could not queue the correction timeout; correction "
                       "hold remains active.");
      }
    } else {
      clearPendingInput();
      Serial.println("Voice command timed out.");
    }
    return;
  }

  if (strcmp(rawmsg, "mod") == 0 || strcmp(rawmsg, "MOD") == 0) {
    if (waitingForCorrectionSource) {
      startCommandTimer(CORRECTION_COMMAND_TIMEOUT_MS);
      Serial.println("Correction wait restarted; waiting up to 30 seconds for "
                     "the source square.");
      return;
    }
    clearPendingInput();
    waitingForCorrectionSource = true;
    correctionRequestActive = true;
    if (!sendVoiceCommand(BeginCorrectionWait, DefaultPiece)) {
      correctionRequestActive = false;
      clearPendingInput();
      return;
    }
    startCommandTimer(CORRECTION_COMMAND_TIMEOUT_MS);
    Serial.println("Correction requested; waiting up to 30 seconds for the "
                   "source square.");
    return;
  }

  if (waitingForCorrectionSource) {
    uint8_t correctionFile = 0;
    uint8_t correctionRank = 0;
    if (decodeSquare(rawmsg, &correctionFile, &correctionRank)) {
      xTimerStop(order_timer, 0);
      if (sendVoiceCommand(CorrectSourceSquare, DefaultPiece,
                           correctionFile, correctionRank)) {
        clearPendingInput();
      } else {
        startCommandTimer(CORRECTION_COMMAND_TIMEOUT_MS);
        Serial.println("Could not queue the correction square; correction "
                       "hold remains active.");
      }
    } else {
      Serial.println("Correction is waiting for a source-square frame; other "
                     "commands are ignored.");
    }
    return;
  }

  if (strcmp(rawmsg, "undo") == 0) {
    clearPendingInput();
    xTimerStop(order_timer, 0);
    sendVoiceCommand(UndoTurn, DefaultPiece);
    return;
  }

  if (strcmp(rawmsg, "new") == 0 || strcmp(rawmsg, "nes") == 0) {
    clearPendingInput();
    xTimerStop(order_timer, 0);
    if (strcmp(rawmsg, "nes") == 0) {
      Serial.println("Voice alias nes mapped to new.");
    }
    sendVoiceCommand(StartNewGame, DefaultPiece);
    return;
  }

  if (strcmp(rawmsg, "AIO") == 0) {
    clearPendingInput();
    xTimerStop(order_timer, 0);
    sendVoiceCommand(EnableEngineOpponent, DefaultPiece);
    return;
  }

  if (strcmp(rawmsg, "WHT") == 0) {
    clearPendingInput();
    xTimerStop(order_timer, 0);
    sendVoiceCommand(ChoosePlayerWhite, DefaultPiece);
    return;
  }

  if (strcmp(rawmsg, "BLK") == 0) {
    clearPendingInput();
    xTimerStop(order_timer, 0);
    sendVoiceCommand(ChoosePlayerBlack, DefaultPiece);
    return;
  }

  if (strcmp(rawmsg, "scs") == 0) {
    clearPendingInput();
    xTimerStop(order_timer, 0);
    sendVoiceCommand(CastleKingSide, King);
    return;
  }

  if (strcmp(rawmsg, "lcs") == 0) {
    clearPendingInput();
    xTimerStop(order_timer, 0);
    sendVoiceCommand(CastleQueenSide, King);
    return;
  }

  if (rawmsg[0] == 'P' && rawmsg[1] == 'I') {
    // Accept both the current PIK form and the older PI:K form.
    const char pieceCode = rawmsg[2] == ':' ? rawmsg[3] : rawmsg[2];
    PieceType piece = decodePiece(pieceCode);
    if (piece == DefaultPiece) {
      Serial.println("Unknown piece frame.");
      return;
    }

    pendingPiece = piece;
    waitingForTarget = true;
    waitingForCorrectionSource = false;
    startCommandTimer(MOVE_COMMAND_TIMEOUT_MS);
    return;
  }

  uint8_t file = 0;
  uint8_t rank = 0;
  if (decodeSquare(rawmsg, &file, &rank)) {
    xTimerStop(order_timer, 0);

    if (!waitingForTarget) {
      Serial.println("Square ignored: send a piece frame first (use PIP for "
                     "a pawn).");
      clearPendingInput();
      return;
    }

    sendVoiceCommand(MoveToSquare, pendingPiece, file, rank);
    clearPendingInput();
    return;
  }

  // The wake frame is informational and should not be treated as an error.
  if (strcmp(rawmsg, "wak") != 0) {
    Serial.printf("Ignored voice frame: %s\n", rawmsg);
  }
}

static const char* audioEventName(uint8_t event)
{
  switch (event) {
    case 1: return "checkmate";
    case 2: return "queen captured";
    case 3: return "bishop captured";
    case 4: return "knight captured";
    case 5: return "pawn captured";
    case 6: return "rook captured";
    default: return "unknown";
  }
}

bool queueAudioEvent(uint8_t event)
{
  if (event < 1 || event > 6 || audioqueue == NULL) {
    Serial.printf("Invalid audio event: %u.\n", event);
    return false;
  }
  if (xQueueSend(audioqueue, &event, pdMS_TO_TICKS(20)) != pdPASS) {
    Serial.printf("Audio queue full; event %u dropped.\n", event);
    return false;
  }
  return true;
}

void audioTask(void* parameter)
{
  (void)parameter;
  uint8_t event = 0;
  while (true) {
    if (xQueueReceive(audioqueue, &event, portMAX_DELAY) != pdPASS) {
      continue;
    }

    const uint8_t frame[] = {
      0xAA, 0x55, event, event, 0x00, 0x00, 0x00, 0x55, 0xAA
    };
    const size_t written = uart1.write(frame, sizeof(frame));
    uart1.flush();
#if CHESS_ENABLE_BLUETOOTH
    const bool bluetoothWritten = bluetoothSend(frame, sizeof(frame));
#else
    const bool bluetoothWritten = false;
#endif
    Serial.printf("Audio TX %s: AA 55 %02X %02X 00 00 00 55 AA "
                  "(UART %u/9 bytes, BLE %s).\n",
                  audioEventName(event), event, event,
                  static_cast<unsigned>(written),
                  bluetoothWritten ? "sent" : "not connected");
    if (written != sizeof(frame)) {
      Serial.println("Audio UART write was incomplete.");
    }

    // Keep consecutive capture/checkmate frames distinct for the audio module.
    vTaskDelay(pdMS_TO_TICKS(30));
  }
}

void receiveTask(void* parameter)
{
  (void)parameter;
  memset(lineBuf, 0, sizeof(lineBuf));
  lineIdx = 0;
  lineOverflow = false;
  clearPendingInput();

  while (true) {
    receiveRawMessage();
    processRawMessage();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}
