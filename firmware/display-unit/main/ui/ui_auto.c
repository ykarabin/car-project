/* Theme "Авто": the car photo on the left (the same opaque bitmap the "Авто" page uses, so no extra image
 * memory), the speed as a huge digit between the car and a gear lever down the right edge, and a strip along the
 * bottom with charge (a 10-segment bar) on the left and range on the right margin. All live parts are flat
 * rectangles and text; the photo never changes. */
#include <math.h>
#include <stdio.h>

#include "ui_priv.h"

#define BSEGS 10
#define LEVER_X 732             /* centre of the gear lever's slot, design coordinates */
#define MARGIN_R 776

static lv_obj_t *bseg[BSEGS], *speed_txt, *batt_txt, *range_txt, *gear_lbl[3];

void auto_create(lv_obj_t *p)
{
    lv_obj_t *car = lv_image_create(p);
    lv_image_set_src(car, &img_car);
    lv_obj_set_pos(car, PX(24), PY(135));

    /* gear lever: a slot with R, N, D one under another */
    lv_obj_t *slot = ui_box(p, 704, 130, 56, 196, 28, C_DIAL);
    lv_obj_set_style_border_color(slot, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_border_width(slot, 2, 0);
    gear_create_v(gear_lbl, p, LEVER_X, 176, 66);

    speed_txt = ui_text(p, &font_s238, C_TEXT, ALIGN_C, 524, 282, "0");
    ui_text(p, &font_r25, C_MUTED, ALIGN_C, 524, 314, "км/год");

    /* bottom strip: charge on the left, range on the right margin */
    ui_box(p, 24, 362, 752, 2, 0, C_LINE);
    ui_text(p, &font_r20, C_MUTED, ALIGN_L, 24, 388, "заряд");
    batt_txt = ui_text(p, &font_s50, C_TEXT, ALIGN_L, 24, 436, "78%");
    for (int i = 0; i < BSEGS; i++) bseg[i] = ui_box(p, 130 + 36 * i, 410, 32, 26, 4, C_TRACK);

    ui_text(p, &font_r20, C_MUTED, ALIGN_R, MARGIN_R, 388, "запас ходу");
    lv_point_t sz;   /* the unit hugs the margin, the value ends just before it */
    lv_text_get_size(&sz, "км", &font_r25, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    ui_text(p, &font_r25, C_MUTED, ALIGN_R, MARGIN_R, 440, "км");
    range_txt = ui_text(p, &font_s70, C_TEXT, ALIGN_R, MARGIN_R - sz.x * 4 / 5 - 8, 440, "11,7");
}

void auto_update(const drive_ctx_t *c)
{
    char b[24];
    const bool lost = c->lost;

    gear_set_v(gear_lbl, LEVER_X, 176, 66, &font_s50, &font_s40);
    snprintf(b, sizeof b, "%d", (int)lroundf(c->d_speed));
    ui_text_set(speed_txt, lost ? "—" : b);

    const uint32_t bc = c->low ? C_AMBER : C_GREEN;
    for (int i = 0; i < BSEGS; i++) ui_set_bg(bseg[i], !lost && c->d_batt >= i * 10 + 5 ? bc : C_TRACK);
    snprintf(b, sizeof b, "%d%%", (int)lroundf(c->d_batt));
    ui_text_set(batt_txt, lost ? "—" : b);
    ui_text_set(range_txt, lost ? "—" : c->range_txt);
}
