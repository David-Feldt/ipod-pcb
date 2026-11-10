// Captive DNS: answers every A query with 192.168.4.1 (the hotspot address).
#pragma once
#include "esp_err.h"

esp_err_t dns_start(void);
void dns_stop(void);
