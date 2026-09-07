/*
 * common.h
 *
 *  Created on: 27.04.2017
 *      Author: michaelboeckling
 */
 
#ifndef _INCLUDE_COMMON_BUF_H_
#define _INCLUDE_COMMON_BUF_H_

#include <inttypes.h>
#include <stddef.h>
#include "freertos/ringbuf.h"
#include  "esp_log.h"
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif

typedef struct
{
    uint8_t *base;
    uint8_t *read_pos;//读指针，指向下一个将要读取的字节位置。当读取数据时，该指针会向后移动
    uint8_t *fill_pos;//写指针，指向下一个将要写入的字节位置。当写入数据时，该指针会向后移动
    uint16_t len;
    uint32_t bytes_consumed;//从缓冲区中成功读取的总字节数
} buffer_t;

/* create a buffer on the heap */
buffer_t *buf_create(size_t len);
buffer_t *buf_create_dma(size_t len);

/* free the backing storage, and the struct itself */
int buf_destroy(buffer_t *buf);

/**
 * Seek from the current position of the pointer.
 */
int buf_seek_rel(buffer_t *buf, uint32_t pos);


int buf_clear(buffer_t *buf);

/* available unused capacity */
size_t buf_free_capacity(buffer_t *buf);

/* total amount of data in the buffer */
size_t buf_data_total(buffer_t *buf);

/* bytes left to be consumed */
size_t buf_data_unread(buffer_t *buf);

/* stale bytes that have already been consumed */
size_t buf_data_stale(buffer_t *buf);

/**
 * Reads an array of count elements, each one with a size of size bytes,
 * from the stream and stores them in the block of memory specified by ptr.
 *
 * The position indicator of the stream is advanced by the total amount of bytes read.
 *
 * The total amount of bytes read if successful is (size*count).
 *
 * @param ptr Pointer to a block of memory with a size of at least (size*count) bytes, converted to a void*.
 * @param size Size, in bytes, of each element to be read.
 * @param count Number of elements, each one with a size of size bytes.
 */
size_t fill_read_buffer(buffer_t *buf,RingbufHandle_t audio_ringbuf);

#endif /* _INCLUDE_COMMON_BUFFER_H_ */
