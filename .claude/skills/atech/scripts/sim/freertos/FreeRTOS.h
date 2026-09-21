// freertos/FreeRTOS.h — host stand-in. Ticks are milliseconds on the virtual clock.
#pragma once
#include <Arduino.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portTICK_PERIOD_MS 1
#define portMAX_DELAY 0xFFFFFFFFu
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define configTICK_RATE_HZ 1000
