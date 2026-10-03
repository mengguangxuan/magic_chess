#pragma once

// The phone owns Internet access, Doubao HTTPS and text-to-speech. The ESP32
// only sends compact move-quality events over BLE.
#define CHESS_ENABLE_BLUETOOTH 1

// Only confirmed moves at or above this engine-score loss notify the phone.
#define CHESS_TAUNT_MIN_LOSS_CP 120
