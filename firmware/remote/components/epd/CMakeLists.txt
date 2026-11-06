idf_component_register(SRCS "epd.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES board esp_driver_gpio esp_driver_spi esp_timer esp_rom log)
