/* What every gauge theme shares: the per-frame context, the status chrome (lights lamp, link warning,
 * parent-remote banner), the R/N/D gear labels and the dispatcher that redraws only the active theme. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui_priv.h"

const char *const THEME_NAMES[THEME_COUNT] = {"Кластер", "Спорт", "Аналог", "Пригода", "Авто"};

/* ---------------- status chrome ----------------
 * Top bar shared by all themes, in reading order: today's trip next to the clock (the clock itself is in
 * ui.c), the outdoor temperature, the speed-level character between the turn-signal arrows, and real-car telltales on the right that are
 * visible only while lit, left to right: overheat (amber), check (amber: a fault or no CAN link),
 * parents driving (blue), headlights (green). The arrows are the only thing that blinks on the drive screens. */
static lv_obj_t *out_cap, *out_val, *trip_val, *turn_l, *turn_r, *tl_lights, *tl_temp, *tl_check, *tl_remote;
static char_ctl_t *level_char;
static char trip_txt[32], out_txt[16];

static lv_obj_t *icon(lv_obj_t *p, const lv_image_dsc_t *src, int x, int y, uint32_t color)
{
    lv_obj_t *o = lv_image_create(p);
    lv_image_set_src(o, src);
    lv_obj_set_pos(o, PX(x), PY(y));
    lv_obj_set_style_image_recolor(o, lv_color_hex(color), 0);   /* A8: drawn in the recolour colour */
    lv_obj_set_style_image_recolor_opa(o, LV_OPA_COVER, 0);
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    return o;
}

void chrome_create(lv_obj_t *p)
{
    ui_box(p, 124, 16, 2, 32, 0, C_LINE);
    ui_text(p, &font_r18, C_MUTED, ALIGN_L, 140, 24, "пробіг сьогодні");
    trip_val = ui_text(p, &font_s32, C_TEXT, ALIGN_L, 140, 48, "1,4 км");

    /* outdoor temperature in the right corner (permanent); the telltales sit to its left; hidden while no sensor is connected */
    out_cap = ui_text(p, &font_r18, C_MUTED, ALIGN_R, 776, 24, "надворі");
    out_val = ui_text(p, &font_s32, C_TEXT, ALIGN_R, 776, 48, "+12°");

    level_char = char_create(p, 374, 4, false);   /* the level: turtle, hare or cheetah */
    turn_l = icon(p, &img_turn_l, 312, 17, C_GREEN);
    turn_r = icon(p, &img_turn_r, 448, 17, C_GREEN);
    tl_lights = icon(p, &img_tl_lights, 676, 14, C_GREEN);
    tl_temp = icon(p, &img_tl_temp, 532, 14, C_AMBER);
    tl_check = icon(p, &img_tl_check, 580, 14, C_AMBER);
    tl_remote = icon(p, &img_tl_remote, 628, 14, C_BLUE);
}

static void show_if(lv_obj_t *o, bool on)
{
    if (on == lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) lv_obj_set_flag(o, LV_OBJ_FLAG_HIDDEN, !on);
}

void chrome_update(void)
{
    show_if(tl_lights, S.lights);
    show_if(tl_temp, S.temp_l >= 55 || S.temp_r >= 55);
    show_if(tl_check, S.can_lost || S.fault);
    show_if(tl_remote, S.remote);

    const bool on = (lv_tick_get() / 350) % 2 == 0;   /* 350 ms on, 350 ms off, both arrows in step */
    show_if(turn_l, on && (S.turn == TURN_LEFT || S.turn == TURN_HAZARD));
    show_if(turn_r, on && (S.turn == TURN_RIGHT || S.turn == TURN_HAZARD));

    const drive_ctx_t lc = {.sleepy = S.battery < 10};
    char_update(level_char, &lc);

    show_if(out_cap, S.outdoor_ok);
    show_if(out_val, S.outdoor_ok);
    if (S.outdoor_ok) {
        const int t = (int)lroundf(S.outdoor);
        char o[16];
        snprintf(o, sizeof o, "%s%d°", t > 0 ? "+" : t < 0 ? "\xe2\x88\x92" : "", t < 0 ? -t : t);   /* U+2212 minus */
        if (strcmp(o, out_txt) != 0) {
            strcpy(out_txt, o);
            ui_text_set(out_val, o);
        }
    }

    char b[32], n[12];
    fmt_num(n, sizeof n, S.trip, 1);
    snprintf(b, sizeof b, "%s км", n);
    if (strcmp(b, trip_txt) != 0) {
        strcpy(trip_txt, b);
        ui_text_set(trip_val, b);
    }
}

