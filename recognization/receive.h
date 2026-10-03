#pragma once
#include<HardwareSerial.h>
#include<Arduino.h>
#include"freertos/task.h"
#include"freertos/queue.h"
#include"freertos/timers.h"
#include"chess_types.h"
#define UART_BUF 512
#define RAW_MESSAGE_SIZE 64
#define UART1_RX 18
#define UART1_TX 17

extern HardwareSerial uart1;
extern QueueHandle_t voicequeue;
extern QueueHandle_t rawmsgqueue;
extern QueueHandle_t movequeue;
extern QueueHandle_t audioqueue;
extern TimerHandle_t order_timer;
extern volatile bool correctionRequestActive;
void uartreceive();
void ordertimercallback(TimerHandle_t order_timer);
void receiveTask(void* parameter);
bool queueProtocolFrame(const char* frame, const char* source);
bool queueAudioEvent(uint8_t event);
void audioTask(void* parameter);
