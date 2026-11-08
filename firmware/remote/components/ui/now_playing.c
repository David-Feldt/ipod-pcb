// Now-playing screen, 122 x 250 portrait, pure black on white.
//
//   [>]            87% [USB]   status bar
//   ---------------------------
//        +-------------+
//        |  album art  |       square, ~100 px
//        +-------------+
//   Title, up to two lines...
//   Artist (one line)
//   Album (one line)
//   [=========-------------]   progress
//   1:23              3:45
//
// Every text field has a fixed number of lines sized from its font's real
// line height and truncates with "..." so nothing spills into its neighbour.
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "epd.h"
#include "lvgl.h"

#define PAD        4
#define BAR_W      (s_w - 2 * PAD)
#define VOL_SHOW_MS 1500
#define NOTICE_SHOW_MS 3000

static lv_obj_t *s_play, *s_batt, *s_title, *s_artist, *s_album, *s_msg;
static lv_obj_t *s_bar, *s_elapsed, *s_total;
static lv_obj_t *s_art, *s_art_placeholder;
static lv_obj_t *s_vol_box, *s_vol_label, *s_vol_bar;
static lv_timer_t *s_vol_timer;
static lv_obj_t *s_np_screen;
static int s_duration_ms;
static int s_progress_px = -1;
static int s_elapsed_s = -1;
static int s_w = EPD_WIDTH, s_h = EPD_HEIGHT;   // visible area after insets

// The built-in fonts cover ASCII only. Map common typographic punctuation and
// Latin-1 accents to ASCII; anything else becomes a single '?' per run.
static void to_ascii(char *out, size_t n, const char *in)
{
    static const char latin1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";
    _Static_assert(sizeof latin1 == 65, "U+00C0..U+00FF table");
    size_t o = 0;
    bool last_unknown = false;
    const unsigned char *p = (const unsigned char *)(in ? in : "");
    while (*p && o + 4 < n) {
        uint32_t cp;
        int len;
        if (*p < 0x80) { cp = *p; len = 1; }
        else if ((*p & 0xE0) == 0xC0 && p[1]) { cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F); len = 2; }
        else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) {
            cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); len = 3;
        } else if ((*p & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) { cp = 0x1FFFFF; len = 4; }
        else { cp = 0x1FFFFF; len = 1; }
        p += len;

        const char *rep = NULL;
        char one[2] = { 0, 0 };
        if (cp < 0x80) { one[0] = (char)cp; rep = one; }
        else if (cp >= 0xC0 && cp <= 0xFF) { one[0] = latin1[cp - 0xC0]; rep = one; }
        else if (cp == 0xA0) rep = " ";
        else if (cp == 0x2018 || cp == 0x2019 || cp == 0x201B || cp == 0xB4 || cp == 0x2032) rep = "'";
        else if (cp == 0x201C || cp == 0x201D || cp == 0x2033) rep = "\"";
        else if (cp >= 0x2010 && cp <= 0x2015) rep = "-";
        else if (cp == 0x2026) rep = "...";
        else if (cp == 0x2022 || cp == 0xB7) rep = "*";

        if (rep) {
            for (; *rep && o + 1 < n; rep++) out[o++] = *rep;
            last_unknown = false;
        } else if (!last_unknown) {
            out[o++] = '?';
            last_unknown = true;
        }
    }
    out[o] = 0;
}

static void set_text(lv_obj_t *l, const char *utf8)
{
    char buf[160];
    to_ascii(buf, sizeof buf, utf8);
    if (strcmp(lv_label_get_text(l), buf)) lv_label_set_text(l, buf);
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, int x, int y, int w)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_black(), 0);
    lv_obj_set_pos(l, x, y);
    if (w > 0) lv_obj_set_width(l, w);
    lv_label_set_text(l, "");
    return l;
}

// Label clipped to `lines` lines of its font, truncating with "...".
static lv_obj_t *text_block(lv_obj_t *parent, const lv_font_t *font, int y, int lines)
{
    lv_obj_t *l = label(parent, font, PAD, y, BAR_W);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(l, lines * lv_font_get_line_height(font));
    return l;
}

