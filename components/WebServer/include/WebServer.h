#ifndef WEBSERVER_H
#define WEBSERVER_H
#include <string.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "WIFI.h"

httpd_handle_t start_webserver(void);
#endif


