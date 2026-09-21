// freertos/queue.h — host stand-in. Queues accept and drop; receives time out.
#pragma once
#include "FreeRTOS.h"
typedef void* QueueHandle_t;
inline QueueHandle_t xQueueCreate(UBaseType_t, UBaseType_t) { static int dummy; return &dummy; }
inline BaseType_t xQueueSend(QueueHandle_t, const void*, TickType_t) { return pdTRUE; }
inline BaseType_t xQueueSendToBack(QueueHandle_t, const void*, TickType_t) { return pdTRUE; }
inline BaseType_t xQueueReceive(QueueHandle_t, void*, TickType_t) { return pdFALSE; }
inline UBaseType_t uxQueueMessagesWaiting(QueueHandle_t) { return 0; }
inline void vQueueDelete(QueueHandle_t) {}
