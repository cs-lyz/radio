idf_component_register(SRCS  "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES    nvs_flash WIFI Audio I2S
                       WebServer
                       )