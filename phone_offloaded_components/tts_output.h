#pragma once

#include <Arduino.h>

// WT3000T-M01+ uses a 3.3 V TTL UART at 9600 baud by default.
// UART2 keeps its protocol separate from the voice/audio UART1 bus.
#define TTS_UART_RX 15
#define TTS_UART_TX 16
#define TTS_UART_BAUD 9600

bool initTtsOutput();
bool queueTtsText(const char* utf8Text);
void ttsOutputTask(void* parameter);

