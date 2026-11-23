/* Display unit UI root: layers, navigation, gestures, frame loop and demo scenes. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#include "esp_timer.h"
#include "ui.h"
#include "ui_priv.h"

ui_nav_t N;

static lv_obj_t *clock_lbl, *hold_bar;
static lv_timer_t *hold_timer;
static int64_t last_us;
static int clock_min_shown = -1;
static bool demo_level_done;

/* no RTC battery on the board: start the clock at build time and count uptime */
static int boot_minutes(void)
{
    int h = 0, m = 0;
    sscanf(__TIME__, "%d:%d", &h, &m);
    return h * 60 + m;
}

static void show(lv_obj_t *o, bool on)
{
    if (on == lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) lv_obj_set_flag(o, LV_OBJ_FLAG_HIDDEN, !on);
}

void ui_apply_layers(void)
{
    const bool main = N.screen == SCR_MAIN;
    show(N.l_splash, N.screen == SCR_SPLASH);
    show(N.l_charge, main && S.charging);
    const bool on_drive = main && !S.charging && N.page == PAGE_DRIVE;
    for (int t = 0; t < THEME_COUNT; t++) show(N.l_theme[t], on_drive && S.theme == t);
    show(N.l_chrome, on_drive);
    show(N.l_car, main && !S.charging && N.page == PAGE_CAR);
    show(N.l_pin, N.modal == MODAL_PIN);
    show(N.l_settings, N.modal == MODAL_SETTINGS);
    show(N.l_themes, N.modal == MODAL_THEMES);
    show(N.l_demo, N.modal == MODAL_DEMO);
    show(N.l_alert, main && N.alert != ALERT_NONE && N.modal == MODAL_NONE);
    if (!main || N.modal != MODAL_NONE || N.alert != ALERT_NONE) show(N.l_level, false);

    show(N.hold, main && N.modal == MODAL_NONE && N.alert == ALERT_NONE);
    show(clock_lbl, main);
    if (N.dev_btn) show(N.dev_btn, N.modal != MODAL_DEMO);
}

void ui_go_main(void)
{
    N.screen = SCR_MAIN;
    ui_apply_layers();
}

void ui_restart_splash(void)
{
    N.screen = SCR_SPLASH;
    N.modal = MODAL_NONE;
    N.alert = ALERT_NONE;
    ui_apply_layers();
    splash_play();
}

void ui_raise(alert_t a)
{
    N.alert = a;
    if (a == ALERT_LOWBAT) {
        if (S.battery > 9) S.battery = 9;
        S.low_shown = true;
    } else if (a == ALERT_OVERHEAT) {
        if (S.temp_r < 72) S.temp_r = 72;
        S.heat_shown = true;
        S.stop = true;
    } else if (a == ALERT_CAN) {
        S.can_lost = true;
    }
    alert_show(a);
    ui_apply_layers();
}

void ui_set_level(int level)
{
    S.level = level;
    if (N.screen == SCR_MAIN && N.modal == MODAL_NONE && N.alert == ALERT_NONE) level_show(level);
}

void ui_set_theme(int t, bool save)
{
    t = ((t % THEME_COUNT) + THEME_COUNT) % THEME_COUNT;
    if (t == S.theme) return;
    S.theme = t;
    /* fill the new theme while it is still hidden (the Analog face is baked on its first use) */
    if (N.screen == SCR_MAIN) drive_update();
    ui_apply_layers();
    if (save) state_save_settings();
    char b[48];
    snprintf(b, sizeof b, "Тема: %s", THEME_NAMES[t]);
    toast(b);
}

void ui_set_brightness(int pct)
{
    /* the 5B backlight is on/off only, so dimming is a black veil on the top layer */
    const lv_opa_t opa = (lv_opa_t)((100 - pct) * 255 / 100);
    lv_obj_set_style_bg_opa(N.dim, opa, 0);
    show(N.dim, opa > 0);
}

/* ---------------- scenes (same list as the web prototype) ---------------- */
const char *const UI_SCENES[] = {
    "Заставка", "Їзда", "На ліміті", "Задній хід", "Пульт батьків", "Зміна рівня",
    "Мало заряду", "Перегрів", "Немає CAN", "Екран авто", "Зарядка", "Фари", "Автодемо",
    "Поворот ліворуч", "Поворот праворуч", "Аварійка", "Помилка (check)", "Усі значки", "Мороз −7°", "Без датчика надворі",
};
const int UI_SCENE_COUNT = sizeof UI_SCENES / sizeof UI_SCENES[0];

static void base(void)
{
    N.modal = MODAL_NONE;
    N.alert = ALERT_NONE;
    N.page = PAGE_DRIVE;
    S.can_lost = S.charging = S.remote = S.demo = S.stop = S.fault = false;
    S.turn = TURN_OFF;
    S.gear = GEAR_D;
    S.temp_l = 38;
    S.temp_r = 41;
    alert_show(ALERT_NONE);
    if (N.screen != SCR_MAIN) ui_go_main();
}

