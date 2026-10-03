#include "receive.h"
#include "board.h"
#include "bluetooth_transport.h"
#include "network_config.h"

hw_timer_t * clockTimer = NULL;
volatile char count = 0;
volatile bool clockRunning = false;
volatile bool clockWhiteTurn = true;

#define hi(x) digitalWrite(x, HIGH)
#define lo(x) digitalWrite(x, LOW)

void IRAM_ATTR timerISR() {
  if (!clockRunning) return;
  if (clockWhiteTurn) {
    switch(count) {
      case 0:
        lo(PIN_WB);
        count ++;
        return;
      case 1:
        hi(PIN_WA);
        count ++;
        return;
      case 2:
        lo(PIN_WA);
        count ++;
        return;
      case 3:
        hi(PIN_WB);
        count = 0;
        return;
      default:
        lo(PIN_WB);
        lo(PIN_WA);
        count = 0;
        return;
    }
  } else {
        switch(count) {
      case 0:
        lo(PIN_BB);
        count ++;
        return;
      case 1:
        hi(PIN_BA);
        count ++;
        return;
      case 2:
        lo(PIN_BA);
        count ++;
        return;
      case 3:
        hi(PIN_BB);
        count = 0;
        return;
      default:
        lo(PIN_BB);
        lo(PIN_BA);
        count = 0;
        return;
    }
  }
}

#undef hi
#undef lo

void init_kernel()
{
  // UART
  uart1.begin(115200,SERIAL_8N1,UART1_RX,UART1_TX);
  uart1.onReceive(uartreceive);
  Serial.begin(115200);

  // Queues
  voicequeue=xQueueCreate(UART_BUF,sizeof(uint8_t));
  rawmsgqueue=xQueueCreate(5,RAW_MESSAGE_SIZE);
  movequeue=xQueueCreate(5,sizeof(VoiceCommand));
  audioqueue=xQueueCreate(8,sizeof(uint8_t));

  // Timer
  order_timer=xTimerCreate("order_timer",pdMS_TO_TICKS(3000),pdFALSE,NULL,ordertimercallback);

  if (voicequeue == NULL || rawmsgqueue == NULL || movequeue == NULL ||
      audioqueue == NULL || order_timer == NULL) {
    Serial.println("Failed to create queues or timer.");
    while (true) {
      delay(1000);
    }
  }

#if CHESS_ENABLE_BLUETOOTH
  if (!initBluetooth()) {
    Serial.println("Bluetooth initialization failed.");
    while (true) {
      delay(1000);
    }
  }
#else
  Serial.println("Bluetooth disabled for standalone Wi-Fi test.");
#endif

  // Clock
  pinMode(PIN_WA, OUTPUT);
  pinMode(PIN_WB, OUTPUT);
  pinMode(PIN_BA, OUTPUT);
  pinMode(PIN_BB, OUTPUT);
  clockTimer = timerBegin(1e6);
  timerAttachInterrupt(clockTimer, &timerISR);
  timerAlarm(clockTimer, 20000, true, 0);
  timerStop(clockTimer);
  digitalWrite(PIN_WA, LOW);
  digitalWrite(PIN_WB, LOW);
  digitalWrite(PIN_BA, LOW);
  digitalWrite(PIN_BB, LOW);
}

void init_task()
{
  xTaskCreate(receiveTask,"receiveTask",4096,NULL,3,NULL);
  xTaskCreate(boardTask,"boardTask",12288,NULL,2,NULL);
  xTaskCreate(audioTask,"audioTask",3072,NULL,2,NULL);
#if CHESS_ENABLE_BLUETOOTH
  xTaskCreate(bluetoothTask,"bluetoothTask",4096,NULL,3,NULL);
#endif
}
void setup()
{
  // put your setup code here, to run once:
  init_kernel();
  init_task();
}

void loop() 
{
  vTaskDelay(pdMS_TO_TICKS(1000));
}
