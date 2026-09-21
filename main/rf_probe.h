#pragma once

#include <stdint.h>
#include "esp_err.h"

/*
 * Initializes the ESP32-C5 5 GHz Wi-Fi frontend for the R0 experiment.
 *
 * R0 intentionally uses channel 36 (5180 MHz), a normal non-DFS Wi-Fi
 * channel, rather than the 5.8 GHz FPV frequencies used by C5VRX.
 */
esp_err_t c5x_rf_start(void);

/*
 * Arms the C5 modem dump path in the same proven pre-trigger configuration
 * used to expose live MODEM_DIAG Q4/I4 in C5VRX.
 *
 * Important: this configuration uses TX_START as its trigger. In C5X R0,
 * TX_START is expected to occur during the TX-burst record. The before/after
 * dump registers are therefore part of every capture result.
 */
void c5x_rf_arm_iq_source(void);

uint8_t c5x_rf_channel(void);