void ui_scene(int i)
{
    switch (i) {
    case 0: ui_restart_splash(); return;
    case 1: base(); S.pedal = 4; S.level = 2; S.battery = 78; break;
    case 2: base(); S.pedal = 9; S.level = 1; S.battery = 64; break;
    case 3: base(); S.gear = GEAR_R; S.pedal = 3; break;
    case 4: base(); S.remote = true; S.pedal = 3; break;
    case 5: base(); S.pedal = 5; ui_apply_layers(); ui_set_level(S.level % 3 + 1); break;
    case 6: base(); S.pedal = 2; ui_raise(ALERT_LOWBAT); break;
    case 7: base(); S.pedal = 0; ui_raise(ALERT_OVERHEAT); break;
    case 8: base(); ui_raise(ALERT_CAN); break;
    case 9: base(); N.page = PAGE_CAR; S.pedal = 4; break;
    case 10: base(); S.charging = true; S.gear = GEAR_N; S.battery = 62; break;
    case 11: { const bool l = !S.lights; base(); S.lights = l; S.pedal = 3; break; }
    case 12: base(); S.demo = true; if (S.battery < 40) S.battery = 40; break;
    case 13: base(); S.turn = TURN_LEFT; S.pedal = 3; break;
    case 14: base(); S.turn = TURN_RIGHT; S.pedal = 3; break;
    case 15: base(); S.turn = TURN_HAZARD; S.pedal = 0; S.gear = GEAR_N; break;
    case 16: base(); S.fault = true; S.pedal = 3; break;
    case 17:   /* every telltale, both arrows and the sleepy z at once: to judge placement and spacing */
        base(); S.fault = S.remote = S.lights = true; S.temp_l = S.temp_r = 60; S.turn = TURN_HAZARD;
        S.battery = 8; S.low_shown = true; S.pedal = 3; break;
    case 18: base(); S.outdoor = -7; S.outdoor_ok = true; S.pedal = 3; break;
    case 19: base(); S.outdoor_ok = !S.outdoor_ok; S.pedal = 3; break;
    }
    ui_apply_layers();
}

/* ---------------- input ---------------- */
static void gesture_cb(lv_event_t *e)
{
    (void)e;
    if (N.screen != SCR_MAIN || S.charging || N.modal != MODAL_NONE || N.alert != ALERT_NONE) return;
    const lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_active());
    if (d == LV_DIR_LEFT) N.page = PAGE_CAR;
    else if (d == LV_DIR_RIGHT) N.page = PAGE_DRIVE;
    else if ((d == LV_DIR_TOP || d == LV_DIR_BOTTOM) && N.page == PAGE_DRIVE) {
        ui_set_theme(S.theme + (d == LV_DIR_TOP ? 1 : -1), true);   /* swipe up = next theme, down = previous */
        return;
    } else return;
    ui_apply_layers();
}

static void bar_w_cb(void *o, int32_t v) { lv_obj_set_width(o, v); }

static void hold_fire(lv_timer_t *t)
{
    (void)t;
    hold_timer = NULL;
    lv_anim_delete(hold_bar, NULL);
    lv_obj_set_width(hold_bar, 0);
    /* parent mode parks the car: settings only change with the car stopped in N */
    S.demo = false;
    S.pedal = 0;
    S.gear = GEAR_N;
    pin_open();
}

static void hold_cb(lv_event_t *e)
{
    const lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_PRESSED) {
        if (hold_timer) lv_timer_delete(hold_timer);
        hold_timer = lv_timer_create(hold_fire, 1500, NULL);
        lv_timer_set_repeat_count(hold_timer, 1);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, hold_bar);
        lv_anim_set_exec_cb(&a, bar_w_cb);
        lv_anim_set_values(&a, 0, PS(70));
        lv_anim_set_duration(&a, 1500);
        lv_anim_start(&a);
    } else if (c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST) {
        if (hold_timer) {
            lv_timer_delete(hold_timer);
            hold_timer = NULL;
        }
        lv_anim_delete(hold_bar, NULL);
        lv_obj_set_width(hold_bar, 0);
    }
}

static void dev_btn_cb(lv_event_t *e)
{
    (void)e;
    if (N.screen != SCR_MAIN) {   /* tapped during the splash: skip it */
        splash_stop();
        ui_go_main();
    }
    demo_open();
}

