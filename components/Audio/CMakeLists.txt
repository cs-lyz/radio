idf_component_register(SRCS "Audio.c"
                    INCLUDE_DIRS "include"
                    REQUIRES  esp_ringbuf I2S esp_timer Common_buf esp_http_client WIFI
                    )
