#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "capture_format.h"
#include "iq_capture.h"
#include "rf_probe.h"
#include "tx_probe.h"

static const char *TAG = "c5x";

static void fatal(const char *step, esp_err_t err)
{
    ESP_LOGE(TAG, "%s failed: %s", step, esp_err_to_name(err));
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000u));
    }
}

void app_main(void)
{
    ESP_LOGW(TAG, "C5X R0 - single-C5 TX/RX overlap probe");
    ESP_LOGW(TAG, "Target hardware: XIAO ESP32-C5");
    ESP_LOGW(TAG, "This firmware measures feasibility; it does not claim radar operation.");

    esp_err_t err = c5x_rf_start();
    if (err != ESP_OK) fatal("RF init", err);

    err = c5x_capture_init();
    if (err != ESP_OK) fatal("capture init", err);

    err = c5x_capture_store_reset();
    if (err != ESP_OK) fatal("capture partition erase", err);

    ESP_LOGI(TAG, "Starting bounded experiment in 3 seconds...");
    vTaskDelay(pdMS_TO_TICKS(3000u));

    c5x_capture_header_t baseline;
    err = c5x_capture_run(C5X_CAPTURE_BASELINE, NULL, NULL, &baseline);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Baseline capture returned %s; persisting diagnostics anyway",
                 esp_err_to_name(err));
    }

    size_t bytes = 0u;
    const uint8_t *data = c5x_capture_data(&bytes);
    esp_err_t store_err =
        c5x_capture_store_record(&baseline, data, bytes);
    if (store_err != ESP_OK) fatal("baseline store", store_err);

    vTaskDelay(pdMS_TO_TICKS(100u));

    c5x_tx_probe_stats_t tx_stats = {0};
    c5x_capture_header_t tx;
    err = c5x_capture_run(C5X_CAPTURE_TX_BURST,
                          c5x_tx_probe_burst,
                          &tx_stats,
                          &tx);

    tx.tx_attempted = tx_stats.attempted;
    tx.tx_ok = tx_stats.succeeded;

    data = c5x_capture_data(&bytes);
    store_err = c5x_capture_store_record(&tx, data, bytes);
    if (store_err != ESP_OK) fatal("TX capture store", store_err);

    ESP_LOGW(TAG, "R0 COMPLETE");
    ESP_LOGW(TAG,
             "BASE: status=%s hash=%08x power=%llu sentinel=%u",
             esp_err_to_name((esp_err_t)baseline.capture_status),
             (unsigned)baseline.fnv1a,
             (unsigned long long)baseline.power_sum,
             (unsigned)baseline.sentinel_bytes);
    ESP_LOGW(TAG,
             "TX:   status=%s hook=%s frames=%u/%u hash=%08x power=%llu sentinel=%u",
             esp_err_to_name((esp_err_t)tx.capture_status),
             esp_err_to_name((esp_err_t)tx.hook_status),
             (unsigned)tx.tx_ok,
             (unsigned)tx.tx_attempted,
             (unsigned)tx.fnv1a,
             (unsigned long long)tx.power_sum,
             (unsigned)tx.sentinel_bytes);

    ESP_LOGW(TAG,
             "Read partition 'c5xcap' with parttool.py, then run "
             "python tools/analyze_capture.py c5xcap.bin");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000u));
    }
}
