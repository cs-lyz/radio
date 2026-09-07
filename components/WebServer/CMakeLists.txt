idf_component_register(SRCS  "WebServer.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_http_server WIFI
                    EMBED_FILES "index.html"
                    )
