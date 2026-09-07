#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <inttypes.h>

#include "nvs_flash.h"
#include "WIFI.h"
#include "esp_err.h"
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include "esp_log.h"
#include "Audio.h"
#include "I2S.h"
#include "WebServer.h"
#define SAMPLE_RATE 44100
#define TONE_FREQ_HZ 1000
#define TONE_VOLUME 6000
#define FRAME_COUNT 512

static const char *TAG = "APP_MAIN";
TaskHandle_t audio_task_handle = NULL;
// 前置声明（仅当启用运行统计时）
#ifdef CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
#endif
void memory_monitor_task(void *pvParameters);

void app_main(void)
{
    // ────────────────────────────────────────────────────
    //  1. 初始化 NVS（WiFi 存储底层需要 NVS）
    // ────────────────────────────────────────────────────
    ESP_LOGI(TAG, "初始化 NVS...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "NVS 分区损坏，擦除后重新初始化");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "✅ NVS 初始化完成");

    // ────────────────────────────────────────────────────
    //  2. 初始化硬件
    // ────────────────────────────────────────────────────
    ESP_LOGI(TAG, "初始化 I2S...");
    i2s_init();
    ESP_LOGI(TAG, "✅ I2S 初始化完成");

    ESP_LOGI(TAG, "初始化 WiFi...");
    wifi_init_sta();
    ESP_LOGI(TAG, "✅ WiFi 初始化完成");
    audio_player_eventgroup_init();
    //启动web服务器
    start_webserver();
    // ────────────────────────────────────────────────────
    //  3. 创建 HTTP 获取任务
    // ────────────────────────────────────────────────────
    ESP_LOGI(TAG, "创建 HTTP 任务 (Core 0)...");
    BaseType_t res0 = xTaskCreatePinnedToCore(
        http_request_task, // 任务函数
        "http_task",       // 任务名称
        1024 * 8,          // 任务栈大小 (8KB)
        NULL,              // 传递给任务的参数
        4,                 // 任务优先级 (降低到 4，让解码优先)
        NULL,              // 任务句柄
        0                  // 绑定到 Core 0 (处理网络)
    );
    if (res0 != pdPASS)
    {
        ESP_LOGE(TAG, "❌ HTTP 任务创建失败！");
    }
    else
    {
        ESP_LOGI(TAG, "✅ HTTP 任务创建成功");
    }

    BaseType_t res1 = xTaskCreatePinnedToCore(
        memory_monitor_task, // 任务函数
        "mem_monitor",       // 任务名称
        2048,                // 栈大小 2KB（足够打印日志）
        NULL,                // 参数
        1,                   // 优先级设为 1（最低有效优先级）
        NULL,                // 句柄（可忽略）
        0                    // 绑定到 Core 0（与 HTTP 任务同核，但优先级低，不会干扰）
    );
    if (res1 != pdPASS)
    {
        ESP_LOGE(TAG, "❌ HTTP 任务创建失败！");
    }
    else
    {
        ESP_LOGI(TAG, "✅ HTTP 任务创建成功");
    }
    // ────────────────────────────────────────────────────
    //  4. 创建音频解码任务（最高优先级！）
    // ────────────────────────────────────────────────────
    // 可以先创建，没用到FIFO,一直在死等着
    ESP_LOGI(TAG, "创建音频解码任务 (Core 1)...");
    BaseType_t res2 = xTaskCreatePinnedToCore(
        audio_decoder_task,
        "decoder_task",
        1024 * 32,
        NULL,
        6,
        &audio_task_handle,
        1);
    if (res2 != pdPASS)
    {
        ESP_LOGE(TAG, "❌ 音频解码任务创建失败！");
    }
    else
    {
        ESP_LOGI(TAG, "✅ 音频解码任务创建成功");
    }
}

void memory_monitor_task(void *pvParameters)
{
    while (1)
    {
        printf("Free heap: %u (min: %u)\n",
               (unsigned)esp_get_free_heap_size(),
               (unsigned)esp_get_minimum_free_heap_size());
        printf("Internal free: %u (min: %u)\n",
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
               (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}