#include <stdio.h>
#include <string.h>
#include <inttypes.h> // 新增：提供 PRIu32、PRIu64
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "WIFI.h"
#include "Audio.h"
#include "esp_crt_bundle.h"

#define WIFI_SSID "lyz-"
#define WIFI_PASS "21952984201"

char REQUEST_URL[256] = "https://lhttp.qingting.fm/live/333/64k.mp3";
#define REQUEST_URL1 "http://vprbbc.streamguys.net/vprbbc24.mp3"

#define HTTP_SEND_TIMEOUT (200)
#define HTTP_READ_TIMEOUT (30000)
#define HTTP_MAX_RETRY (5)
int HTTP_READ_BUF_SIZE=8 * 1024;
static const char *TAG = "wifi_http";

EventGroupHandle_t wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0

// 1. 定义事件组句柄
EventGroupHandle_t player_event_group = NULL;
// 2. 定义两个事件位
#define PLAYER_PLAY_BIT (1 << 0)  // 第 0 位：控制播放

#define FIFO_READY_BIT (1 << 2)  //第2位 ：FIFO准备好位
#define DECODE_STOPED_BIT (1 << 3)  //第3位 ：解码任务已经收到停止位
// 3. 初始化事件组（在 app_main 创建任务前调用一次）
void audio_player_eventgroup_init(void) {
    if (player_event_group == NULL) {
        player_event_group = xEventGroupCreate();
    }
}


// ：解码任务已经收到停止位
void DECODE_STOPED_SET(void) {
    if (player_event_group != NULL) {
        ESP_LOGI(TAG, "▶️ 解码任务已停止");
        
        xEventGroupSetBits(player_event_group, DECODE_STOPED_BIT); 
    }
}

void DECODE_STOPED_CLEAR(void) {
    if (player_event_group != NULL) {
        xEventGroupClearBits(player_event_group, DECODE_STOPED_BIT); 
    }
}
void DECODE_STOPED_WAIT(void){
    xEventGroupWaitBits(player_event_group, DECODE_STOPED_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
}

// 控制解码器任务开始
void FIFO_READY_SET(void) {
    if (player_event_group != NULL) {
        ESP_LOGI(TAG, "▶️ 解码指令已发送");
        
        xEventGroupSetBits(player_event_group, FIFO_READY_BIT); 
    }
}

void FIFO_READY_CLEAR(void) {
    if (player_event_group != NULL) {
        xEventGroupClearBits(player_event_group, FIFO_READY_BIT); 
    }
}
void FIFO_READY_WAIT(void){
    xEventGroupWaitBits(player_event_group, FIFO_READY_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
}

// 检查FIFO是否为0
bool check_FIFO_NOTREADY(void) {
    EventBits_t bits = xEventGroupWaitBits(player_event_group,
                                           FIFO_READY_BIT,
                                           pdFALSE,   // 不清除位
                                           pdFALSE,   // 等待任意一个位
                                           0);        // 不阻塞，立即返回
    return (bits & FIFO_READY_BIT) == 0;
}

// 4. 外部调用的【播放】接口
void audio_player_play(void) {
    if (player_event_group != NULL) {
        ESP_LOGI(TAG, "▶️ 发出 [播放] 指令");
        // 清除残留的停止信号，并设置播放信号
        xEventGroupSetBits(player_event_group, PLAYER_PLAY_BIT); 
    }
}
// 5. 外部调用的【停止】接口
void audio_player_stop(void) {
    if (player_event_group != NULL) {
        ESP_LOGI(TAG, "⏹️ 发出 [停止] 指令");
        // 清除残留的播放信号，并设置停止信号
        xEventGroupClearBits(player_event_group, PLAYER_PLAY_BIT);
    }
}

// 等待开始信号置1   置1了之间往下  有开始信号返回true
void wait_player_start(void) {
    EventBits_t bits =xEventGroupWaitBits(player_event_group, PLAYER_PLAY_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
}

// 检查开始信号是否为0
bool check_player_notstart(void) {
    EventBits_t bits = xEventGroupWaitBits(player_event_group,
                                           PLAYER_PLAY_BIT,
                                           pdFALSE,   // 不清除位
                                           pdFALSE,   // 等待任意一个位
                                           0);        // 不阻塞，立即返回
    return (bits & PLAYER_PLAY_BIT) == 0;
}

// ───────────────────────────────────────────────
//  【新增】诊断统计结构体
// ───────────────────────────────────────────────
typedef struct {
    uint64_t total_read;              // 改为 64 位，防止长时间溢出
    uint32_t ringbuf_send_success;
    uint32_t ringbuf_send_timeout;
    uint32_t ringbuf_send_fail;
    uint64_t last_read_time;
    uint64_t last_send_time;
    uint32_t max_timestamp_gap;
    // 速度测量
    uint64_t last_speed_calc_time;    // 上次计算速度的时间（微秒）
    uint64_t last_speed_bytes;        // 上次计算时的累计字节数
    float    current_speed_kbps;      // 最近 5 秒平均速度（kbps）
} http_stats_t;

static http_stats_t http_stats = {0};

// Wi-Fi 事件处理回调
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGW(TAG, "Wi-Fi 断开，尝试重连...");
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "获得 IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    wifi_event_group = xEventGroupCreate();
    if (wifi_event_group == NULL)
    {
        ESP_LOGE(TAG, "创建事件组失败");
        return;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);

    ESP_LOGI(TAG, "Wi-Fi 初始化完成");
}

static esp_http_client_handle_t player_http_connect(const char *url) 
{
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 30000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = HTTP_READ_BUF_SIZE,
        .keep_alive_enable = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) return NULL;

    esp_http_client_transport_t transport = esp_http_client_get_transport_type(client);
    set_audio_config_by_transport(transport); // 你之前的函数
    
    esp_err_t err = esp_http_client_open(client, 0);//函数下面就释放完TCP的内存了
    //然后可以创建FIFO了
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return NULL;
    }

    int status_code = esp_http_client_get_status_code(client);
    if (status_code != 200) {
        esp_http_client_cleanup(client);
        return NULL;
    }

    return client;
}

