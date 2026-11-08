idf_component_register(SRCS "art.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_http_client esp-tls mbedtls esp_rom esp_timer log)
