/* "Car" page (photo, charge, motors, CAN rows) and the charging screen. */
#include <math.h>
#include <stdio.h>

#include "ui_priv.h"

static lv_obj_t *batt_box, *batt_txt, *mot[2], *mot_t[2];
static lv_obj_t *row_v[6], *odo;

void car_create(lv_obj_t *p)
{
    /* the car is one opaque bitmap already flattened onto the screen colour: it is copied, not blended */
    lv_obj_t *car = lv_image_create(p);
    lv_image_set_src(car, &img_car);
    lv_obj_set_pos(car, 62, 64);

    batt_box = ui_box(p, 24, 256, 352, 66, 12, C_DIAL);
    lv_obj_set_style_border_width(batt_box, 2, 0);
    lv_obj_set_style_border_color(batt_box, lv_color_hex(C_GREEN), 0);
    ui_text(p, &font_r20, C_MUTED, ALIGN_L, 44, 296, "Заряд");
    batt_txt = ui_text(p, &font_s40, C_GREEN, ALIGN_R, 356, 302, "78%");

    static const char *side[2] = {"Л", "П"};
    for (int i = 0; i < 2; i++) {
        const int x0 = 24 + 182 * i;
        ui_box(p, x0, 334, 170, 96, 12, C_DIAL);
        mot[i] = ui_box(p, x0 + 16, 366, 32, 32, -1, C_BG);
        lv_obj_set_style_border_width(mot[i], 4, 0);
        lv_obj_set_style_border_color(mot[i], lv_color_hex(C_TEXT), 0);
        ui_text(p, &font_r16, C_MUTED, ALIGN_C, x0 + 32, 387, side[i]);
        ui_text(p, &font_r16, C_MUTED, ALIGN_L, x0 + 60, 370, "Мотор");
        mot_t[i] = ui_text(p, &font_s28, C_TEXT, ALIGN_L, x0 + 60, 400, "38°");
    }

    static const char *keys[6] = {"Акумулятор", "Мотор лівий", "Мотор правий", "Фари", "Пульт 2.4G", "CAN"};
    for (int i = 0; i < 6; i++) {
        ui_box(p, 400, 70 + 60 * i, 376, 52, 12, C_DIAL);
        ui_text(p, &font_r20, C_MUTED, ALIGN_L, 420, 102 + 60 * i, keys[i]);
        row_v[i] = ui_text(p, &font_r22, C_TEXT, ALIGN_R, 756, 103 + 60 * i, "");
    }
    odo = ui_text(p, &font_r16, C_DIM, ALIGN_L, 400, 452, "");
}

static uint32_t temp_color(float t) { return t >= 55 ? C_AMBER : C_TEXT; }

void car_update(void)
{
    char b[96], n1[12], n2[12];
    const uint32_t bc = S.battery < 20 ? C_AMBER : C_GREEN;
    ui_set_color_cache(batt_box, bc, 0, false);
    ui_set_color_cache(batt_txt, bc, 0, true);
    snprintf(b, sizeof b, "%d%%", (int)lroundf(S.battery));
    ui_text_set(batt_txt, b);

    const float t[2] = {S.temp_l, S.temp_r};
    for (int i = 0; i < 2; i++) {
        ui_set_color_cache(mot[i], temp_color(t[i]), 0, false);
        snprintf(b, sizeof b, "%d°", (int)lroundf(t[i]));
        ui_text_set(mot_t[i], b);
        ui_set_color_cache(mot_t[i], temp_color(t[i]), 0, true);
    }

    const float amps = S.speed > 0.1f ? 0.6f + S.speed * 0.55f : 0;
    fmt_num(n1, sizeof n1, 21 + S.battery * 0.045f, 1);
    snprintf(b, sizeof b, "%s В · 10 Ач", n1);
    ui_text_set(row_v[0], b);
    for (int i = 0; i < 2; i++) {
        fmt_num(n2, sizeof n2, amps * (i ? 1.05f : 1), 1);
        snprintf(b, sizeof b, "%d°C · %s А", (int)lroundf(t[i]), n2);
        ui_text_set(row_v[1 + i], b);
        ui_set_color_cache(row_v[1 + i], temp_color(t[i]), 0, true);
    }
    ui_text_set(row_v[3], S.lights ? "увімкнено" : "вимкнено");
    ui_text_set(row_v[4], S.remote ? "керують батьки" : "не активний");
    ui_set_color_cache(row_v[4], S.remote ? C_BLUE : C_TEXT, 0, true);
    ui_text_set(row_v[5], S.can_lost ? "немає відповіді" : "3 вузли в мережі");
    ui_set_color_cache(row_v[5], S.can_lost ? C_AMBER : C_GREEN, 0, true);

    fmt_num(n1, sizeof n1, S.odo, 1);
    fmt_num(n2, sizeof n2, S.trip, 2);
    snprintf(b, sizeof b, "Пробіг: %s км · поїздка: %s км", n1, n2);
    ui_text_set(odo, b);
}