/* ---------------- frame loop ---------------- */
static void frame_cb(lv_timer_t *t)
{
    (void)t;
    const int64_t now = esp_timer_get_time();
    const float dt = fminf(0.1f, (now - last_us) / 1e6f);
    last_us = now;
    const bool main = N.screen == SCR_MAIN;
    const bool running = main && N.modal == MODAL_NONE && N.alert == ALERT_NONE;

    if (S.demo && running) {
        const int sec = (int)S.demo_t;
        if (sec % 37 == 20 && !demo_level_done) {
            demo_level_done = true;
            ui_set_level(S.level % 3 + 1);
        }
        if (sec % 37 != 20) demo_level_done = false;
    }

    const alert_t a = state_tick(dt, main);
    if (a != ALERT_NONE && N.alert == ALERT_NONE && N.modal == MODAL_NONE) ui_raise(a);

    /* while the level card is up the page under it is not updated: gauge changes are invisible there,
     * and the dirty areas would still be walked and drawn up to the card */
    const bool card = !lv_obj_has_flag(N.l_level, LV_OBJ_FLAG_HIDDEN);
#if CONFIG_DASH_DEMO_THEME_CYCLE
    if (S.demo && running) ui_set_theme((int)(S.demo_t / 10) % THEME_COUNT, false);   /* profiling aid: a new theme every 10 s */
#endif
    if (main) {
        if (S.charging) charge_update();
        else if (N.page == PAGE_DRIVE) {
            if (!card) drive_update();
            else chrome_update();
        } else car_update();
    }
    if (main && lv_obj_has_flag(N.l_charge, LV_OBJ_FLAG_HIDDEN) == S.charging) ui_apply_layers();

    const int mins = (boot_minutes() + (int)(now / 60000000)) % (24 * 60);
    if (mins != clock_min_shown) {
        clock_min_shown = mins;
        char b[8];
        snprintf(b, sizeof b, "%02d:%02d", mins / 60, mins % 60);
        lv_label_set_text(clock_lbl, b);
    }
    settings_tick();
}

void ui_init(lv_display_t *disp)
{
    (void)disp;
    state_init();
    memset(&N, 0, sizeof N);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, gesture_cb, LV_EVENT_GESTURE, NULL);

    /* z-order: pages, then status chrome, then overlays/modals */
    N.l_splash = ui_layer(scr, false);
    splash_create(N.l_splash);
    for (int t = 0; t < THEME_COUNT; t++) N.l_theme[t] = ui_layer(scr, false);
    cluster_create(N.l_theme[THEME_CLUSTER]);
    sport_create(N.l_theme[THEME_SPORT]);
    analog_create(N.l_theme[THEME_ANALOG]);
    kids_create(N.l_theme[THEME_KIDS]);
    auto_create(N.l_theme[THEME_AUTO]);
    N.l_chrome = ui_layer(scr, false);
    chrome_create(N.l_chrome);
    N.l_car = ui_layer(scr, false);
    car_create(N.l_car);
    N.l_charge = ui_layer(scr, false);
    charge_create(N.l_charge);

    clock_lbl = ui_text(scr, &font_s40, C_TEXT, ALIGN_L, 24, 46, "00:00");

    N.hold = lv_obj_create(scr);
    lv_obj_remove_style_all(N.hold);
    lv_obj_set_size(N.hold, PS(150), PS(72));
    lv_obj_add_flag(N.hold, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(N.hold, hold_cb, LV_EVENT_ALL, NULL);
    hold_bar = ui_box(N.hold, 24, 72 - 17, 0, 3, 2, C_RED);
    lv_obj_set_width(hold_bar, 0);

    overlays_create(scr);
    pin_create(scr);
    settings_create(scr);
    themes_create(scr);
    demo_create(scr);

    N.dim = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(N.dim);
    lv_obj_set_size(N.dim, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(N.dim, lv_color_black(), 0);
    lv_obj_remove_flag(N.dim, LV_OBJ_FLAG_CLICKABLE);
    ui_set_brightness(S.brightness);

#if CONFIG_DASH_DEV_DEMO_BUTTON
    /* development shortcut: scene picker without the PIN, above everything (even the dimmer) */
    N.dev_btn = ui_button(lv_layer_top(), &font_r16, "ДЕМО", dev_btn_cb, NULL);
    lv_obj_set_size(N.dev_btn, 92, 44);
    lv_obj_set_pos(N.dev_btn, 12, 600 - 12 - 44);
    lv_obj_set_style_bg_opa(N.dev_btn, LV_OPA_80, 0);
    lv_obj_set_style_border_color(N.dev_btn, lv_color_hex(C_LINE2), 0);
    lv_obj_set_style_text_color(N.dev_btn, lv_color_hex(C_MUTED), 0);
    lv_obj_set_ext_click_area(N.dev_btn, 8);
#endif

    last_us = esp_timer_get_time();
    lv_timer_create(frame_cb, 33, NULL);
    ui_restart_splash();
}
