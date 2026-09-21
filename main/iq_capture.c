#include "iq_capture.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/parlio_rx.h"
#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "soc/parl_io_struct.h"

#include "rf_probe.h"

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define DUMP_CTRL     0x600a9004u
#define DUMP_PTR_MODE 0x600a9008u

#define C5X_IQ_RATE_HZ 40000000u
#define C5X_SENTINEL   0xa5u

static const char *TAG = "c5x_cap";

static DMA_ATTR __attribute__((aligned(64)))
uint8_t s_capture[CONFIG_C5X_CAPTURE_BYTES];

static parlio_rx_unit_handle_t s_rx;
static parlio_rx_delimiter_handle_t s_delimiter;

static inline int8_t sign4(uint8_t value)
{
    value &= 0x0fu;
    return (value & 0x08u) ? (int8_t)(value - 16u) : (int8_t)value;
}

static uint32_t fnv1a(const uint8_t *data, size_t bytes)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0u; i < bytes; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

static void summarize(c5x_capture_header_t *header)
{
    int32_t i_sum = 0;
    int32_t q_sum = 0;
    uint64_t power_sum = 0u;
    uint32_t nonzero = 0u;
    uint32_t changed = 0u;
    uint32_t sentinel = 0u;

    uint8_t previous = s_capture[0];

    for (size_t n = 0u; n < sizeof(s_capture); ++n) {
        const uint8_t packed = s_capture[n];
        const int8_t q = sign4(packed);
        const int8_t i = sign4(packed >> 4u);

        i_sum += i;
        q_sum += q;
        power_sum += (uint64_t)(i * i + q * q);
        nonzero += packed != 0u;
        sentinel += packed == C5X_SENTINEL;

        if (n != 0u) {
            changed += packed != previous;
            previous = packed;
        }
    }

    header->fnv1a = fnv1a(s_capture, sizeof(s_capture));
    header->i_sum = i_sum;
    header->q_sum = q_sum;
    header->power_sum = power_sum;
    header->nonzero_bytes = nonzero;
    header->changed_bytes = changed;
    header->sentinel_bytes = sentinel;
}

esp_err_t c5x_capture_init(void)
{
    const parlio_rx_unit_config_t cfg = {
        .trans_queue_depth = 1u,
        .max_recv_size = sizeof(s_capture),
        .dma_burst_size = 32u,
        .data_width = 8u,
        .clk_src = PARLIO_CLK_SRC_DEFAULT,
        .ext_clk_freq_hz = 0u,
        .exp_clk_freq_hz = C5X_IQ_RATE_HZ,
        .clk_in_gpio_num = -1,
        .clk_out_gpio_num = -1,
        .valid_gpio_num = -1,
        .data_gpio_nums = {
            1, 0, 25, 7,
            10, 5, 3, 4,
        },
        .flags = {
            .free_clk = true,
            .clk_gate_en = false,
            .allow_pd = false,
        },
    };

    esp_err_t err = parlio_new_rx_unit(&cfg, &s_rx);
    if (err != ESP_OK) return err;

    const parlio_rx_soft_delimiter_config_t delimiter_cfg = {
        .sample_edge = PARLIO_SAMPLE_EDGE_POS,
        .bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB,
        .eof_data_len = sizeof(s_capture),
        .timeout_ticks = 0u,
    };

    err = parlio_new_rx_soft_delimiter(&delimiter_cfg, &s_delimiter);
    if (err != ESP_OK) return err;

    return parlio_rx_unit_enable(s_rx, true);
}

esp_err_t c5x_capture_store_reset(void)
{
    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                 (esp_partition_subtype_t)0x40,
                                 "c5xcap");
    if (!part) return ESP_ERR_NOT_FOUND;

    if (part->size < (2u * C5X_CAPTURE_SLOT_SIZE)) {
        return ESP_ERR_INVALID_SIZE;
    }

    return esp_partition_erase_range(part, 0u, part->size);
}