/* ---------------- charging screen ---------------- */
static lv_obj_t *ch_pct, *ch_state, *ch_bar, *ch_glow, *ch_eta, *ch_va;
static lv_point_precise_t cable_pts[14];

void charge_create(lv_obj_t *p)
{
    lv_obj_t *car = lv_image_create(p);
    lv_image_set_src(car, &img_car);
    lv_obj_set_pos(car, PX(233), PY(36));

    /* cable: quadratic curve M548,160 Q640,175 660,240 then L690,240 */
    for (int i = 0; i < 13; i++) {
        const float t = i / 12.0f, u = 1 - t;
        cable_pts[i].x = PX(u * u * 548 + 2 * u * t * 640 + t * t * 660);
        cable_pts[i].y = PY(u * u * 160 + 2 * u * t * 175 + t * t * 240);
    }
    cable_pts[13].x = PX(690);
    cable_pts[13].y = PY(240);
    lv_obj_t *cable = lv_line_create(p);
    lv_line_set_points(cable, cable_pts, 14);
    lv_obj_set_style_line_width(cable, 5, 0);
    lv_obj_set_style_line_color(cable, lv_color_hex(0x5b6069), 0);
    lv_obj_set_style_line_rounded(cable, true, 0);
    ui_box(p, 690, 228, 30, 24, 5, C_LINE2);

    ch_pct = ui_text(p, &font_s90, C_TEXT, ALIGN_C, 400, 320, "62%");
    ch_state = ui_text(p, &font_r20, C_MUTED, ALIGN_C, 400, 352, "заряджається");
    ui_box(p, 220, 378, 360, 22, 11, C_TRACK);
    ch_bar = ui_box(p, 220, 378, 223, 22, 11, C_GREEN);
    ch_glow = ui_box(p, 420, 378, 23, 22, 11, 0x7ee08f);
    lv_obj_set_style_border_width(ch_glow, 0, 0);
    ch_eta = ui_text(p, &font_r22, C_TEXT, ALIGN_C, 400, 440, "");
    ch_va = ui_text(p, &font_r18, C_MUTED, ALIGN_R, 776, 40, "");
}

void charge_update(void)
{
    char b[96], n[12];
    const float bt = S.battery;
    snprintf(b, sizeof b, "%d%%", (int)lroundf(bt));
    ui_text_set(ch_pct, b);

    const int32_t bw = PS(fmaxf(22, 360 * bt / 100));
    if (lv_obj_get_width(ch_bar) != bw) {
        lv_obj_set_width(ch_bar, bw);
        lv_obj_set_x(ch_glow, PX(220) + bw - PS(23));
    }
    const bool full = bt >= 99.5f;
    ui_text_set(ch_state, full ? "заряджено" : "заряджається");
    lv_obj_set_flag(ch_glow, LV_OBJ_FLAG_HIDDEN, full);

    const int mins = (int)lroundf((100 - bt) / 100 * 12.5f * 60);
    if (full) ui_text_set(ch_eta, "можна їхати");
    else {
        snprintf(b, sizeof b, "до повного ≈ %d год %d хв", mins / 60, mins % 60);
        ui_text_set(ch_eta, b);
    }
    fmt_num(n, sizeof n, 21 + bt * 0.045f + 0.6f, 1);
    snprintf(b, sizeof b, "%s В · %s А", n, full ? "0,0" : "0,8");
    ui_text_set(ch_va, b);
}
