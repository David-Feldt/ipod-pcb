idf_component_register(SRCS "net.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_netif freertos
                       PRIV_REQUIRES esp_wifi esp_event settings log lwip)
