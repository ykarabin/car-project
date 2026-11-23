/* Theme "Пригода", for a child who cannot read yet: no words. The level is the big character, the charge is ten
 * bubbles, the gear is a lever down the right edge. Grid: 40 px side margins and four rows: charge (y 72-112), the
 * hero zone (character, digit, gear lever; 150-394), the road (394-414) and the range number (y 452).
 * The character is an opaque bitmap on the flat screen colour. The only motion is the road: its dashes slide
 * while the car moves (faster at speed, the other way in reverse); a small clipped strip, a few kpx a frame. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui_priv.h"

#define BUBBLES 10
#define KD_L 40
#define KD_R 760
#define ROAD_W (KD_R - KD_L)
#define DASH_PITCH 60           /* design px */
#define LEVER_X (KD_R - 28)     /* centre of the gear lever's slot */

static lv_obj_t *bubble[BUBBLES], *speed_txt, *batt_txt, *range_txt, *gear_lbl[3];
static char_ctl_t *chr;
static int last_level = -1;
static lv_obj_t *road_inner;
static int road_dur = -2, road_dir = 0;

static void road_cb(void *o, int32_t v) { lv_obj_set_style_translate_x(o, v, 0); }

/* the dashes slide left (right in reverse); dur_ms = time for one dash pitch, 0 = stopped */
static void road_set(int dur_ms, int dir)
{
    if (dur_ms == road_dur && dir == road_dir) return;
    road_dur = dur_ms;
    road_dir = dir;
    lv_anim_delete(road_inner, road_cb);
    lv_obj_set_style_translate_x(road_inner, 0, 0);
    if (dur_ms <= 0) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, road_inner);
    lv_anim_set_exec_cb(&a, road_cb);
    if (dir >= 0) lv_anim_set_values(&a, 0, -PS(DASH_PITCH));
    else lv_anim_set_values(&a, -PS(DASH_PITCH), 0);
    lv_anim_set_duration(&a, dur_ms);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_start(&a);
}

void kids_create(lv_obj_t *p)
{
    /* the sprite's opaque box reaches below the feet, so it goes first: the ground line and the road are
     * drawn over its empty margin instead of being hidden by it */
    chr = char_create(p, 110, 221, true);

    /* row 1: charge, right-aligned percent on the right margin */
    for (int i = 0; i < BUBBLES; i++) bubble[i] = ui_box(p, KD_L + 60 * i, 72, 40, 40, -1, C_TRACK);
    batt_txt = ui_text(p, &font_s50, C_TEXT, ALIGN_R, KD_R, 104, "78%");

    /* gear lever: a slot with R, N, D one under another */
    lv_obj_t *slot = ui_box(p, KD_R - 56, 168, 56, 196, 28, C_DIAL);
    lv_obj_set_style_border_color(slot, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_border_width(slot, 2, 0);
    gear_create_v(gear_lbl, p, LEVER_X, 214, 66);

    /* hero zone: the big digit between the character and the lever */
    speed_txt = ui_text(p, &font_s212, C_TEXT, ALIGN_C, 520, 330, "0");
    ui_text(p, &font_r25, C_MUTED, ALIGN_C, 520, 362, "км/год");

    /* the road: a ground line and a clipped strip with a wider inner row of dashes that slides inside it */
    ui_box(p, KD_L, 394, ROAD_W, 3, 2, C_LINE);
    lv_obj_t *strip = lv_obj_create(p);
    lv_obj_remove_style_all(strip);
    lv_obj_set_pos(strip, PX(KD_L), PY(412));
    lv_obj_set_size(strip, PS(ROAD_W), PS(4));
    lv_obj_remove_flag(strip, LV_OBJ_FLAG_CLICKABLE);
    road_inner = lv_obj_create(strip);
    lv_obj_remove_style_all(road_inner);
    lv_obj_set_pos(road_inner, 0, 0);
    lv_obj_set_size(road_inner, PS(ROAD_W + DASH_PITCH), PS(4));
    lv_obj_remove_flag(road_inner, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(road_inner, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    for (int i = 0; i * DASH_PITCH < ROAD_W + DASH_PITCH; i++) {
        lv_obj_t *d = lv_obj_create(road_inner);
        lv_obj_remove_style_all(d);
        lv_obj_set_pos(d, PS(i * DASH_PITCH), 0);
        lv_obj_set_size(d, PS(26), PS(4));
        lv_obj_set_style_bg_color(d, lv_color_hex(C_TRACK), 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE);
    }

    /* range number, right-aligned: the unit hugs the margin and the value ends just before it */
    lv_point_t sz;
    lv_text_get_size(&sz, "км запасу", &font_r20, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    ui_text(p, &font_r20, C_MUTED, ALIGN_R, KD_R, 452, "км запасу");
    range_txt = ui_text(p, &font_s40, C_TEXT, ALIGN_R, KD_R - sz.x * 4 / 5 - 8, 452, "11,7");
}

void kids_update(const drive_ctx_t *c)
{
    char b[24];
    const bool lost = c->lost;

    gear_set_v(gear_lbl, LEVER_X, 214, 66, &font_s50, &font_s40);
    snprintf(b, sizeof b, "%d", (int)lroundf(c->d_speed));
    ui_text_set(speed_txt, lost ? "—" : b);

    const uint32_t bc = c->low ? C_AMBER : C_GREEN;
    for (int i = 0; i < BUBBLES; i++) ui_set_bg(bubble[i], !lost && c->d_batt >= i * 10 + 5 ? bc : C_TRACK);
    snprintf(b, sizeof b, "%d%%", (int)lroundf(c->d_batt));
    ui_text_set(batt_txt, lost ? "—" : b);
    ui_text_set(range_txt, lost ? "—" : c->range_txt);

    /* the road keeps pace with the car: a dash pitch takes 0.9 s at a crawl and 0.26 s from 13 km/h up */
    const float pitch_s = fmaxf(0.26f, 0.9f - 0.05f * c->speed);
    road_set(c->speed > 0.1f ? (int)(pitch_s * 1000) / 40 * 40 : 0, S.gear == GEAR_R ? -1 : 1);

    if (S.level != last_level) {   /* the three sprites have their feet at different heights */
        last_level = S.level;
        static const int oy[3] = {238, 221, 216};
        char_set_pos(chr, 110, oy[S.level - 1]);
    }
    char_update(chr, c);
}
