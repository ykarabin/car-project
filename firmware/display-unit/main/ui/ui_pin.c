/* Parent PIN screen: 4 digits, 3 wrong tries -> 15 s lockout. */
#include <stdio.h>
#include <string.h>

#include "esp_timer.h"
#include "ui_priv.h"

static lv_obj_t *dots_box, *dots[4], *msg, *keys[12];
static char entry[5];
static int fails;
static int64_t lock_until_us;
static lv_timer_t *lock_timer;

static void draw_dots(void)
{
    const size_t n = strlen(entry);
    for (int i = 0; i < 4; i++) {
        const bool f = (size_t)i < n;
        lv_obj_set_style_bg_opa(dots[i], f ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(dots[i], lv_color_hex(f ? C_TEXT : C_LINE2), 0);
    }
}

static void shake_cb(void *o, int32_t v) { lv_obj_set_style_translate_x(o, v, 0); }
static void shake_done(lv_anim_t *a)
{
    lv_obj_set_style_translate_x(a->var, 0, 0);
    entry[0] = 0;
    draw_dots();
}

static void lock_tick(lv_timer_t *t)
{
    (void)t;
    const int left = (int)((lock_until_us - esp_timer_get_time() + 999999) / 1000000);
    for (int i = 0; i < 12; i++)
        if (keys[i]) lv_obj_set_state(keys[i], LV_STATE_DISABLED, left > 0);
    if (left > 0) {
        char b[96];
        snprintf(b, sizeof b, "Забагато спроб. Зачекайте %d с", left);
        lv_label_set_text(msg, b);
    } else {
        lv_label_set_text(msg, "");
        lv_timer_delete(lock_timer);
        lock_timer = NULL;
    }
}

static void key_cb(lv_event_t *e)
{
    const char k = (char)(intptr_t)lv_event_get_user_data(e);
    if (esp_timer_get_time() < lock_until_us) return;
    size_t n = strlen(entry);
    if (n >= 4) return;   /* shake in progress */
    if (k == '<') {
        if (n) entry[n - 1] = 0;
    } else {
        entry[n] = k;
        entry[n + 1] = 0;
    }
    draw_dots();
    if (strlen(entry) < 4) return;

    if (strcmp(entry, S.pin) == 0) {
        fails = 0;
        entry[0] = 0;
        settings_open();
        return;
    }
    fails++;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, dots_box);
    lv_anim_set_exec_cb(&a, shake_cb);
    lv_anim_set_values(&a, -9, 9);
    lv_anim_set_duration(&a, 60);
    lv_anim_set_reverse_duration(&a, 60);
    lv_anim_set_repeat_count(&a, 4);
    lv_anim_set_completed_cb(&a, shake_done);
    lv_anim_start(&a);
    if (fails >= 3) {
        fails = 0;
        lock_until_us = esp_timer_get_time() + 15 * 1000000LL;
        if (!lock_timer) lock_timer = lv_timer_create(lock_tick, 250, NULL);
        lock_tick(NULL);
    } else {
        lv_label_set_text(msg, "Невірний PIN");
    }
}

static void cancel_cb(lv_event_t *e)
{
    (void)e;
    N.modal = MODAL_NONE;
    ui_apply_layers();
}

static lv_obj_t *label(lv_obj_t *p, const lv_font_t *f, uint32_t c, int x, int y, const char *t)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
    lv_label_set_text(l, t);
    lv_obj_set_pos(l, x, y);
    return l;
}

void pin_create(lv_obj_t *p)
{
    N.l_pin = ui_layer(p, true);
    lv_obj_t *L = N.l_pin;
    label(L, &font_s40, C_TEXT, 50, 140, "Батьківський\nрежим");
    label(L, &font_r18, C_MUTED, 50, 250, "Введіть PIN, щоб змінити\nналаштування");

    dots_box = lv_obj_create(L);
    lv_obj_remove_style_all(dots_box);
    lv_obj_set_pos(dots_box, 50, 322);
    lv_obj_set_size(dots_box, 4 * 27 + 3 * 22 + 20, 30);
    for (int i = 0; i < 4; i++) {
        dots[i] = lv_obj_create(dots_box);
        lv_obj_remove_style_all(dots[i]);
        lv_obj_set_size(dots[i], 27, 27);
        lv_obj_set_pos(dots[i], i * 49, 0);
        lv_obj_set_style_radius(dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dots[i], 3, 0);
        lv_obj_set_style_bg_color(dots[i], lv_color_hex(C_TEXT), 0);
    }
    msg = label(L, &font_r18, C_AMBER, 50, 368, "");

    lv_obj_t *cancel = ui_button(L, &font_r22, "Скасувати", cancel_cb, NULL);
    lv_obj_set_size(cancel, 190, 64);
    lv_obj_set_pos(cancel, 50, 420);

    static const char K[12] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', 0, '0', '<'};
    const int x0 = 1024 - 50 - (3 * 135 + 2 * 15), y0 = (600 - (4 * 110 + 3 * 15)) / 2;
    for (int i = 0; i < 12; i++) {
        if (!K[i]) continue;
        char t[2] = {K[i], 0};
        keys[i] = ui_button(L, &font_s40, K[i] == '<' ? LV_SYMBOL_BACKSPACE : t, key_cb, (void *)(intptr_t)K[i]);
        lv_obj_set_size(keys[i], 135, 110);
        lv_obj_set_pos(keys[i], x0 + (i % 3) * 150, y0 + (i / 3) * 125);
    }
}

void pin_open(void)
{
    entry[0] = 0;
    draw_dots();
    if (esp_timer_get_time() >= lock_until_us) lv_label_set_text(msg, "");
    N.modal = MODAL_PIN;
    ui_apply_layers();
}
