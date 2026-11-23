/* Settings (after PIN): speed limit per level, wheel size, brightness, PIN; plus the demo-scene picker. */
#include <stdio.h>

#include "ui_priv.h"

#define IDLE_S 30

static lv_obj_t *lim_slider[3], *lim_val[3], *wheel_val, *bright_lbl, *bright_slider, *foot;
static int last_idle = -1;

static void sync(void)
{
    char b[96], n[12];
    for (int i = 0; i < 3; i++) {
        lv_slider_set_value(lim_slider[i], (int32_t)(S.limits[i] * 2), LV_ANIM_OFF);
        fmt_num(n, sizeof n, S.limits[i], -1);
        snprintf(b, sizeof b, "%s км/год", n);
        lv_label_set_text(lim_val[i], b);
    }
    snprintf(b, sizeof b, "%d см", S.wheel_cm);
    lv_label_set_text(wheel_val, b);
    snprintf(b, sizeof b, "Яскравість · %d%%", S.brightness);
    lv_label_set_text(bright_lbl, b);
    lv_slider_set_value(bright_slider, S.brightness / 5, LV_ANIM_OFF);
}

static void lim_cb(lv_event_t *e)
{
    const int i = (int)(intptr_t)lv_event_get_user_data(e);
    const float v = lv_slider_get_value(lv_event_get_target_obj(e)) / 2.0f;
    S.limits[i] = v;
    /* keep turtle <= hare <= cheetah */
    for (int j = i + 1; j < 3; j++) if (S.limits[j] < v) S.limits[j] = v;
    for (int j = i - 1; j >= 0; j--) if (S.limits[j] > v) S.limits[j] = v;
    sync();
}

static void wheel_cb(lv_event_t *e)
{
    const int d = (int)(intptr_t)lv_event_get_user_data(e);
    S.wheel_cm += d;
    if (S.wheel_cm < 10) S.wheel_cm = 10;
    if (S.wheel_cm > 30) S.wheel_cm = 30;
    sync();
}

static void bright_cb(lv_event_t *e)
{
    S.brightness = lv_slider_get_value(lv_event_get_target_obj(e)) * 5;
    ui_set_brightness(S.brightness);
    sync();
}

static void close_cb(lv_event_t *e)
{
    (void)e;
    state_save_settings();
    N.modal = MODAL_NONE;
    ui_apply_layers();
}

static void pin_change_cb(lv_event_t *e)
{
    (void)e;
    toast("Зміна PIN: введіть новий двічі (у демо не реалізовано)");
}

static void theme_open_cb(lv_event_t *e)
{
    (void)e;
    themes_open();
}

static void demo_open_cb(lv_event_t *e)
{
    (void)e;
    demo_open();
}

