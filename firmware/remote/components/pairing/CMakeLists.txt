idf_component_register(SRCS "pairing.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_http_server esp_timer esp_hw_support mbedtls net spotify log)
