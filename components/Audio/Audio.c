// Audio.c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>  // 新增：提供 PRIu32、PRIu64
#include "esp_log.h"
#include "esp_timer.h"
#include "Audio.h"
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#include "I2S.h"
#include "Common_buf.h"
#include "WIFI.h"
// ─────────────────────────────────────────────
//  缓冲区参数
// ─────────────────────────────────────────────
typedef struct {
    uint32_t ringbuf_size;
    uint32_t mp3_input_buf_size;
    uint32_t mp3_refill_threshold;
} audio_config_t;

const audio_config_t http_config = {
    .ringbuf_size = 64 * 1024,
    .mp3_input_buf_size = 4 * 1024,
    .mp3_refill_threshold = 1024
};
const audio_config_t https_config = {
    .ringbuf_size = 64 * 1024,
    .mp3_input_buf_size = 4 * 1024,
    .mp3_refill_threshold = 1024
};

uint32_t RINGBUF_SIZE = 64 * 1024;      // 默认值 http
uint32_t MP3_INPUT_BUF_SIZE = 4 * 1024;
uint32_t MP3_REFILL_THRESHOLD = 1024;

static const char *TAG = "AUDIO";
unsigned int buf_underrun_cnt=0;

RingbufHandle_t audio_ringbuf = NULL;
mp3dec_t *mp3d=NULL;
buffer_t   *mp3_input_buf=NULL;
mp3d_sample_t *pcm_buf;
mp3dec_frame_info_t info;

typedef struct {
    uint32_t frame_count;               // 总解码帧数
    uint32_t total_samples;             // 总样本数
    uint32_t ringbuf_refill_count;      // RingBuffer 补充次数
    uint32_t ringbuf_empty_count;       // RingBuffer 为空次数
    
} audio_stats_t;

/**
 * @brief 根据传输类型设置音频缓冲区参数
 * @param transport 传输类型（HTTP_TRANSPORT_OVER_TCP 或 HTTP_TRANSPORT_OVER_SSL）
 */
void set_audio_config_by_transport(esp_http_client_transport_t transport)
{
    const audio_config_t *cfg;
    if (transport == HTTP_TRANSPORT_OVER_SSL) {
        cfg = &https_config;
    } else {
        cfg = &http_config;
    }

    RINGBUF_SIZE = cfg->ringbuf_size;
    MP3_INPUT_BUF_SIZE = cfg->mp3_input_buf_size;
    MP3_REFILL_THRESHOLD = cfg->mp3_refill_threshold;

    ESP_LOGI(TAG, "音频配置已设置: ringbuf=%lu, input_buf=%lu, refill=%lu",
             (unsigned long)RINGBUF_SIZE,
             (unsigned long)MP3_INPUT_BUF_SIZE,
             (unsigned long)MP3_REFILL_THRESHOLD);
}

// ─────────────────────────────────────────────
//  初始化环形缓冲区
// ─────────────────────────────────────────────

int audio_buffer_init(void) {
    audio_ringbuf = xRingbufferCreate(RINGBUF_SIZE, RINGBUF_TYPE_BYTEBUF);
    if (!audio_ringbuf) return -1;

    mp3d = malloc(sizeof(mp3dec_t));
    if (!mp3d) {
        vRingbufferDelete(audio_ringbuf);
        audio_ringbuf = NULL;
        return -1;
    }
    
    mp3dec_init(mp3d);
    mp3_input_buf = buf_create(MP3_INPUT_BUF_SIZE);
    pcm_buf = malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(mp3d_sample_t));
    
    if (!mp3_input_buf || !pcm_buf) {
        if (mp3_input_buf) buf_destroy(mp3_input_buf);
        if (pcm_buf) free(pcm_buf);
        free(mp3d);
        vRingbufferDelete(audio_ringbuf);
        audio_ringbuf = NULL;
        return -1;
    }
    memset(&info, 0, sizeof(info));
    return 0;
}

void audio_deinit(void)
{
    // 1. 释放环形缓冲区（如果有）
    if (audio_ringbuf != NULL) {
        vRingbufferDelete(audio_ringbuf);
        audio_ringbuf = NULL;
    }

    // 2. 释放解码器状态
    if (mp3d != NULL) {
        free(mp3d);
        mp3d = NULL;
    }

    // 3. 释放 MP3 输入缓冲区（假设 buf_destroy 内部会释放内存并置空）
    if (mp3_input_buf != NULL) {
        buf_destroy(mp3_input_buf);   // 务必确保该函数将传入指针置 NULL，或你手动置 NULL
        mp3_input_buf = NULL;
    }

    // 4. 释放 PCM 工作缓冲区
    if (pcm_buf != NULL) {
        free(pcm_buf);
        pcm_buf = NULL;
    }
    // 6. 可选：清零 info 结构（如果有动态成员则需额外处理，这里只是静态结构）
    memset(&info, 0, sizeof(info));
    ESP_LOGI(TAG, "所有音频资源已释放");
}

