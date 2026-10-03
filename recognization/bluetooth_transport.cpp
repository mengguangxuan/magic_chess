#include "bluetooth_transport.h"

#include <BLEDevice.h>
#include <BLE2902.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <string.h>

#include "receive.h"

QueueHandle_t bluetoothqueue = NULL;

namespace {
BLEServer* server = NULL;
BLECharacteristic* txCharacteristic = NULL;
volatile bool clientConnected = false;

class ChessServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* connectedServer) override
  {
    (void)connectedServer;
    clientConnected = true;
    Serial.println("Bluetooth client connected.");
  }

  void onDisconnect(BLEServer* disconnectedServer) override
  {
    clientConnected = false;
    Serial.println("Bluetooth client disconnected; advertising restarted.");
    disconnectedServer->startAdvertising();
  }
};

class ChessReceiveCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override
  {
    const String value = characteristic->getValue();
    if (value.length() == 0) {
      return;
    }

    BluetoothChunk chunk = {};
    chunk.length = static_cast<uint8_t>(
        value.length() > BLUETOOTH_CHUNK_SIZE
            ? BLUETOOTH_CHUNK_SIZE
            : value.length());
    memcpy(chunk.data, value.c_str(), chunk.length);

    if (xQueueSend(bluetoothqueue, &chunk, 0) != pdPASS) {
      Serial.println("Bluetooth receive queue full; packet dropped.");
    }
    if (value.length() > BLUETOOTH_CHUNK_SIZE) {
      Serial.println("Bluetooth packet truncated to 96 bytes.");
    }
  }
};
}

bool initBluetooth()
{
  bluetoothqueue = xQueueCreate(8, sizeof(BluetoothChunk));
  if (bluetoothqueue == NULL) {
    Serial.println("Failed to create Bluetooth receive queue.");
    return false;
  }

  BLEDevice::setMTU(100);
  BLEDevice::init(CHESS_BLE_DEVICE_NAME);

  server = BLEDevice::createServer();
  if (server == NULL) {
    Serial.println("Failed to create Bluetooth server.");
    return false;
  }
  server->setCallbacks(new ChessServerCallbacks());

  BLEService* service = server->createService(CHESS_BLE_SERVICE_UUID);
  txCharacteristic = service->createCharacteristic(
      CHESS_BLE_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  txCharacteristic->addDescriptor(new BLE2902());

  BLECharacteristic* rxCharacteristic = service->createCharacteristic(
      CHESS_BLE_RX_UUID,
      BLECharacteristic::PROPERTY_WRITE |
          BLECharacteristic::PROPERTY_WRITE_NR);
  rxCharacteristic->setCallbacks(new ChessReceiveCallbacks());

  service->start();
  server->getAdvertising()->addServiceUUID(CHESS_BLE_SERVICE_UUID);
  server->getAdvertising()->start();

  Serial.printf("Bluetooth ready: %s.\n", CHESS_BLE_DEVICE_NAME);
  return true;
}

bool bluetoothSend(const uint8_t* data, size_t length)
{
  if (!clientConnected || txCharacteristic == NULL || data == NULL ||
      length == 0) {
    return false;
  }

  txCharacteristic->setValue(data, length);
  txCharacteristic->notify();
  return true;
}

bool bluetoothSendTaunt(uint8_t blunderKind, int loss,
                        const char* playedMove, const char* bestMove)
{
  if (playedMove == NULL || bestMove == NULL) return false;
  if (loss < 0) loss = 0;
  if (loss > 9999) loss = 9999;

  // This compact event fits in the default 20-byte BLE payload for ordinary
  // four-character coordinate moves, so it does not depend on MTU negotiation.
  char event[32] = {0};
  const int length = snprintf(event, sizeof(event), "T|%u|%d|%s|%s",
                              static_cast<unsigned>(blunderKind), loss,
                              playedMove, bestMove);
  if (length <= 0 || static_cast<size_t>(length) >= sizeof(event)) return false;
  const bool sent = bluetoothSend(
      reinterpret_cast<const uint8_t*>(event), static_cast<size_t>(length));
  Serial.printf("Phone taunt event %s: %s\n", sent ? "sent" : "not sent",
                event);
  return sent;
}

void bluetoothTask(void* parameter)
{
  (void)parameter;
  char frame[RAW_MESSAGE_SIZE] = {0};
  uint8_t frameLength = 0;
  bool overflow = false;
  BluetoothChunk chunk = {};

  while (true) {
    if (xQueueReceive(bluetoothqueue, &chunk, portMAX_DELAY) != pdPASS) {
      continue;
    }

    for (uint8_t i = 0; i < chunk.length; ++i) {
      const uint8_t ch = chunk.data[i];
      if (ch == '\r' || ch == '\n') {
        continue;
      }
      if (ch == '.') {
        if (!overflow && frameLength > 0) {
          frame[frameLength] = '\0';
          queueProtocolFrame(frame, "Bluetooth");
        }
        memset(frame, 0, sizeof(frame));
        frameLength = 0;
        overflow = false;
        continue;
      }
      if (overflow) {
        continue;
      }
      if (frameLength < sizeof(frame) - 1) {
        frame[frameLength++] = static_cast<char>(ch);
      } else {
        memset(frame, 0, sizeof(frame));
        frameLength = 0;
        overflow = true;
        Serial.println("Bluetooth frame too long; discarded.");
      }
    }
  }
}
