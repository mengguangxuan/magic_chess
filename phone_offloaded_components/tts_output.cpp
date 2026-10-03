#include "tts_output.h"

#include <HardwareSerial.h>
#include <string.h>

#include "network_config.h"

namespace {
const uint8_t TTS_QUEUE_LENGTH = 3;
// A WT3000T command item has a one-byte data length. The model is prompted to
// return at most 24 Chinese characters, so 240 UTF-8 bytes leaves ample room.
const size_t TTS_MAX_TEXT_BYTES = 240;
const uint16_t TTS_SYNTHESIZE_COMMAND = 0x03E8;
const uint32_t TTS_COMPLETION_TIMEOUT_MS = 15000;

struct TtsMessage {
  char text[TTS_MAX_TEXT_BYTES + 1];
};

HardwareSerial ttsUart(2);
QueueHandle_t ttsQueue = NULL;
uint8_t sequenceNumber = 1;

size_t utf8PrefixLength(const char* text)
{
  const size_t fullLength = strlen(text);
  if (fullLength <= TTS_MAX_TEXT_BYTES) return fullLength;

  size_t length = TTS_MAX_TEXT_BYTES;
  // Do not leave an incomplete UTF-8 character at the end of the frame.
  while (length > 0 &&
         (static_cast<uint8_t>(text[length]) & 0xC0) == 0x80) {
    --length;
  }
  return length;
}

bool sendSynthesizeFrame(const char* text)
{
  const size_t textLength = strlen(text);
  if (textLength == 0 || textLength > TTS_MAX_TEXT_BYTES) return false;

  // The documented frame length counts bytes from the two length bytes
  // through the checksum, excluding only 0x7E and 0xEF.
  const uint16_t frameLength = static_cast<uint16_t>(textLength + 9);
  uint8_t checksum = 0;

  ttsUart.write(0x7E);
  const uint8_t lengthHigh = static_cast<uint8_t>(frameLength >> 8);
  const uint8_t lengthLow = static_cast<uint8_t>(frameLength);
  const uint8_t sequence = sequenceNumber++;
  const uint8_t commandHigh =
      static_cast<uint8_t>(TTS_SYNTHESIZE_COMMAND >> 8);
  const uint8_t commandLow =
      static_cast<uint8_t>(TTS_SYNTHESIZE_COMMAND);
  const uint8_t dataLength = static_cast<uint8_t>(textLength);

  const uint8_t header[] = {
    lengthHigh, lengthLow, sequence, 0x00, 0x03,
    commandHigh, commandLow, dataLength
  };
  for (size_t i = 0; i < sizeof(header); ++i) {
    ttsUart.write(header[i]);
    checksum = static_cast<uint8_t>(checksum + header[i]);
  }
  for (size_t i = 0; i < textLength; ++i) {
    const uint8_t byte = static_cast<uint8_t>(text[i]);
    ttsUart.write(byte);
    checksum = static_cast<uint8_t>(checksum + byte);
  }
  ttsUart.write(checksum);
  ttsUart.write(0xEF);
  ttsUart.flush();

  Serial.printf("TTS TX: %u UTF-8 bytes, sequence=%u, checksum=%02X.\n",
                static_cast<unsigned>(textLength), sequence, checksum);
  return true;
}

bool waitForCompletion()
{
  const uint32_t startedAt = millis();
  bool receivedFrame = false;
  while (millis() - startedAt < TTS_COMPLETION_TIMEOUT_MS) {
    while (ttsUart.available() > 0) {
      const int byte = ttsUart.read();
      if (byte == 0x7E) receivedFrame = true;
      if (receivedFrame && byte == 0xEF) return true;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return false;
}
}

bool initTtsOutput()
{
#if !CHESS_ENABLE_TTS
  Serial.println("WT3000T TTS disabled in network_config.h.");
  return false;
#else
  ttsQueue = xQueueCreate(TTS_QUEUE_LENGTH, sizeof(TtsMessage));
  if (ttsQueue == NULL) {
    Serial.println("WT3000T TTS disabled: failed to create text queue.");
    return false;
  }

  ttsUart.begin(TTS_UART_BAUD, SERIAL_8N1, TTS_UART_RX, TTS_UART_TX);
  Serial.printf("WT3000T TTS ready on UART2: RX=%d, TX=%d, %d baud.\n",
                TTS_UART_RX, TTS_UART_TX, TTS_UART_BAUD);

#if CHESS_TTS_STARTUP_TEST
  queueTtsText("语音合成模块连接成功");
#endif
  return true;
#endif
}

bool queueTtsText(const char* utf8Text)
{
  if (ttsQueue == NULL || utf8Text == NULL || utf8Text[0] == '\0') {
    return false;
  }

  TtsMessage message = {};
  const size_t length = utf8PrefixLength(utf8Text);
  memcpy(message.text, utf8Text, length);
  message.text[length] = '\0';

  if (xQueueSend(ttsQueue, &message, 0) == pdPASS) {
    return true;
  }

  // Prefer the latest chess comment over one that is no longer timely.
  TtsMessage discarded = {};
  xQueueReceive(ttsQueue, &discarded, 0);
  if (xQueueSend(ttsQueue, &message, 0) == pdPASS) {
    Serial.println("TTS queue full: oldest sentence replaced.");
    return true;
  }

  Serial.println("TTS queue full: sentence dropped.");
  return false;
}

void ttsOutputTask(void* parameter)
{
  (void)parameter;
  TtsMessage message = {};
  while (true) {
    if (xQueueReceive(ttsQueue, &message, portMAX_DELAY) != pdPASS) {
      continue;
    }

    // Discard old acknowledgements before issuing the next command.
    while (ttsUart.available() > 0) ttsUart.read();
    if (!sendSynthesizeFrame(message.text)) {
      Serial.println("TTS frame was not sent.");
      continue;
    }
    // The M01 connector does not expose BUSY. Its completion response keeps
    // queued taunts from interrupting one another. A timeout still lets the
    // task recover if TX/RX is disconnected.
    if (!waitForCompletion()) {
      Serial.println("TTS completion response timed out after 15 seconds.");
    }
  }
}

