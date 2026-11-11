// Bluetooth remote screen, 122 x 250 portrait: shown while a phone is
// connected. It draws the button cross as it sits on the board, so the
// controls can be read at a glance.
//
//   Bluetooth           87% [USB]   title + status
//   ---------------------------
//          Connected
//        .-----MENU-----.
//       |                |
//       | |<<  (>||)  >>| |         ring = the four buttons, centre = SEL
//       |                |
//        '--------------'
//      (vol) Wheel: volume
//       No Wi-Fi in range            footer (only after an automatic switch)
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "epd.h"
#include "lvgl.h"

#define PAD     4
#define RING_D  100
#define SEL_D   42

static lv_obj_t *s_scr, *s_status, *s_batt, *s_footer;

static void plain(lv_obj_t *o)
{
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, const char *s, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, s);
    return l;
}

static lv_obj_t *circle(lv_obj_t *parent, int d, bool filled)
{
    lv_obj_t *c = lv_obj_create(parent);
    plain(c);
    lv_obj_set_size(c, d, d);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(c, lv_color_black(), 0);
    lv_obj_set_style_border_width(c, 2, 0);
    lv_obj_set_style_bg_color(c, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(c, filled ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    return c;
}

void bt_screen_create(const ui_insets_t *in)
{
    int w = EPD_WIDTH - in->left - in->right, h = EPD_HEIGHT - in->top - in->bottom;
    s_scr = lv_obj_create(NULL);
    plain(s_scr);
    lv_obj_set_style_bg_color(s_scr, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_t *root = lv_obj_create(s_scr);
    plain(root);
    lv_obj_set_pos(root, in->left, in->top);
    lv_obj_set_size(root, w, h);

    lv_obj_t *title = text(root, &lv_font_montserrat_14, LV_SYMBOL_BLUETOOTH " Bluetooth", lv_color_black());
    lv_obj_set_pos(title, PAD, 1);
    s_batt = text(root, &lv_font_montserrat_12, "", lv_color_black());
    lv_obj_set_width(s_batt, w - PAD);
    lv_obj_set_style_text_align(s_batt, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s_batt, 0, 3);
    lv_obj_t *line = lv_obj_create(root);
    plain(line);
    lv_obj_set_style_bg_color(line, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_pos(line, 0, 18);
    lv_obj_set_size(line, w, 1);

    s_status = text(root, &lv_font_montserrat_16, "", lv_color_black());
    lv_obj_set_width(s_status, w);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_status, 0, 28);

    // The cross: ring with MENU / prev / next / (PLAY: unused) around SEL.
    int ring_y = 60;
    lv_obj_t *ring = circle(root, RING_D, false);
    lv_obj_set_pos(ring, (w - RING_D) / 2, ring_y);
    lv_obj_t *menu = text(ring, &lv_font_montserrat_12, "MENU", lv_color_black());
    lv_obj_align(menu, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_t *prev = text(ring, &lv_font_montserrat_16, LV_SYMBOL_PREV, lv_color_black());
    lv_obj_align(prev, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_t *next = text(ring, &lv_font_montserrat_16, LV_SYMBOL_NEXT, lv_color_black());
    lv_obj_align(next, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_t *sel = circle(ring, SEL_D, true);
    lv_obj_center(sel);
    lv_obj_t *pp = text(sel, &lv_font_montserrat_12, LV_SYMBOL_PLAY LV_SYMBOL_PAUSE, lv_color_white());
    lv_obj_center(pp);

    lv_obj_t *wheel = text(root, &lv_font_montserrat_14, LV_SYMBOL_VOLUME_MAX "  Wheel: volume", lv_color_black());
    lv_obj_set_width(wheel, w);
    lv_obj_set_style_text_align(wheel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(wheel, 0, ring_y + RING_D + 14);

    s_footer = text(root, &lv_font_montserrat_12, "", lv_color_black());
    lv_obj_set_width(s_footer, w);
    lv_obj_set_style_text_align(s_footer, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_footer, 0, h - lv_font_get_line_height(&lv_font_montserrat_12) - 2);
}

static void set_text(lv_obj_t *l, const char *s)
{
    if (strcmp(lv_label_get_text(l), s ? s : "")) lv_label_set_text(l, s ? s : "");
}

void bt_screen_show(const char *status, const char *footer)
{
    if (!s_scr) return;
    set_text(s_status, status);
    set_text(s_footer, footer);
    if (lv_screen_active() != s_scr) {
        lv_screen_load(s_scr);
        ui_request_full_refresh();
    }
}

void bt_screen_set_battery(int pct, bool usb)
{
    if (!s_scr) return;
    char buf[24] = "";
    if (pct >= 0) snprintf(buf, sizeof buf, "%d%%", pct);
    if (usb) snprintf(buf + strlen(buf), sizeof buf - strlen(buf), " " LV_SYMBOL_CHARGE);
    set_text(s_batt, buf);
}
