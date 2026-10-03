#pragma once

#include <Arduino.h>
#include "freertos/queue.h"

#ifndef CHESS_BLE_DEVICE_NAME
#define CHESS_BLE_DEVICE_NAME "SmartChessboard"
#endif

#define CHESS_BLE_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHESS_BLE_RX_UUID      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHESS_BLE_TX_UUID      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

static const size_t BLUETOOTH_CHUNK_SIZE = 96;

struct BluetoothChunk {
  uint8_t length;
  uint8_t data[BLUETOOTH_CHUNK_SIZE];
};

extern QueueHandle_t bluetoothqueue;

bool initBluetooth();
bool bluetoothSend(const uint8_t* data, size_t length);
bool bluetoothSendTaunt(uint8_t blunderKind, int loss,
                        const char* playedMove, const char* bestMove);
void bluetoothTask(void* parameter);