static int input(buffer_t *buf){
    int bytes_to_read;
    while(1){
        wait_player_start();
        FIFO_READY_WAIT();
        size_t total_size =RINGBUF_SIZE ;/* 创建 ringbuffer 时指定的大小 */
        size_t free_size = xRingbufferGetCurFreeSize(audio_ringbuf);
        size_t used_size = total_size - free_size;
        bytes_to_read=min(buf_free_capacity(buf),used_size);
        if(bytes_to_read==0){//没有数据可以读
            if(buf_free_capacity(buf) == 0){
                break;  // buf已满，直接去解码
            }
            if(used_size == 0){
                ESP_LOGV(TAG, "Buffer underflow");
                buf_underrun_cnt++;
                vTaskDelay(20);
            }
        }else{
            fill_read_buffer(buf,audio_ringbuf);
            if(buf_data_unread(buf) >= MP3_REFILL_THRESHOLD)
                break;
        }
    }
    return 0;
}

// ─────────────────────────────────────────────
//  音频解码任务
// ─────────────────────────────────────────────
void audio_decoder_task(void *pvParameters){   
    uint32_t init_sample_rate = 44100;
    uint32_t init_channel = 2;
    int64_t last_stats_print = 0;          // 上次打印统计信息的时间戳
    
    while(1){
        // input 保持原样，没有任何修改。
        // 如果处于停止状态且 RingBuffer 被清空，任务会一直在 input 里每 20ms 延时空转。
        // 直到下一次播放，RingBuffer 有数据了，它才会出来。
         // 每回循环都检查FIFO要创建好信号
        input(mp3_input_buf);
        while(1){
            
            if (check_player_notstart() || check_FIFO_NOTREADY()) {
                ESP_LOGI(TAG, "🛑 收到停止信号，强制中断解码");
                // 1. 废弃本地缓存中还没解完的 mp3 残留数据
                // 例如: buf_reset(mp3_input_buf); 
                buf_clear(mp3_input_buf);
                i2s_dma_zero();
                DECODE_STOPED_SET();
                break; // 跳出内部解码循环，回到最外层调用 input()，从而进入 20ms 的循环等待
            }


            int samples =mp3dec_decode_frame(
                mp3d,
                mp3_input_buf ->read_pos,
                buf_data_unread(mp3_input_buf),//最大可以读多少
                pcm_buf,
                &info
            );
            
            //你的想法：
            // frame_bytes=128，samples=0
            // → "没解出声音，说明数据不全"
            // → "等下一轮数据补全了再解"
            // 实际情况：
            // frame_bytes=128 说明minimp3已经完整识别了这128字节
            // 数据是完整的，只是ID3不产生PCM
            // 如果不移动指针：
            //   下一轮还是拿同样的128字节给minimp3
            //   minimp3还是返回 samples=0, frame_bytes=128
            //   → 死循环
            //数据如果不够没到全   frame_bytes一定是0
            if(info.frame_bytes > 0){
                buf_seek_rel(mp3_input_buf, info.frame_bytes);  // ✅ 每帧解完立即消费
            }
            if (samples > 0) {
                // 采样率切换检测
                if ((uint32_t)info.hz != init_sample_rate || info.channels != init_channel) {
                    i2s_set_sample_rate_and_channel((uint32_t)info.hz,info.channels);
                    init_sample_rate = (uint32_t)info.hz;
                    init_channel=info.channels;
                    ESP_LOGI(TAG, "采样率: %d Hz, 声道数: %d", info.hz, info.channels);
                }
                size_t bytes_written;
                size_t pcm_bytes = (size_t)samples * info.channels * sizeof(mp3d_sample_t);
                i2s_write_data(pcm_buf, pcm_bytes, &bytes_written);
                //pcm_bytes：要发送的字节数（上一步计算得出）。
                //&bytes_written：输出参数，函数执行后写入实际发送成功的字节数。

                // 每 5 秒打印一次关键指标
            int64_t now = esp_timer_get_time();
            if (now - last_stats_print > 5000000) {   // 5 秒 = 5,000,000 微秒
                size_t used = RINGBUF_SIZE - xRingbufferGetCurFreeSize(audio_ringbuf);
                ESP_LOGI(TAG, "[Stats] underrun=%u, ringbuf_used=%u/%u",
                    buf_underrun_cnt, used, (unsigned int)RINGBUF_SIZE);
                last_stats_print = now;
            }

            }else{
                break;
            }
        }
    }
}