static int bottom_of(lv_obj_t *o)
{
    lv_obj_update_layout(o);
    return lv_obj_get_y(o) + lv_obj_get_height(o);
}

static void style_box(lv_obj_t *o, int border)
{
    lv_obj_set_style_bg_color(o, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, lv_color_black(), 0);
    lv_obj_set_style_border_width(o, border, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *bar(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *b = lv_bar_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    style_box(b, 1);
    lv_obj_set_style_bg_color(b, lv_color_black(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(b, 0, LV_PART_INDICATOR);
    lv_obj_set_style_anim_duration(b, 0, 0);
    return b;
}

static void fmt_time(char *out, size_t n, int ms)
{
    int s = ms / 1000;
    snprintf(out, n, "%d:%02d", s / 60, s % 60);
}

static void vol_hide_cb(lv_timer_t *t)
{
    lv_obj_add_flag(s_vol_box, LV_OBJ_FLAG_HIDDEN);
    lv_timer_pause(t);
}

void np_create(const ui_insets_t *in)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    style_box(scr, 0);
    // Everything lives in a root container covering only the visible area,
    // so screen calibration (insets) moves the whole layout.
    s_w = EPD_WIDTH - in->left - in->right;
    s_h = EPD_HEIGHT - in->top - in->bottom;
    lv_obj_t *root = lv_obj_create(scr);
    style_box(root, 0);
    lv_obj_set_pos(root, in->left, in->top);
    lv_obj_set_size(root, s_w, s_h);

    const lv_font_t *f_title = &lv_font_montserrat_14, *f_small = &lv_font_montserrat_12;
    const int lh_small = lv_font_get_line_height(f_small);

    // status bar
    s_play = label(root, f_title, PAD, 1, 0);
    s_batt = label(root, f_small, 0, 2, s_w - PAD);
    lv_obj_set_style_text_align(s_batt, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_t *line = lv_obj_create(root);
    style_box(line, 0);
    lv_obj_set_style_bg_color(line, lv_color_black(), 0);
    lv_obj_set_pos(line, 0, 18);
    lv_obj_set_size(line, s_w, 1);

    // bottom block, laid out upwards from the bottom edge
    int y_times = s_h - lh_small - 1;
    int y_bar = y_times - 8;
    s_elapsed = label(root, f_small, PAD, y_times, 0);
    s_total = label(root, f_small, 0, y_times, s_w - PAD);
    lv_obj_set_style_text_align(s_total, LV_TEXT_ALIGN_RIGHT, 0);
    s_bar = bar(root, PAD, y_bar, BAR_W, 6);
    lv_bar_set_range(s_bar, 0, BAR_W);          // one unit per pixel

    // album art: the largest square that leaves room for the track text
    // (2 title lines + artist + album) between it and the progress bar
    int text_h = 2 * lv_font_get_line_height(f_title) + 2 * lh_small + 6;
    int art = y_bar - 4 - text_h - 22;
    if (art > s_w - 2 * PAD) art = s_w - 2 * PAD;
    s_art = lv_obj_create(root);
    style_box(s_art, 1);
    lv_obj_set_size(s_art, art, art);
    lv_obj_set_pos(s_art, (s_w - art) / 2, 22);
    s_art_placeholder = label(s_art, &lv_font_montserrat_20, 0, 0, 0);
    lv_label_set_text(s_art_placeholder, LV_SYMBOL_AUDIO);
    lv_obj_center(s_art_placeholder);

    // track text between the art and the progress bar
    int y = 22 + art + 3;
    s_title = text_block(root, f_title, y, 2);
    y = bottom_of(s_title) + 1;
    s_artist = text_block(root, f_small, y, 1);
    y = bottom_of(s_artist);
    s_album = text_block(root, f_small, y, 1);
    s_msg = text_block(root, f_title, 22 + art + 3, 3);
    lv_obj_set_style_text_align(s_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(s_msg, LV_OBJ_FLAG_HIDDEN);

    // volume overlay
    s_vol_box = lv_obj_create(root);
    style_box(s_vol_box, 2);
    lv_obj_set_size(s_vol_box, s_w - 16, 52);
    lv_obj_align(s_vol_box, LV_ALIGN_CENTER, 0, 0);
    s_vol_label = label(s_vol_box, &lv_font_montserrat_16, 0, 6, s_w - 20);
    lv_obj_set_style_text_align(s_vol_label, LV_TEXT_ALIGN_CENTER, 0);
    s_vol_bar = bar(s_vol_box, 8, 30, s_w - 36, 10);
    lv_bar_set_range(s_vol_bar, 0, 100);
    lv_obj_add_flag(s_vol_box, LV_OBJ_FLAG_HIDDEN);
    s_vol_timer = lv_timer_create(vol_hide_cb, VOL_SHOW_MS, NULL);
    lv_timer_pause(s_vol_timer);

    np_set_playing(false);
    s_np_screen = scr;
    lv_screen_load(scr);
}

void np_show(void) { if (s_np_screen) lv_screen_load(s_np_screen); }

void np_set_status(const ui_status_t *st)
{
    char buf[32] = "";
    if (st->battery_pct >= 0) snprintf(buf, sizeof buf, "%d%%", st->battery_pct);
    if (st->usb) snprintf(buf + strlen(buf), sizeof buf - strlen(buf), " " LV_SYMBOL_CHARGE);
    if (st->wifi) snprintf(buf + strlen(buf), sizeof buf - strlen(buf), " " LV_SYMBOL_WIFI);
    if (strcmp(lv_label_get_text(s_batt), buf)) lv_label_set_text(s_batt, buf);
}

void np_set_track(const char *title, const char *artist, const char *album, int duration_ms)
{
    set_text(s_title, title);
    set_text(s_artist, artist);
    set_text(s_album, album);
    s_duration_ms = duration_ms;
    char t[12];
    fmt_time(t, sizeof t, duration_ms);
    lv_label_set_text(s_total, t);
    s_progress_px = -1;
    s_elapsed_s = -1;
    np_set_progress(0);
}

void np_set_progress(int progress_ms)
{
    if (progress_ms < 0) progress_ms = 0;
    if (s_duration_ms > 0 && progress_ms > s_duration_ms) progress_ms = s_duration_ms;
    int px = s_duration_ms > 0 ? (int)((int64_t)progress_ms * BAR_W / s_duration_ms) : 0;
    if (px != s_progress_px) {
        s_progress_px = px;
        lv_bar_set_value(s_bar, px, LV_ANIM_OFF);
    }
    int sec = progress_ms / 1000;
    if (sec != s_elapsed_s) {
        s_elapsed_s = sec;
        char t[12];
        fmt_time(t, sizeof t, progress_ms);
        lv_label_set_text(s_elapsed, t);
    }
}

void np_set_playing(bool playing)
{
    lv_label_set_text(s_play, playing ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE);
}

void np_show_volume(int pct)
{
    char buf[16];
    snprintf(buf, sizeof buf, "Volume %d%%", pct);
    lv_label_set_text(s_vol_label, buf);
    lv_bar_set_value(s_vol_bar, pct, LV_ANIM_OFF);
    lv_obj_remove_flag(s_vol_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_vol_box, LV_OBJ_FLAG_HIDDEN);
    lv_timer_set_period(s_vol_timer, VOL_SHOW_MS);
    lv_timer_reset(s_vol_timer);
    lv_timer_resume(s_vol_timer);
}

void np_show_notice(const char *msg)
{
    set_text(s_vol_label, msg);
    lv_obj_add_flag(s_vol_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_vol_box, LV_OBJ_FLAG_HIDDEN);
    lv_timer_set_period(s_vol_timer, NOTICE_SHOW_MS);
    lv_timer_reset(s_vol_timer);
    lv_timer_resume(s_vol_timer);
}

void np_show_message(const char *msg)
{
    bool show = msg && *msg;
    lv_obj_t *track[] = { s_title, s_artist, s_album, s_bar, s_elapsed, s_total };
    for (size_t i = 0; i < sizeof track / sizeof track[0]; i++) {
        if (show) lv_obj_add_flag(track[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(track[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (show) {
        set_text(s_msg, msg);
        lv_obj_remove_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
    }
}