esp_err_t c5x_capture_run(c5x_capture_kind_t kind,
                          c5x_capture_hook_t hook,
                          void *hook_arg,
                          c5x_capture_header_t *header)
{
    if (!header || !s_rx || !s_delimiter) return ESP_ERR_INVALID_STATE;

    memset(header, 0, sizeof(*header));
    header->magic = C5X_CAPTURE_MAGIC;
    header->version = C5X_CAPTURE_VERSION;
    header->header_bytes = sizeof(*header);
    header->kind = (uint32_t)kind;
    header->sample_rate_hz = C5X_IQ_RATE_HZ;
    header->sample_bytes = sizeof(s_capture);
    header->wifi_channel = c5x_rf_channel();

    memset(s_capture, C5X_SENTINEL, sizeof(s_capture));

    /*
     * Re-arm before every bounded record. For the TX record this also makes
     * any TX_START-induced state transition directly visible in the header.
     */
    c5x_rf_arm_iq_source();

    header->dump_ctrl_before = REG32(DUMP_CTRL);
    header->dump_ptr_before = REG32(DUMP_PTR_MODE);

    const parlio_receive_config_t receive_cfg = {
        .delimiter = s_delimiter,
        .flags = {
            .partial_rx_en = false,
            .indirect_mount = false,
        },
    };

    esp_err_t err = parlio_rx_unit_receive(
        s_rx, s_capture, sizeof(s_capture), &receive_cfg);
    if (err != ESP_OK) {
        header->capture_status = (uint32_t)err;
        summarize(header);
        return err;
    }

    const uint32_t start_cycles = esp_cpu_get_cycle_count();

    /*
     * C5VRX hardware work established this explicit software gate as the
     * deterministic way to open the bounded PARLIO capture after the modem
     * producer is armed.
     */
    PARL_IO.rx_mode_cfg.rx_sw_en = 1u;
    __asm__ __volatile__("fence iorw, iorw" ::: "memory");

    esp_err_t hook_err = ESP_OK;
    if (hook) {
        hook_err = hook(hook_arg);
    }

    const esp_err_t wait_err = parlio_rx_unit_wait_all_done(s_rx, 100u);

    PARL_IO.rx_mode_cfg.rx_sw_en = 0u;
    __asm__ __volatile__("fence iorw, iorw" ::: "memory");

    header->capture_cycles = esp_cpu_get_cycle_count() - start_cycles;
    header->capture_status = (uint32_t)wait_err;
    header->hook_status = (uint32_t)hook_err;
    header->dump_ctrl_after = REG32(DUMP_CTRL);
    header->dump_ptr_after = REG32(DUMP_PTR_MODE);
    header->parlio_int_raw = PARL_IO.int_raw.val;
    header->parlio_rx_st0 = PARL_IO.rx_st0.val;
    header->parlio_rx_st1 = PARL_IO.rx_st1.val;

    if (wait_err == ESP_OK) header->flags |= 1u;
    if (hook) header->flags |= 2u;

    summarize(header);

    ESP_LOGI(TAG,
             "%s status=%s hook=%s hash=%08x power=%llu sentinel=%u "
             "dump=%08x->%08x ptr=%08x->%08x",
             kind == C5X_CAPTURE_TX_BURST ? "TX" : "BASE",
             esp_err_to_name(wait_err),
             esp_err_to_name(hook_err),
             (unsigned)header->fnv1a,
             (unsigned long long)header->power_sum,
             (unsigned)header->sentinel_bytes,
             (unsigned)header->dump_ctrl_before,
             (unsigned)header->dump_ctrl_after,
             (unsigned)header->dump_ptr_before,
             (unsigned)header->dump_ptr_after);

    return wait_err == ESP_OK ? hook_err : wait_err;
}

const uint8_t *c5x_capture_data(size_t *bytes)
{
    if (bytes) *bytes = sizeof(s_capture);
    return s_capture;
}

esp_err_t c5x_capture_store_record(const c5x_capture_header_t *header,
                                   const uint8_t *data,
                                   size_t bytes)
{
    if (!header || !data) return ESP_ERR_INVALID_ARG;
    if (bytes != header->sample_bytes || bytes > (C5X_CAPTURE_SLOT_SIZE -
                                                  C5X_CAPTURE_DATA_OFFSET)) {
        return ESP_ERR_INVALID_SIZE;
    }

    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                 (esp_partition_subtype_t)0x40,
                                 "c5xcap");
    if (!part) return ESP_ERR_NOT_FOUND;

    if (header->kind > C5X_CAPTURE_TX_BURST) return ESP_ERR_INVALID_ARG;

    const size_t slot = (size_t)header->kind * C5X_CAPTURE_SLOT_SIZE;
    if (slot + C5X_CAPTURE_DATA_OFFSET + bytes > part->size) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = esp_partition_write(
        part, slot + C5X_CAPTURE_DATA_OFFSET, data, bytes);
    if (err != ESP_OK) return err;

    /*
     * Write the validity marker/header last: a valid magic means the payload
     * was already persisted.
     */
    return esp_partition_write(part, slot, header, sizeof(*header));
}
