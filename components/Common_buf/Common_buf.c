/*
 * common_buffer.c
 *
 *  Created on: 28.04.2017
 *      Author: michaelboeckling
 */

#include "Common_buf.h"
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

#define TAG "common_buf"

/* creates a buffer struct and its storage on the heap */
buffer_t *buf_create(size_t len)
{
    buffer_t* buf = calloc(1, sizeof(buffer_t));
    //分配一个 buffer_t 大小的内存块，并将所有字节初始化为零。
    //使用 calloc 而非 malloc 可以保证结构体初始状态为全零，避免未初始化字段的随机值。
	if(buf == NULL) {
        ESP_LOGE(TAG, "couldn't malloc buffer size %d",  sizeof(buffer_t));
        return NULL;
    }
    buf->len = len;
    buf->base = calloc(len,sizeof(uint8_t));//sizeof(uint8_t) 为 1，等价于 calloc(len, 1)。
    if(buf->base == NULL) {
        ESP_LOGE(TAG, "couldn't calloc base size %d", len);
        return NULL;
    }
    buf->read_pos = buf->base;
    buf->fill_pos = buf->base;
    buf->bytes_consumed = 0;

    return buf;
}
/* creates a buffer struct and its storage on the heap */
buffer_t *buf_create_dma(size_t len)
{
    buffer_t* buf = heap_caps_calloc(1,sizeof(buffer_t), MALLOC_CAP_DEFAULT);
	if(buf == NULL) {
        ESP_LOGE(TAG, "couldn't calloc buffer size %d",  sizeof(buffer_t));
        return NULL;
    }
    buf->len = len;
    buf->base = heap_caps_calloc(len,sizeof(uint8_t), MALLOC_CAP_DMA);
    if(buf->base == NULL) {
        ESP_LOGE(TAG, "couldn't calloc base size %d", len);
        return NULL;
    }
    buf->read_pos = buf->base;
    buf->fill_pos = buf->base;
    buf->bytes_consumed = 0;

    return buf;
}

/* free the buffer struct and its storage */
int buf_destroy(buffer_t *buf)
{
    if(buf == NULL)
        return -1;

    if(buf->base != NULL)
        free(buf->base);

    free(buf);

    return 0;
}

/* available unused capacity *///用于计算缓冲区中可写入的空闲容量
size_t buf_free_capacity(buffer_t *buf)
{
    if(buf == NULL) return -1;

    size_t unused_capacity = (buf->base + buf->len) - buf->fill_pos;
    return buf_data_stale(buf) + unused_capacity;//读过的区域 读指针永远比写指针小
}

/* amount of bytes read */
size_t buf_data_total(buffer_t *buf)
{
    if(buf == NULL) return -1;

    return buf->fill_pos - buf->base;
}

/* amount of bytes unread */
size_t buf_data_unread(buffer_t *buf)
{
    if(buf == NULL) return -1;

    return buf->fill_pos - buf->read_pos;
}

/* amount of bytes already consumed */
size_t buf_data_stale(buffer_t *buf)
{
    if(buf == NULL) return -1;

    return buf->read_pos - buf->base;
}

void buf_move_remaining_bytes_to_front(buffer_t *buf)
//buf把已读过的数据丢掉
{
    size_t unread_data = buf_data_unread(buf);

    // move remaining data to front
    memmove(buf->base, buf->read_pos, unread_data);
    buf->read_pos = buf->base;
    buf->fill_pos = buf->base + unread_data;
}

// move unread data to front and fill the free space
//删除读过的数据  补充缓冲区  返回补充了多少字节
size_t fill_read_buffer(buffer_t *buf,RingbufHandle_t audio_ringbuf)
{
    buf_move_remaining_bytes_to_front(buf);
    size_t item_size = 0;//输出参数，函数返回时写入实际接收到的字节数。
    //是零拷贝接口，返回指向环形缓冲区内部存储区域的指针 item。
	unsigned fbsize = buf_free_capacity(buf);
    if (fbsize == 0) {
        return 0;   // 没有空间，避免 xMaxSize = 0
    }
    char *item = (char *)xRingbufferReceiveUpTo(
        audio_ringbuf,
        &item_size,
        pdMS_TO_TICKS(5),   // 短超时：减少等待，保证解码连续性
        (size_t)fbsize
    );
    if (item_size > 0) {
        memcpy(buf->fill_pos, item, item_size);
        buf->fill_pos += item_size;
        vRingbufferReturnItem(audio_ringbuf, item);
    }
    return item_size;
}

//丢弃缓冲区中一定数量的字节  更新读指针
int buf_seek_rel(buffer_t *buf, uint32_t offset)
{
    if (buf == NULL) return -1;

    // advance through buffer, loading new data as necessary
    size_t data_avail = buf_data_unread(buf);
    // if offset exceeds buffer capacity, load more data
    if(offset > data_avail) {
        return -1;
    } else {
        buf->read_pos += offset;
        buf->bytes_consumed += offset;
    }
    return 0;
}

int buf_clear(buffer_t *buf)
{
    if (buf == NULL) return -1;
	buf->read_pos = buf->base;
    buf->fill_pos = buf->base;
    buf->bytes_consumed = 0;

    return 0;
}