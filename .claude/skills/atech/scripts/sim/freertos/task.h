// freertos/task.h — host stand-in. vTaskDelay advances the virtual clock.
#pragma once
#include "FreeRTOS.h"
typedef void* TaskHandle_t;
typedef void (*TaskFunction_t)(void*);
inline void vTaskDelay(TickType_t ticks) { sim::delay_ms(ticks); }
inline TickType_t xTaskGetTickCount() { return (TickType_t)sim::now_ms(); }
inline void taskYIELD() {}
inline BaseType_t xTaskCreate(TaskFunction_t, const char* name, uint32_t, void*, UBaseType_t, TaskHandle_t* h) {
    sim::trace("rtos  ", std::string("xTaskCreate(") + (name ? name : "") + ") ignored: tasks do not run in the simulator");
    if (h) *h = nullptr;
    return pdPASS;
}
inline BaseType_t xTaskCreatePinnedToCore(TaskFunction_t f, const char* n, uint32_t s, void* p, UBaseType_t pr, TaskHandle_t* h, int) { return xTaskCreate(f, n, s, p, pr, h); }
inline void vTaskDelete(TaskHandle_t) {}
inline void vTaskSuspend(TaskHandle_t) {}
inline void vTaskResume(TaskHandle_t) {}
