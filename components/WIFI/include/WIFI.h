// WIFI.h
#ifndef WIFI_H
#define WIFI_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define WIFI_CONNECTED_BIT BIT0 
extern EventGroupHandle_t player_event_group ;
extern EventGroupHandle_t wifi_event_group;

extern char REQUEST_URL[256];


void audio_player_eventgroup_init(void);
void DECODE_STOPED_SET(void);
void DECODE_STOPED_CLEAR(void);
void DECODE_STOPED_WAIT(void);
void FIFO_READY_SET(void);
void FIFO_READY_CLEAR(void);
void FIFO_READY_WAIT(void);
bool check_FIFO_NOTREADY(void);
void audio_player_play(void);
void audio_player_stop(void);
void wait_player_start(void);
bool check_player_notstart(void);



void audio_player_eventgroup_init(void);

void wifi_init_sta(void);
void http_request_task(void *pvParameters);

#endif