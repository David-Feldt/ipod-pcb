idf_component_register(SRCS "power.c" "input.c" "mode.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_gpio esp_adc
                       PRIV_REQUIRES esp_timer esp_rom log)
