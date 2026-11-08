idf_component_register(SRCS "player.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES spotify net espressif__cjson esp_timer log)
