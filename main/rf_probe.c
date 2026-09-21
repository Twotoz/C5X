#include "rf_probe.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_rom_gpio.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "soc/gpio_sig_map.h"

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

/* R0 uses legal, ordinary 5 GHz Wi-Fi operation rather than FPV frequencies. */
#define C5X_WIFI_CHANNEL 36u

/*
 * Undocumented C5 modem registers carried over from the physically tested
 * C5VRX receive-domain work. They are deliberately isolated in this file.
 */
#define DUMP_CTRL       0x600a9004u
#define DUMP_PTR_MODE   0x600a9008u
#define DUMP_FORMAT     0x600a9018u
#define FE_PATH         0x600a20b4u
#define FE_ENABLE       0x600a0800u
#define SOURCE_CTRL     0x600a08ccu
#define SOURCE_MUX      0x600a70b8u
#define MODEM_CLOCK     0x600a9c04u
#define HP_SRAM_USAGE   0x60095004u

#define CTRL_ENABLE     0x80000000u
#define CTRL_DUMP_FIRST 0x00020000u
#define TX_START_SELECT 0x00060000u
#define SELECTOR_MASK   0x01fe0000u

/*
 * XIAO ESP32-C5 mapping proven by C5VRX hardware captures:
 *   DIAG[6:9]   -> Q[9:6]
 *   DIAG[16:19] -> I[9:6]
 */
static const gpio_num_t s_iq_pins[8] = {
    GPIO_NUM_1, GPIO_NUM_0, GPIO_NUM_25, GPIO_NUM_7,
    GPIO_NUM_10, GPIO_NUM_5, GPIO_NUM_3, GPIO_NUM_4,
};
static const uint8_t s_iq_diag[8] = {
    6u, 7u, 8u, 9u,
    16u, 17u, 18u, 19u,
};

static const char *TAG = "c5x_rf";

static esp_err_t init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err == ESP_OK) err = nvs_flash_init();
    }
    return err;
}

static esp_err_t route_modem_iq(void)
{
    uint64_t mask = 0u;
    for (unsigned lane = 0; lane < 8u; ++lane) {
        mask |= 1ULL << s_iq_pins[lane];
    }

    const gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) return err;

    for (unsigned lane = 0; lane < 8u; ++lane) {
        esp_rom_gpio_connect_out_signal(
            s_iq_pins[lane],
            MODEM_DIAG0_IDX + s_iq_diag[lane],
            false,
            false);
    }

    __asm__ __volatile__("fence iorw, iorw" ::: "memory");
    return ESP_OK;
}

void c5x_rf_arm_iq_source(void)
{
    /*
     * Keep CPU ownership of HP SRAM while enabling the modem diagnostic
     * producer. C5X does not consume the private dump SRAM in R0; it only uses
     * the simultaneous MODEM_DIAG observation path.
     */
    REG32(HP_SRAM_USAGE) =
        (REG32(HP_SRAM_USAGE) & 0xfffef0ffu) | 0x00010000u;

    REG32(SOURCE_CTRL) &= 0xff87ffffu;
    REG32(SOURCE_MUX) = (REG32(SOURCE_MUX) & 0xfffffff8u) | 1u;
    REG32(MODEM_CLOCK) = UINT32_MAX;
    REG32(FE_ENABLE) |= 4u;
    REG32(FE_PATH) &= ~1u;

    uint32_t v = REG32(DUMP_FORMAT);
    v = (v & 0xff03ffffu) | 0x006c0000u;
    REG32(DUMP_FORMAT) = v;
    v = (REG32(DUMP_FORMAT) & 0xfffc0fffu) | 0x0001a000u;
    REG32(DUMP_FORMAT) = v;
    v = (REG32(DUMP_FORMAT) & 0xfffff03fu) | 0x00000640u;
    REG32(DUMP_FORMAT) = v;
    v = (REG32(DUMP_FORMAT) & 0xffffffc0u) | 0x18u;
    REG32(DUMP_FORMAT) = v | 0x01000000u;

    REG32(DUMP_PTR_MODE) =
        (REG32(DUMP_PTR_MODE) & ~SELECTOR_MASK) | TX_START_SELECT;

    uint32_t ctrl = REG32(DUMP_CTRL);
    ctrl &= ~(CTRL_ENABLE | 0x00080000u | 0x00040000u);
    ctrl |= CTRL_DUMP_FIRST;
    ctrl = (ctrl & ~0x0001ffffu) | 16384u;
    REG32(DUMP_CTRL) = ctrl;

    __asm__ __volatile__("fence iorw, iorw" ::: "memory");
    REG32(DUMP_CTRL) = ctrl | CTRL_ENABLE;
    __asm__ __volatile__("fence iorw, iorw" ::: "memory");
}