static lv_obj_t *slider(lv_obj_t *p, int32_t min, int32_t max)
{
    lv_obj_t *s = lv_slider_create(p);
    lv_slider_set_range(s, min, max);
    lv_obj_set_height(s, 12);
    lv_obj_set_style_bg_color(s, lv_color_hex(C_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, lv_color_hex(C_RED), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s, lv_color_hex(C_TEXT), LV_PART_KNOB);
    lv_obj_set_style_pad_all(s, 14, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(s, 0, LV_PART_KNOB);
    lv_obj_set_ext_click_area(s, 20);
    return s;
}

static lv_obj_t *label(lv_obj_t *p, const lv_font_t *f, uint32_t c, const char *t)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
    lv_label_set_text(l, t);
    return l;
}

static lv_obj_t *card(lv_obj_t *p, int x, const char *title, lv_obj_t **title_lbl)
{
    lv_obj_t *c = lv_obj_create(p);
    lv_obj_remove_style_all(c);
    lv_obj_set_pos(c, x, 372);
    lv_obj_set_size(c, 306, 120);
    lv_obj_set_style_radius(c, 18, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(C_DIAL), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *t = label(c, &font_r16, C_MUTED, title);
    lv_obj_set_pos(t, 20, 15);
    if (title_lbl) *title_lbl = t;
    return c;
}

void settings_create(lv_obj_t *p)
{
    static const char *const NAMES[3] = {"Черепаха", "Зайчик", "Гепард"};
    N.l_settings = ui_layer(p, true);
    lv_obj_t *L = N.l_settings;

    lv_obj_t *t = label(L, &font_s32, C_TEXT, "Налаштування");
    lv_obj_set_pos(t, 35, 30);
    lv_obj_t *close = ui_button(L, &font_r22, "Закрити", close_cb, NULL);
    lv_obj_set_size(close, 150, 56);
    lv_obj_set_pos(close, 1024 - 35 - 150, 24);
    lv_obj_t *demo = ui_button(L, &font_r22, "Демо", demo_open_cb, NULL);
    lv_obj_set_size(demo, 130, 56);
    lv_obj_set_pos(demo, 1024 - 35 - 150 - 14 - 130, 24);
    lv_obj_t *theme_btn = ui_button(L, &font_r22, "Тема", theme_open_cb, NULL);
    lv_obj_set_size(theme_btn, 130, 56);
    lv_obj_set_pos(theme_btn, 1024 - 35 - 150 - 2 * (14 + 130), 24);

    lv_obj_t *sub = label(L, &font_r16, C_MUTED, "Обмеження швидкості для рівнів");
    lv_obj_set_pos(sub, 35, 100);

    for (int i = 0; i < 3; i++) {
        const int y = 130 + 78 * i;
        lv_obj_t *ic = lv_image_create(L);
        lv_image_set_src(ic, ui_char_img(i + 1, 's'));
        lv_image_set_scale(ic, 140);
        lv_image_set_pivot(ic, 0, 0);
        lv_obj_set_pos(ic, 35, y + 12);
        lv_obj_t *n = label(L, &font_r22, C_TEXT, NAMES[i]);
        lv_obj_set_pos(n, 117, y + 24);
        lim_slider[i] = slider(L, 2, 24)   /* half-km/h steps: 1..12 km/h */;
        lv_obj_set_width(lim_slider[i], 520);
        lv_obj_set_pos(lim_slider[i], 290, y + 33);
        lv_obj_add_event_cb(lim_slider[i], lim_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)i);
        lim_val[i] = label(L, &font_r25, C_TEXT, "");
        lv_obj_set_width(lim_val[i], 150);
        lv_obj_set_style_text_align(lim_val[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(lim_val[i], 1024 - 35 - 150, y + 22);
    }

    lv_obj_t *c1 = card(L, 35, "Діаметр колеса", NULL);
    lv_obj_t *minus = ui_button(c1, &font_s32, "−", wheel_cb, (void *)(intptr_t)-1);
    lv_obj_set_size(minus, 58, 58);
    lv_obj_set_pos(minus, 20, 48);
    wheel_val = label(c1, &font_r25, C_TEXT, "");
    lv_obj_align(wheel_val, LV_ALIGN_TOP_MID, 0, 62);
    lv_obj_t *plus = ui_button(c1, &font_s32, "+", wheel_cb, (void *)(intptr_t)1);
    lv_obj_set_size(plus, 58, 58);
    lv_obj_set_pos(plus, 306 - 20 - 58, 48);

    lv_obj_t *c2 = card(L, 359, "", &bright_lbl);
    bright_slider = slider(c2, 6, 20);
    lv_obj_set_width(bright_slider, 250);
    lv_obj_set_pos(bright_slider, 28, 76);
    lv_obj_add_event_cb(bright_slider, bright_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *c3 = card(L, 683, "Безпека", NULL);
    lv_obj_t *pl = label(c3, &font_r25, C_TEXT, "PIN-код");
    lv_obj_set_pos(pl, 20, 62);
    lv_obj_t *pc = ui_button(c3, &font_r18, "Змінити", pin_change_cb, NULL);
    lv_obj_set_size(pc, 120, 56);
    lv_obj_set_pos(pc, 306 - 20 - 120, 50);

    foot = label(L, &font_r16, C_MUTED, "");
    lv_obj_set_width(foot, 1024);
    lv_obj_set_style_text_align(foot, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(foot, 0, 520);
}

void settings_open(void)
{
    sync();
    last_idle = -1;
    lv_display_trigger_activity(NULL);
    N.modal = MODAL_SETTINGS;
    ui_apply_layers();
    settings_tick();
}

void settings_tick(void)
{
    if (N.modal != MODAL_SETTINGS && N.modal != MODAL_THEMES) return;   /* the theme picker is part of settings */
    const int idle = IDLE_S - (int)(lv_display_get_inactive_time(NULL) / 1000);
    if (idle == last_idle) return;
    last_idle = idle;
    if (idle <= 0) {
        close_cb(NULL);
        return;
    }
    char b[96];
    snprintf(b, sizeof b, "Автовихід через %d с без дотиків", idle);
    lv_label_set_text(foot, b);
}

/* ---------------- demo scene picker ---------------- */
static void scene_cb(lv_event_t *e)
{
    ui_scene((int)(intptr_t)lv_event_get_user_data(e));
}

static modal_t demo_from = MODAL_NONE;

void demo_open(void)
{
    if (N.modal == MODAL_DEMO) return;
    if (N.modal == MODAL_SETTINGS) state_save_settings();
    demo_from = N.modal;
    N.modal = MODAL_DEMO;
    ui_apply_layers();
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    if (demo_from == MODAL_SETTINGS) {
        settings_open();
        return;
    }
    N.modal = demo_from == MODAL_PIN ? MODAL_PIN : MODAL_NONE;
    ui_apply_layers();
}

void demo_create(lv_obj_t *p)
{
    N.l_demo = ui_layer(p, true);
    lv_obj_t *L = N.l_demo;
    lv_obj_t *t = label(L, &font_s32, C_TEXT, "Демо-сцени");
    lv_obj_set_pos(t, 35, 30);
    lv_obj_t *back = ui_button(L, &font_r22, "Назад", back_cb, NULL);
    lv_obj_set_size(back, 150, 56);
    lv_obj_set_pos(back, 1024 - 35 - 150, 24);
    lv_obj_t *hint = label(L, &font_r16, C_MUTED, "Дані імітуються. На машині їх даватиме CAN-шина.");
    lv_obj_set_pos(hint, 35, 100);

    const int cols = 4, w = (1024 - 70 - 3 * 16) / 4, h = 68;
    for (int i = 0; i < UI_SCENE_COUNT; i++) {
        lv_obj_t *b = ui_button(L, &font_r22, UI_SCENES[i], scene_cb, (void *)(intptr_t)i);
        lv_obj_set_size(b, w, h);
        lv_obj_set_pos(b, 35 + (i % cols) * (w + 16), 140 + (i / cols) * (h + 12));
    }
}

/* ---------------- theme picker (Settings -> Тема) ---------------- */
static lv_obj_t *th_card[THEME_COUNT];

static void themes_refresh(void)
{
    for (int t = 0; t < THEME_COUNT; t++) {
        lv_obj_set_style_border_color(th_card[t], lv_color_hex(S.theme == t ? C_RED : C_LINE), 0);
        lv_obj_set_style_border_width(th_card[t], S.theme == t ? 3 : 2, 0);
    }
}

static void theme_pick_cb(lv_event_t *e)
{
    ui_set_theme((int)(intptr_t)lv_event_get_user_data(e), true);
    themes_refresh();
}

static void themes_back_cb(lv_event_t *e)
{
    (void)e;
    settings_open();
}

void themes_create(lv_obj_t *p)
{
    static const char *const hint[THEME_COUNT] = {
        "Три циферблати, як у Porsche", "LED-смуга й велика цифра", "Стрілка на класичній шкалі", "Великий персонаж і дорога",
        "Фото авто й велика швидкість"};
    N.l_themes = ui_layer(p, true);
    lv_obj_t *L = N.l_themes;
    lv_obj_t *t = label(L, &font_s32, C_TEXT, "Тема приладів");
    lv_obj_set_pos(t, 35, 30);
    lv_obj_t *back = ui_button(L, &font_r22, "Назад", themes_back_cb, NULL);
    lv_obj_set_size(back, 150, 56);
    lv_obj_set_pos(back, 1024 - 35 - 150, 24);
    lv_obj_t *sub = label(L, &font_r16, C_MUTED, "Вигляд головного екрана. Теми перемикає й свайп угору чи вниз на сторінці Їзда.");
    lv_obj_set_pos(sub, 35, 100);

    const int gap = 16, w = (1024 - 70 - 2 * gap) / 3, h = 170;   /* three columns, two rows */
    for (int i = 0; i < THEME_COUNT; i++) {
        lv_obj_t *c = ui_button(L, &font_s28, THEME_NAMES[i], theme_pick_cb, (void *)(intptr_t)i);
        th_card[i] = c;
        lv_obj_set_size(c, w, h);
        lv_obj_set_pos(c, 35 + (i % 3) * (w + gap), 170 + (i / 3) * (h + gap));
        lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *name = lv_obj_get_child(c, 0);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 6, 10);
        lv_obj_t *d = label(c, &font_r18, C_MUTED, hint[i]);
        lv_obj_set_width(d, w - 40);
        lv_obj_align(d, LV_ALIGN_TOP_LEFT, 6, 64);
    }
    themes_refresh();
}

void themes_open(void)
{
    themes_refresh();
    N.modal = MODAL_THEMES;
    ui_apply_layers();
}
