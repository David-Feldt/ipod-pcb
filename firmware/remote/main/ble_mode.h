#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Run Bluetooth mode on the already-initialized board and UI, in its own task
// (controller init needs more stack than the main task has). Switching back
// to Wi-Fi is a restart.
void ble_mode_start(QueueHandle_t events);
