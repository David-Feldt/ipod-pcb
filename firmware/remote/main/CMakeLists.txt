idf_component_register(SRCS "app_main.c" "ble_mode.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES board lowbatt btremote settings app_console ui epd net spotify player art pairing wifisetup esp_timer log)