/* ---------------- gear labels ---------------- */
void gear_create(lv_obj_t *lbl[3], lv_obj_t *p, int x0, int dx, int y)
{
    static const char *g[] = {"R", "N", "D"};
    for (int i = 0; i < 3; i++) lbl[i] = ui_text(p, &font_r25, C_DIM, ALIGN_C, x0 + dx * i, y, g[i]);
}

void gear_set(lv_obj_t *lbl[3], int x0, int dx, int y, const lv_font_t *on_font, const lv_font_t *off_font)
{
    for (int i = 0; i < 3; i++) {
        const bool on = (int)S.gear == i;
        const lv_font_t *f = on ? on_font : off_font;
        if (lv_obj_get_style_text_font(lbl[i], 0) != f) {
            lv_obj_set_style_text_font(lbl[i], f, 0);
            ui_text_move(lbl[i], ALIGN_C, x0 + dx * i, y);
        }
        ui_set_color_cache(lbl[i], on ? (i == GEAR_D ? C_RED : C_TEXT) : C_DIM, 0, true);
    }
}

void gear_create_v(lv_obj_t *lbl[3], lv_obj_t *p, int x, int y0, int dy)
{
    static const char *g[] = {"R", "N", "D"};
    for (int i = 0; i < 3; i++) lbl[i] = ui_text(p, &font_r25, C_DIM, ALIGN_C, x, y0 + dy * i, g[i]);
}

void gear_set_v(lv_obj_t *lbl[3], int x, int y0, int dy, const lv_font_t *on_font, const lv_font_t *off_font)
{
    for (int i = 0; i < 3; i++) {
        const bool on = (int)S.gear == i;
        const lv_font_t *f = on ? on_font : off_font;
        if (lv_obj_get_style_text_font(lbl[i], 0) != f) {
            lv_obj_set_style_text_font(lbl[i], f, 0);
            ui_text_move(lbl[i], ALIGN_C, x, y0 + dy * i);
        }
        ui_set_color_cache(lbl[i], on ? (i == GEAR_D ? C_RED : C_TEXT) : C_DIM, 0, true);
    }
}

/* ---------------- per-frame context ---------------- */
void drive_ctx_build(drive_ctx_t *c)
{
    const bool lost = S.can_lost;
    c->lost = lost;
    c->low = S.battery < 20;
    c->sleepy = S.battery < 10;
    c->speed = S.speed;
    c->eff = state_effective_limit();
    c->d_speed = S.speed;
    c->d_batt = S.battery;
    c->frac = lost || c->eff <= 0 ? 0 : fminf(1, c->d_speed / c->eff);

    fmt_num(c->range_txt, sizeof c->range_txt, S.battery * 0.15f, 1);
}

void drive_update(void)
{
    drive_ctx_t c;
    drive_ctx_build(&c);
    switch (S.theme) {
    case THEME_SPORT:  sport_update(&c);   break;
    case THEME_ANALOG: analog_update(&c);  break;
    case THEME_KIDS:   kids_update(&c);    break;
    case THEME_AUTO:   auto_update(&c);    break;
    default:           cluster_update(&c); break;
    }
    chrome_update();
}
