#pragma once

#include <stddef.h>
#include <stdint.h>

#include "capture_format.h"
#include "esp_err.h"

typedef esp_err_t (*c5x_capture_hook_t)(void *arg);

esp_err_t c5x_capture_init(void);
esp_err_t c5x_capture_store_reset(void);

esp_err_t c5x_capture_run(c5x_capture_kind_t kind,
                          c5x_capture_hook_t hook,
                          void *hook_arg,
                          c5x_capture_header_t *header);

const uint8_t *c5x_capture_data(size_t *bytes);

esp_err_t c5x_capture_store_record(const c5x_capture_header_t *header,
                                   const uint8_t *data,
                                   size_t bytes);
