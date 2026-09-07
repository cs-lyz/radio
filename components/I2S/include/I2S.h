// I2S.h
#ifndef I2S_H
#define I2S_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
void i2s_dma_zero(void);
void i2s_init(void);
void i2s_set_sample_rate_and_channel(uint32_t sample_rate, uint8_t channels);
esp_err_t i2s_write_data(const void *data, size_t size, size_t *bytes_written);

#endif