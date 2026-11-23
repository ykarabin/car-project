/* Level-change card, alert overlay and toast. */
#include <stdio.h>

#include "ui_priv.h"

static const char *const NAMES[3] = {"Черепаха", "Зайчик", "Гепард"};

static lv_obj_t *lv_char, *lv_name, *lv_sub, *lv_bar[3];
static lv_timer_t *level_timer;
static lv_obj_t *al_ring, *al_icon, *al_title, *al_sub;
static lv_obj_t *toast_lbl;
static lv_timer_t *toast_timer;


static void toast_hide_cb(lv_timer_t *t)
{
    (void)t;
    toast_timer = NULL;
    lv_obj_add_flag(toast_lbl, LV_OBJ_FLAG_HIDDEN);
}

static void ok_cb(lv_event_t *e)
{
    (void)e;
    if (N.alert == ALERT_CAN) S.can_lost = false;
    if (N.alert == ALERT_OVERHEAT) S.stop = false;
    N.alert = ALERT_NONE;
    ui_apply_layers();
}

void overlays_create(lv_obj_t *p)
{
    /* level card: opaque, design 480x384 at (160,64), NOT a full-screen layer, so showing/hiding it touches
     * only ~288 kpx. Square corners on purpose: LVGL skips drawing everything under an opaque object only
     * when that object fully covers the dirty area, and a rounded corner never does (measured: rounded
     * card opened in 107 ms, a covering one skips the page below). */
    const int32_t cx = PX(160), cy = PY(64);
    lv_obj_t *card = lv_obj_create(p);
    N.l_level = card;
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, cx, cy);
    lv_obj_set_size(card, PS(480), PS(384));
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(C_DIAL), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);   /* touches pass through to the page */
    lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);

    /* children use the same design coordinates as the rest of the UI; the 2 px border shifts the content area */
    ui_origin(cx + 2, cy + 2);
    lv_char = lv_image_create(card);
    lv_obj_set_pos(lv_char, (PS(480) - img_hare_lc.header.w) / 2 - 2, PS(94 - 64) - 2);
    lv_name = ui_text(card, &font_s40, C_TEXT, ALIGN_C, 400, 340, "");
    lv_sub = ui_text(card, &font_r22, C_MUTED, ALIGN_C, 400, 372, "");
    for (int i = 0; i < 3; i++) lv_bar[i] = ui_box(card, 334 + 46 * i, 396, 40, 10, 5, C_LINE2);
    ui_origin(0, 0);

    /* alert overlay */
    N.l_alert = ui_layer(p, true);
    al_ring = lv_obj_create(N.l_alert);
    lv_obj_remove_style_all(al_ring);
    lv_obj_set_size(al_ring, 188, 188);
    lv_obj_set_pos(al_ring, 512 - 94, 95);
    lv_obj_set_style_radius(al_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(al_ring, 6, 0);
    lv_obj_set_style_border_color(al_ring, lv_color_hex(C_AMBER), 0);
    lv_obj_remove_flag(al_ring, LV_OBJ_FLAG_CLICKABLE);
    al_icon = lv_image_create(al_ring);
    lv_obj_center(al_icon);
    al_title = lv_label_create(N.l_alert);
    lv_obj_set_style_text_font(al_title, &font_s50, 0);
    lv_obj_set_style_text_color(al_title, lv_color_hex(C_TEXT), 0);
    lv_obj_align(al_title, LV_ALIGN_TOP_MID, 0, 310);
    al_sub = lv_label_create(N.l_alert);
    lv_obj_set_style_text_font(al_sub, &font_r22, 0);
    lv_obj_set_style_text_color(al_sub, lv_color_hex(C_MUTED), 0);
    lv_obj_align(al_sub, LV_ALIGN_TOP_MID, 0, 378);
    lv_obj_t *ok = ui_button(N.l_alert, &font_r22, "Зрозуміло", ok_cb, NULL);
    lv_obj_set_size(ok, 230, 64);
    lv_obj_align(ok, LV_ALIGN_TOP_MID, 0, 438);
}

void toast(const char *txt)
{
    if (!toast_lbl) {
        toast_lbl = lv_label_create(lv_layer_top());
        lv_obj_set_style_text_font(toast_lbl, &font_r18, 0);
        lv_obj_set_style_text_color(toast_lbl, lv_color_hex(C_TEXT), 0);
        lv_obj_set_style_bg_color(toast_lbl, lv_color_hex(0x1b1e24), 0);
        lv_obj_set_style_bg_opa(toast_lbl, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(toast_lbl, lv_color_hex(C_LINE2), 0);
        lv_obj_set_style_border_width(toast_lbl, 1, 0);
        lv_obj_set_style_radius(toast_lbl, 14, 0);
        lv_obj_set_style_pad_hor(toast_lbl, 22, 0);
        lv_obj_set_style_pad_ver(toast_lbl, 12, 0);
    }
    lv_label_set_text(toast_lbl, txt);
    lv_obj_align(toast_lbl, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_obj_remove_flag(toast_lbl, LV_OBJ_FLAG_HIDDEN);
    if (toast_timer) lv_timer_delete(toast_timer);
    toast_timer = lv_timer_create(toast_hide_cb, 2200, NULL);
    lv_timer_set_repeat_count(toast_timer, 1);
}

static void level_hide_cb(lv_timer_t *t)
{
    (void)t;
    level_timer = NULL;
    lv_obj_add_flag(N.l_level, LV_OBJ_FLAG_HIDDEN);
}

void level_show(int n)
{
    char b[96], v[12];
    lv_image_set_src(lv_char, ui_char_img(n, 'c'));
    ui_text_set(lv_name, NAMES[n - 1]);
    fmt_num(v, sizeof v, S.limits[n - 1], -1);
    snprintf(b, sizeof b, "рівень %d · до %s км/год", n, v);
    ui_text_set(lv_sub, b);
    for (int i = 0; i < 3; i++)
        lv_obj_set_style_bg_color(lv_bar[i], lv_color_hex(i < n ? C_RED : C_LINE2), 0);

    lv_obj_remove_flag(N.l_level, LV_OBJ_FLAG_HIDDEN);

    if (level_timer) lv_timer_delete(level_timer);
    level_timer = lv_timer_create(level_hide_cb, 2200, NULL);
    lv_timer_set_repeat_count(level_timer, 1);
}

void alert_show(alert_t a)
{
    static const char *const T[][2] = {
        {"", ""},
        {"Мало заряду", "Час їхати додому на зарядку"},
        {"Мотор гарячий", "Зроби перерву 10 хвилин"},
        {"Немає зв'язку", "Блоки не відповідають. Перевір з’єднання CAN"},
    };
    static const lv_image_dsc_t *const I[] = {NULL, &img_ic_lowbat, &img_ic_overheat, &img_ic_can};
    if (a == ALERT_NONE) {
        ui_pulse(al_ring, false, LV_PART_MAIN);
        return;
    }
    lv_image_set_src(al_icon, I[a]);
    lv_label_set_text(al_title, T[a][0]);
    lv_label_set_text(al_sub, T[a][1]);
    ui_pulse(al_ring, true, LV_PART_MAIN);
}
