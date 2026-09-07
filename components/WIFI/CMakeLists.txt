idf_component_register(SRCS "WIFI.c"
                       INCLUDE_DIRS "include"
                       REQUIRES  esp_wifi esp_event nvs_flash esp_netif esp_http_client
                        mbedtls
                       esp_system
                       esp_timer  Audio )