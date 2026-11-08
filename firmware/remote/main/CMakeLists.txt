idf_component_register(SRCS "app_main.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES board settings app_console ui epd net spotify player esp_timer log)
