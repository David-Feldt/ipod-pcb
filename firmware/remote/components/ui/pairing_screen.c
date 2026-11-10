// Pairing screen, 122 x 250 portrait:
//
//   Sign in to Spotify        title
//   +-----------+
//   |  QR code  |             http://<ip>/ (hidden when NULL)
//   +-----------+
//   192.168.1.42              big line
//   Scan, or open this ...    small text
//   MENU: cancel              footer
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "epd.h"
#include "lvgl.h"

#define PAD     4
#define QR_SIZE 100         // 25 modules (version 2) x 4 px; longer text drops to 3 px modules

static lv_obj_t *s_scr, *s_title, *s_qr, *s_big, *s_small, *s_footer;
static int s_w, s_h;
static char s_qr_text[48];

static lv_obj_t *centered_label(lv_obj_t *parent, const lv_font_t *font, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_black(), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(l, s_w - 2 * PAD);
    lv_obj_set_pos(l, PAD, y);
    lv_label_set_text(l, "");
    return l;
}

void pair_screen_create(const ui_insets_t *in)
{
    s_w = EPD_WIDTH - in->left - in->right;
    s_h = EPD_HEIGHT - in->top - in->bottom;
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *root = lv_obj_create(s_scr);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(root, in->left, in->top);
    lv_obj_set_size(root, s_w, s_h);

    s_title = centered_label(root, &lv_font_montserrat_14, 2);
    s_qr = lv_qrcode_create(root);
    lv_qrcode_set_size(s_qr, QR_SIZE);
    lv_qrcode_set_dark_color(s_qr, lv_color_black());
    lv_qrcode_set_light_color(s_qr, lv_color_white());
    lv_obj_set_pos(s_qr, (s_w - QR_SIZE) / 2, 24);
    s_big = centered_label(root, &lv_font_montserrat_16, 0);
    s_small = centered_label(root, &lv_font_montserrat_12, 0);
    s_footer = centered_label(root, &lv_font_montserrat_12, s_h - lv_font_get_line_height(&lv_font_montserrat_12) - 2);
}

void pair_screen_show(const char *title, const char *qr_text, const char *big, const char *small, const char *footer)
{
    if (!s_scr) return;
    lv_label_set_text(s_title, title ? title : "");
    int y;
    if (qr_text && qr_text[0]) {
        if (strcmp(qr_text, s_qr_text)) {
            snprintf(s_qr_text, sizeof s_qr_text, "%s", qr_text);
            lv_qrcode_update(s_qr, s_qr_text, strlen(s_qr_text));
        }
        lv_obj_remove_flag(s_qr, LV_OBJ_FLAG_HIDDEN);
        y = 24 + QR_SIZE + 8;
    } else {
        lv_obj_add_flag(s_qr, LV_OBJ_FLAG_HIDDEN);
        y = 60;
    }
    lv_label_set_text(s_big, big ? big : "");
    lv_obj_set_y(s_big, y);
    lv_obj_update_layout(s_big);
    lv_label_set_text(s_small, small ? small : "");
    lv_obj_set_y(s_small, y + lv_obj_get_height(s_big) + 6);
    lv_label_set_text(s_footer, footer ? footer : "");
    if (lv_screen_active() != s_scr) {
        lv_screen_load(s_scr);
        ui_request_full_refresh();
    }
}
