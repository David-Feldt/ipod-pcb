idf_component_register(SRCS "app_console.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES console board lowbatt btremote settings ui epd net spotify player pairing wifisetup espressif__cjson esp_timer esp_app_format esp_driver_tsens heap)
