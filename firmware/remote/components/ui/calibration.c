// Screen-fit calibration pattern. Count the frames visible on each edge:
// frame k (k = 1..10) sits (k - 1) * 3 px in from the panel edge, and frame 6
// (15 px in) is drawn 2 px thick as a landmark. If the outermost visible frame
// on an edge is k, that edge hides about (k - 1) * 3 px.
#include <stdio.h>
#include "ui.h"
#include "epd.h"
#include "lvgl.h"

#define FRAMES  10
#define STEP    3

void ui_show_calibration(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    for (int k = 0; k < FRAMES; k++) {
        lv_obj_t *f = lv_obj_create(scr);
        lv_obj_remove_flag(f, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(f, LV_OPA_TRANSP, 0);
        lv_obj_set_style_radius(f, 0, 0);
        lv_obj_set_style_pad_all(f, 0, 0);
        lv_obj_set_style_border_color(f, lv_color_black(), 0);
        lv_obj_set_style_border_width(f, k == 5 ? 2 : 1, 0);
        int in = k * STEP;
        lv_obj_set_pos(f, in, in);
        lv_obj_set_size(f, EPD_WIDTH - 2 * in, EPD_HEIGHT - 2 * in);
    }
    // centre crosshair to spot a shifted image
    lv_obj_t *h = lv_obj_create(scr), *v = lv_obj_create(scr);
    lv_obj_t *bars[] = { h, v };
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_bg_color(bars[i], lv_color_black(), 0);
        lv_obj_set_style_bg_opa(bars[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bars[i], 0, 0);
        lv_obj_set_style_radius(bars[i], 0, 0);
    }
    lv_obj_set_size(h, 21, 1); lv_obj_set_pos(h, EPD_WIDTH / 2 - 10, EPD_HEIGHT / 2);
    lv_obj_set_size(v, 1, 21); lv_obj_set_pos(v, EPD_WIDTH / 2, EPD_HEIGHT / 2 - 10);

    lv_obj_t *l = lv_label_create(scr);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(l, lv_color_black(), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, EPD_WIDTH - 2 * FRAMES * STEP);
    lv_label_set_text(l, "TOP\n(FPC)\n\nCount the\nlines on\neach edge.\nThick = 6th");
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, FRAMES * STEP + 4);

    lv_screen_load(scr);
    ui_request_full_refresh();
}
