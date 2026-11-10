idf_component_register(SRCS "ui_port.c" "now_playing.c" "calibration.c" "pairing_screen.c" "menu_screen.c"
                       INCLUDE_DIRS "include"
                       REQUIRES lvgl
                       PRIV_REQUIRES epd esp_timer log)
