idf_component_register(SRCS "spotify.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_http_client esp-tls mbedtls esp_timer settings log)
