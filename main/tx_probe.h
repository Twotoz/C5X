#pragma once

#include <stdint.h>
#include "esp_err.h"

typedef struct {
    uint32_t attempted;
    uint32_t succeeded;
    esp_err_t last_error;
} c5x_tx_probe_stats_t;

/*
 * Emits a short burst of broadcast vendor-action frames on the already
 * configured 5 GHz channel. No association or external receiver is required.
 */
esp_err_t c5x_tx_probe_burst(void *arg);