//buffer初始化成功
static bool FIFO_Init(void) 
{
    if (audio_buffer_init() == 0) {//buffer初始化成功
        return true;
    }
    return false;
}


//注意  这个一直拉取的循环需要可以退出来
static int player_stream_fetch_loop(esp_http_client_handle_t client, char *http_buffer) 
{
    int read_len = 0;
    uint32_t timeout_count = 0;
    bool need_read = true;

    while (1) 
    {
        // 1. 检查是否收到外部停止信号
        if (check_player_notstart() ||check_FIFO_NOTREADY()) {
            return 1; // 1 表示被强行打断
        }

        // 2. 从网络读数据
        if (need_read) {
            read_len = esp_http_client_read(client, http_buffer, HTTP_READ_BUF_SIZE);
            need_read = false; 
        }
        
        // 3. 处理读到的数据
        if (read_len > 0) {
            BaseType_t res = xRingbufferSend(audio_ringbuf, (void *)http_buffer, read_len, pdMS_TO_TICKS(HTTP_SEND_TIMEOUT));

            if (res == pdTRUE) {
                need_read = true; 
                timeout_count = 0; // 发送成功，重置超时计数
            } else if (res == pdFALSE) {
                if (is_playback_stopped()) return 1; 
                timeout_count++;
                if (timeout_count > 100) return -1; // 缓冲区塞不进去了，当做错误处理
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        } 
        else if (read_len == 0) {
            return 0; // 0 表示正常读完一首歌
        } 
        else {
            if (read_len == ESP_ERR_HTTP_EAGAIN) {
                if (is_playback_stopped()) return 1; 
                vTaskDelay(pdMS_TO_TICKS(10));
                need_read = true; 
            } else {
                return -1; // -1 表示网络错误断线
            }
        }
    }
}

// ───────────────────────────────────────────────
//  HTTP 请求任务（含详细诊断）
// ───────────────────────────────────────────────
void http_request_task(void *pvParameters)
{
    ESP_LOGI(TAG, "🎵 音频后台任务已启动，进入待命状态...");
    
    char *http_buffer = malloc(HTTP_READ_BUF_SIZE);
    if (!http_buffer) {
        vTaskDelete(NULL); return;
    }
    while (1) 
    {
        // 2. 确保 Wi-Fi 已连接
        xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

        int retry_count = 0;

        // 3. 播放与重试循环
        while (retry_count < HTTP_MAX_RETRY) 
        {
            wait_player_start();
            ESP_LOGI(TAG, "========== 尝试连接 #%d ==========", retry_count + 1);
            //想想   因为会断线重连  然后每一回都要创建销毁FIFO,
            // 【执行积木 1】发起连接
            esp_http_client_handle_t client = player_http_connect(REQUEST_URL);
            if (client == NULL) {
                goto RETRY_DELAY;
            }
            // 初始化FIFO
            if (!FIFO_Init()) {
                esp_http_client_cleanup(client);
                goto RETRY_DELAY;
            }
            //FIFO ready好了  然后置FIFO准备好
            FIFO_READY_SET();
            // 【执行积木 3】进入核心拉流循环   网络不好或者接收到停止指令  
            int stream_result = player_stream_fetch_loop(client, http_buffer);

            // ======== 清理阶段 ========
            esp_http_client_close(client);
            esp_http_client_cleanup(client);

            FIFO_READY_CLEAR();//给FIFO没准备好信号
            DECODE_STOPED_WAIT();//等解码任务收到停止播放信号 

            
            audio_deinit();//释放解码任务内存
            vTaskDelay(2);

            // ======== 结果判断阶段 ========
            if (stream_result == 1) {
                ESP_LOGI(TAG, "🛑 [打断] 播放已强行终止");
                break; // 跳出重试循环，回最外层睡眠
            } 
            else if (stream_result == 0) {
                ESP_LOGI(TAG, "✅ [完成] 一首音频完整播放完毕");
                break; // 跳出重试循环，回最外层睡眠
            }
            else {
                ESP_LOGW(TAG, "⚠️ [断线] 流读取异常，准备重试");
                // 往下走到 RETRY_DELAY
            }

RETRY_DELAY:
            // 重试前的延时，并且支持被停止信号打断
            retry_count++;
            if (check_player_notstart()) break;
        } // end 最大重试次数循环
    } // end 最外层待命循环   等播放命令
}