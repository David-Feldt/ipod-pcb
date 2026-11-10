idf_component_register(SRCS "wifisetup.c" "dns.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_http_server esp_timer esp_hw_support esp_wifi lwip net settings log)
