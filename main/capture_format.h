#pragma once

#include <stdint.h>

#define C5X_CAPTURE_MAGIC       0x30583543u /* "C5X0" little-endian */
#define C5X_CAPTURE_VERSION     1u
#define C5X_CAPTURE_SLOT_SIZE   0x11000u
#define C5X_CAPTURE_DATA_OFFSET 0x1000u

typedef enum {
    C5X_CAPTURE_BASELINE = 0,
    C5X_CAPTURE_TX_BURST = 1,
} c5x_capture_kind_t;

/*
 * Fixed 128-byte on-flash header.
 *
 * Q4/I4 byte layout:
 *   bits 3:0 = Q[9:6]
 *   bits 7:4 = I[9:6]
 * Each nibble is interpreted as signed two's-complement.
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t header_bytes;

    uint32_t kind;
    uint32_t capture_status;
    uint32_t sample_rate_hz;
    uint32_t sample_bytes;
    uint32_t wifi_channel;
    uint32_t flags;

    uint32_t dump_ctrl_before;
    uint32_t dump_ptr_before;
    uint32_t dump_ctrl_after;
    uint32_t dump_ptr_after;

    uint32_t fnv1a;
    int32_t i_sum;
    int32_t q_sum;
    uint64_t power_sum;

    uint32_t nonzero_bytes;
    uint32_t changed_bytes;
    uint32_t sentinel_bytes;

    uint32_t tx_attempted;
    uint32_t tx_ok;
    uint32_t hook_status;

    uint32_t capture_cycles;
    uint32_t parlio_int_raw;
    uint32_t parlio_rx_st0;
    uint32_t parlio_rx_st1;

    uint32_t reserved[5];
} c5x_capture_header_t;

_Static_assert(sizeof(c5x_capture_header_t) == 128u,
               "C5X capture header must remain exactly 128 bytes");
