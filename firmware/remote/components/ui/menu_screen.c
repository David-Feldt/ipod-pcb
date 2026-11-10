// Menu screen, 122 x 250 portrait: a title, a list with the selected item
// drawn inverted (white on black), and a short key hint at the bottom.
#include <stdio.h>
#include "ui.h"
#include "epd.h"
#include "lvgl.h"

#define PAD     4
#define ITEM_H  24
#define LIST_Y  24

static lv_obj_t *s_scr, *s_items[MENU_MAX_ITEMS];

static void plain(lv_obj_t *o)
{
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

void menu_screen_create(const ui_insets_t *in)
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

    lv_obj_t *title = lv_label_create(root);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    lv_label_set_text(title, "Menu");
    lv_obj_set_pos(title, PAD, 1);
    lv_obj_t *line = lv_obj_create(root);
    plain(line);
    lv_obj_set_style_bg_color(line, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_pos(line, 0, 18);
    lv_obj_set_size(line, w, 1);

    for (int i = 0; i < MENU_MAX_ITEMS; i++) {
        lv_obj_t *l = lv_label_create(root);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_obj_set_style_bg_color(l, lv_color_black(), 0);
        lv_obj_set_style_pad_left(l, PAD, 0);
        lv_obj_set_style_pad_top(l, (ITEM_H - lv_font_get_line_height(&lv_font_montserrat_14)) / 2, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_pos(l, 0, LIST_Y + i * ITEM_H);
        lv_obj_set_size(l, w, ITEM_H);
        lv_label_set_text(l, "");
        s_items[i] = l;
    }

    lv_obj_t *hint = lv_label_create(root);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, lv_color_black(), 0);
    lv_label_set_text(hint, "MENU / PLAY: move\nCentre: choose\nLeft: back");
    lv_obj_set_pos(hint, PAD, h - 3 * lv_font_get_line_height(&lv_font_montserrat_12) - 2);
}

void menu_screen_show(const char *const *items, int n, int selected)
{
    if (!s_scr) return;
    for (int i = 0; i < MENU_MAX_ITEMS; i++) {
        lv_obj_t *l = s_items[i];
        if (i >= n) { lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_remove_flag(l, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(l, items[i]);
        bool sel = i == selected;
        lv_obj_set_style_bg_opa(l, sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(l, sel ? lv_color_white() : lv_color_black(), 0);
    }
    if (lv_screen_active() != s_scr) {
        lv_screen_load(s_scr);
        ui_request_full_refresh();
    }
}
