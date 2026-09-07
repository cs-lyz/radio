#include <stdio.h>
#include <string.h>
#include <ctype.h>   // 需要包含此头文件以使用 isxdigit
#include "WebServer.h"

static const char *TAG = "SERVER";

// --- 声明嵌入的 HTML 文件指针 ---
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

// ==========================================
// 辅助函数：URL 解码 (将 %3A 转成 : , %2F 转成 /)
// ==========================================
static void url_decode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit((unsigned char)a) && isxdigit((unsigned char)b))) {
            if (a >= 'a') a -= 'a' - 'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a' - 'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';
            *dst++ = 16 * a + b;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

// ==========================================
// 路由 1: 客户端请求网页 (GET /)
// ==========================================
static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    size_t html_size = (index_html_end - index_html_start);
    httpd_resp_send(req, (const char *)index_html_start, html_size);
    return ESP_OK;
}

// ==========================================
// 路由 2: 客户端发送控制指令 (GET /control)
// ==========================================
static esp_err_t control_get_handler(httpd_req_t *req)
{
    char buf[256];
    char action[16] = {0};
    char stream_url[256] = {0};

    // 获取 URL 后面跟的参数长度
    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1 && buf_len <= sizeof(buf)) {
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            
            // 提取 action 参数
            if (httpd_query_key_value(buf, "action", action, sizeof(action)) == ESP_OK) {
                
                if (strcmp(action, "play") == 0) {
                    // 提取 url 参数
                    if (httpd_query_key_value(buf, "url", stream_url, sizeof(stream_url)) == ESP_OK) {
                        char decoded_url[256] = {0};
                        
                        // *** 关键修复：进行 URL 解码 ***
                        url_decode(decoded_url, stream_url);
                        
                        ESP_LOGI(TAG, "原始接收到的网址: %s", stream_url);
                        ESP_LOGI(TAG, "解码后的正常网址: %s", decoded_url);
                        
                        // 将解码后的 URL 赋值给 REQUEST_URL
                        snprintf(REQUEST_URL, sizeof(REQUEST_URL), "%s", decoded_url);
                        
                        audio_player_play();
                        httpd_resp_send(req, "✅ 正在播放", HTTPD_RESP_USE_STRLEN);
                    } else {
                        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing url parameter");
                    }
                }
                else if (strcmp(action, "stop") == 0) {
                    ESP_LOGI(TAG, "==== 收到停止指令 ====");
                    audio_player_stop();
                    httpd_resp_send(req, "⏹ 已停止", HTTPD_RESP_USE_STRLEN);
                }
            }
        }
    } else {
        httpd_resp_send(req, "❌ 参数错误", HTTPD_RESP_USE_STRLEN);
    }
    return ESP_OK;
}

// ==========================================
// 启动 Web 服务器
// ==========================================
httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    ESP_LOGI(TAG, "启动 Web Server...");
    if (httpd_start(&server, &config) == ESP_OK) {
        
        httpd_uri_t uri_root = {
            .uri      = "/",
            .method   = HTTP_GET,
            .handler  = root_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_root);

        httpd_uri_t uri_control = {
            .uri      = "/control",
            .method   = HTTP_GET,
            .handler  = control_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_control);
        
        return server;
    }
    return NULL;
}