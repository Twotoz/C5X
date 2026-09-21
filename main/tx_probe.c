#include "tx_probe.h"

#include <string.h>

#include "esp_log.h"
#include "esp_wifi.h"

#define C5X_TX_FRAME_BYTES 400u

static const char *TAG = "c5x_tx";

esp_err_t c5x_tx_probe_burst(void *arg)
{
    c5x_tx_probe_stats_t *stats = (c5x_tx_probe_stats_t *)arg;
    if (!stats) return ESP_ERR_INVALID_ARG;

    memset(stats, 0, sizeof(*stats));
    stats->last_error = ESP_OK;

    static uint8_t frame[C5X_TX_FRAME_BYTES];
    memset(frame, 0, sizeof(frame));

    /*
     * IEEE 802.11 management Action frame.
     * Frame control: subtype Action, type Management.
     */
    frame[0] = 0xd0u;
    frame[1] = 0x00u;

    /* DA = broadcast */
    memset(&frame[4], 0xff, 6u);

    uint8_t mac[6];
    esp_err_t err = esp_wifi_get_mac(WIFI_IF_STA, mac);
    if (err != ESP_OK) {
        stats->last_error = err;
        return err;
    }

    /* SA = our station MAC; BSSID = broadcast for this lab probe. */
    memcpy(&frame[10], mac, 6u);
    memset(&frame[16], 0xff, 6u);

    /* Vendor-specific action category + local experimental identifier. */
    frame[24] = 127u;
    frame[25] = 0x02u;
    frame[26] = 0xc5u;
    frame[27] = 0x58u; /* "X" */

    uint32_t prbs = 0x43535830u;
    for (size_t i = 28u; i < sizeof(frame); ++i) {
        prbs ^= prbs << 13u;
        prbs ^= prbs >> 17u;
        prbs ^= prbs << 5u;
        frame[i] = (uint8_t)prbs;
    }

    for (unsigned n = 0u; n < CONFIG_C5X_TX_BURST_FRAMES; ++n) {
        stats->attempted++;
        err = esp_wifi_80211_tx(WIFI_IF_STA, frame, sizeof(frame), true);
        if (err == ESP_OK) {
            stats->succeeded++;
        } else {
            stats->last_error = err;
            ESP_LOGW(TAG, "raw TX %u failed: %s",
                     n, esp_err_to_name(err));
        }
    }

    ESP_LOGI(TAG, "TX burst queued: %u/%u frames",
             (unsigned)stats->succeeded,
             (unsigned)stats->attempted);

    return stats->succeeded ? ESP_OK :
           (stats->last_error == ESP_OK ? ESP_FAIL : stats->last_error);
}
