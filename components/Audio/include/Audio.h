// AUDIO_BUFFER.h
#ifndef AUDIO_H
#define AUDIO_H

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include  "esp_log.h"
#include "esp_http_client.h"
// 声明全局的环形缓冲区句柄，让其他文件(如WIFI.c)能找到它

extern RingbufHandle_t audio_ringbuf;

// 声明初始化函数
int audio_buffer_init(void);
void audio_deinit(void);
// 声明音频消费者任务
void audio_decoder_task(void *pvParameters);
void set_audio_config_by_transport(esp_http_client_transport_t transport);
#endif