esp_err_t c5x_rf_start(void)
{
    esp_err_t err = init_nvs();
    if (err != ESP_OK) return err;

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_cfg.sta_disconnected_pm = false;

    if ((err = esp_wifi_init(&wifi_cfg)) != ESP_OK) return err;
    if ((err = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK) return err;
    if ((err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK) return err;

    /*
     * esp_wifi_80211_tx() otherwise defaults to 1 Mbps. That is an 802.11b
     * rate and is not appropriate for our 5 GHz R0 probe. Configure a legacy
     * 802.11a OFDM rate before esp_wifi_start(), as required by ESP-IDF.
     */
    wifi_tx_rate_config_t tx_rate = {
        .phymode = WIFI_PHY_MODE_11A,
        .rate = WIFI_PHY_RATE_6M,
        .ersu = false,
        .dcm = false,
    };
    if ((err = esp_wifi_config_80211_tx(WIFI_IF_STA, &tx_rate)) != ESP_OK)
        return err;

    if ((err = esp_wifi_start()) != ESP_OK) return err;

#if CONFIG_SOC_WIFI_SUPPORT_5G
    if ((err = esp_wifi_set_band_mode(WIFI_BAND_MODE_5G_ONLY)) != ESP_OK)
        return err;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif

    if ((err = esp_wifi_set_ps(WIFI_PS_NONE)) != ESP_OK) return err;

    wifi_protocols_t protocols = {
        .ghz_2g = WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G |
                  WIFI_PROTOCOL_11N,
        .ghz_5g = WIFI_PROTOCOL_11A | WIFI_PROTOCOL_11N,
    };
    if ((err = esp_wifi_set_protocols(WIFI_IF_STA, &protocols)) != ESP_OK)
        return err;

    wifi_bandwidths_t bandwidths = {
        .ghz_2g = WIFI_BW20,
        .ghz_5g = WIFI_BW40,
    };
    if ((err = esp_wifi_set_bandwidths(WIFI_IF_STA, &bandwidths)) != ESP_OK)
        return err;

    /*
     * Channel 36 + secondary-above gives the first 40 MHz block in the 5 GHz
     * band while staying on ordinary Wi-Fi channels.
     */
    if ((err = esp_wifi_set_channel(C5X_WIFI_CHANNEL,
                                    WIFI_SECOND_CHAN_ABOVE)) != ESP_OK) {
        return err;
    }

    if ((err = esp_wifi_set_promiscuous(true)) != ESP_OK) return err;
    wifi_promiscuous_filter_t filter = { .filter_mask = 0u };
    (void)esp_wifi_set_promiscuous_filter(&filter);

    /*
     * Start deliberately low. This is not a range test yet; R0 asks whether
     * any useful receive-domain information survives self-transmission.
     */
    err = esp_wifi_set_max_tx_power((int8_t)CONFIG_C5X_TX_MAX_POWER_QDBM);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TX power request rejected: %s", esp_err_to_name(err));
    }

    if ((err = route_modem_iq()) != ESP_OK) return err;

    c5x_rf_arm_iq_source();

    uint8_t primary = 0u;
    wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
    if ((err = esp_wifi_get_channel(&primary, &secondary)) != ESP_OK)
        return err;

    ESP_LOGI(TAG,
             "R0 RF ready: primary=%u secondary=%d BW40, TX max request=%d qdBm",
             primary, (int)secondary, CONFIG_C5X_TX_MAX_POWER_QDBM);
    return primary == C5X_WIFI_CHANNEL ? ESP_OK : ESP_ERR_INVALID_STATE;
}

uint8_t c5x_rf_channel(void)
{
    return C5X_WIFI_CHANNEL;
}
