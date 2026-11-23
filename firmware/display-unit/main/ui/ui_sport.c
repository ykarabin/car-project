/* Theme "Спорт": a 20-segment LED bar across the top (full bar = the effective limit), the speed as a huge digit
 * on the left, and two cards on the right whose edges line up with the bar: charge (a 10-segment bar) and range.
 * Everything is flat opaque rectangles: a frame changes a few segments (~2-3 kpx each) and, now and then, the
 * digit. No anti-aliased arcs, no baked bitmap needed. */
#include <math.h>
#include <stdio.h>

#include "ui_priv.h"

#define SEGS 20
#define BSEGS 10
#define HERO_X 221              /* centre of the speed block, design coordinates */
#define CARD_X 440
#define CARD_W 318

static lv_obj_t *seg[SEGS], *bseg[BSEGS];
static lv_obj_t *speed_txt, *batt_txt, *range_txt, *gear_lbl[3];

static lv_obj_t *card(lv_obj_t *p, int y, int h)
{
    lv_obj_t *c = ui_box(p, CARD_X, y, CARD_W, h, 16, C_DIAL);
    lv_obj_set_style_border_color(c, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_border_width(c, 2, 0);
    return c;
}

void sport_create(lv_obj_t *p)
{
    for (int i = 0; i < SEGS; i++) seg[i] = ui_box(p, 42 + 36 * i, 84, 32, 46, 6, C_TRACK);

    /* hero: speed (the whole composition sits 16 px below the bar so the margins above and below match) */
    gear_create(gear_lbl, p, HERO_X - 34, 34, 194);
    speed_txt = ui_text(p, &font_s238, C_TEXT, ALIGN_C, HERO_X, 362, "0");
    ui_text(p, &font_r25, C_MUTED, ALIGN_C, HERO_X, 386, "км/год");

    /* card 1: charge */
    card(p, 166, 122);
    ui_text(p, &font_r20, C_MUTED, ALIGN_L, CARD_X + 20, 200, "заряд");
    batt_txt = ui_text(p, &font_s50, C_TEXT, ALIGN_R, CARD_X + CARD_W - 20, 210, "78%");
    for (int i = 0; i < BSEGS; i++) bseg[i] = ui_box(p, CARD_X + 20 + 28 * i, 232, 22, 34, 5, C_TRACK);

    /* card 2: range */
    card(p, 304, 122);
    ui_text(p, &font_r20, C_MUTED, ALIGN_L, CARD_X + 20, 338, "запас ходу");
    range_txt = ui_text(p, &font_s70, C_TEXT, ALIGN_L, CARD_X + 20, 402, "11,7");
    ui_text(p, &font_r25, C_MUTED, ALIGN_R, CARD_X + CARD_W - 20, 402, "км");
}

void sport_update(const drive_ctx_t *c)
{
    char b[24];
    const bool lost = c->lost;
    const int n = (int)lroundf(c->frac * SEGS);   /* the full bar is the effective limit */
    const uint32_t col = S.gear == GEAR_R ? C_SILVER : C_RED;

    for (int i = 0; i < SEGS; i++) ui_set_bg(seg[i], i < n ? col : C_TRACK);

    gear_set(gear_lbl, HERO_X - 34, 34, 194, &font_s28, &font_r25);
    snprintf(b, sizeof b, "%d", (int)lroundf(c->d_speed));
    ui_text_set(speed_txt, lost ? "—" : b);

    const uint32_t bc = c->low ? C_AMBER : C_GREEN;
    for (int i = 0; i < BSEGS; i++) ui_set_bg(bseg[i], !lost && c->d_batt >= i * 10 + 5 ? bc : C_TRACK);
    snprintf(b, sizeof b, "%d%%", (int)lroundf(c->d_batt));
    ui_text_set(batt_txt, lost ? "—" : b);
    ui_text_set(range_txt, lost ? "—" : c->range_txt);
}
