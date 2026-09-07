#include "I2S.h"

#include <stdbool.h>

#include "esp_err.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"

// 定义 I2S 引脚
#define I2S_BCK_IO 26
#define I2S_WS_IO  25
#define I2S_DO_IO  22

i2s_chan_handle_t tx_chan = NULL;

void i2s_dma_zero(void){
    // 2. 【核心替换】清空 I2S DMA 硬件缓存 (ESP-IDF v5 API 写法)
                // 关闭通道会丢弃所有排队的 DMA 描述符
    i2s_channel_disable(tx_chan); 
                // 重新打开后，由于你配置了 auto_clear = true，硬件会立刻自动输出完全静音（0x00）
    i2s_channel_enable(tx_chan);  
}

void i2s_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    // ✅ 覆盖默认值
    chan_cfg.dma_desc_num  = 8;    // 链表节点数，建议 6~8
    chan_cfg.dma_frame_num = 512;  // 每节点帧数，建议 256~512
    chan_cfg.auto_clear    = true; // ← 重要！下溢时输出静音而不是噪音
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_chan, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_STEREO
        ),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCK_IO,
            .ws   = I2S_WS_IO,
            .dout = I2S_DO_IO,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));
}

void i2s_set_sample_rate_and_channel(uint32_t sample_rate, uint8_t channels)
{
    ESP_ERROR_CHECK(i2s_channel_disable(tx_chan));

    // 1. 更新采样率
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    ESP_ERROR_CHECK(i2s_channel_reconfig_std_clock(tx_chan, &clk_cfg));

    // 2. 更新声道模式（先确定 slot_mode，避免宏展开问题）
    i2s_slot_mode_t slot_mode = (channels == 2) ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO;
    i2s_std_slot_config_t slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT,
        slot_mode
    );
    ESP_ERROR_CHECK(i2s_channel_reconfig_std_slot(tx_chan, &slot_cfg));

    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));
}

esp_err_t i2s_write_data(const void *data, size_t size, size_t *bytes_written)
{
    if (tx_chan == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return i2s_channel_write(//休眠，通过信号量唤醒
        tx_chan,
        data,
        size,
        bytes_written,
        portMAX_DELAY
    );
}