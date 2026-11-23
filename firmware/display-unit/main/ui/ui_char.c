/* The character: a still sprite of the current level (turtle, hare, cheetah), mirrored in reverse, with "z"
 * beside the head on a very low battery. No animation at all: the pedal is a plain button, so the car is almost
 * always at its limit and any motion effect would just run forever. Two flavours, both opaque RGB565 on the
 * flat screen colour: the tiny sprite (65x55) that is the level indicator in the top bar, and the big one
 * (276x234) of the Kids theme. */
#include <stdlib.h>

#include "ui_priv.h"

struct char_ctl {
    lv_obj_t *box, *img, *zz[2];
    bool big;
    int pad_x, pad_y, w, h;
    float k;                 /* sprite width / 117, the size the z positions are written in */
    int key;                 /* level * 2 + mirrored: what the image currently shows */
};

char_ctl_t *char_create(lv_obj_t *p, int x, int y, bool big)
{
    char_ctl_t *c = calloc(1, sizeof *c);
    c->big = big;
    c->w = big ? 276 : 65;
    c->h = big ? 234 : 55;
    c->k = c->w / 117.0f;
    c->pad_x = big ? 95 : 30;      /* room for the z's beside the head */
    c->pad_y = big ? 80 : 14;
    c->key = -1;

    c->box = lv_obj_create(p);
    lv_obj_remove_style_all(c->box);
    lv_obj_set_pos(c->box, PX(x) - c->pad_x, PY(y) - c->pad_y);
    lv_obj_set_size(c->box, c->w + 2 * c->pad_x, c->h + c->pad_y);
    lv_obj_remove_flag(c->box, LV_OBJ_FLAG_CLICKABLE);
    c->img = lv_image_create(c->box);
    lv_obj_set_pos(c->img, c->pad_x, c->pad_y);
    for (int i = 0; i < 2; i++) {
        c->zz[i] = lv_label_create(c->box);
        lv_label_set_text(c->zz[i], "z");
        lv_obj_set_style_text_color(c->zz[i], lv_color_hex(C_MUTED), 0);
        lv_obj_add_flag(c->zz[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_text_font(c->zz[0], big ? &font_s40 : &font_s28, 0);   /* the tiny one has a single z */
    lv_obj_set_style_text_font(c->zz[1], big ? &font_s50 : &font_s28, 0);
    return c;
}

void char_set_pos(char_ctl_t *c, int x, int y)
{
    lv_obj_set_pos(c->box, PX(x) - c->pad_x, PY(y) - c->pad_y);
}

static void place_extras(char_ctl_t *c, bool flipped)
{
    /* the z's sit next to the head (small-sprite coordinates, scaled by k) */
    const int zx[2] = {104, 118}, zy[2] = {4, -16};
    const int zw = c->big ? (int)(16 * c->k) : 14;
    for (int i = 0; i < 2; i++) {
        const int zxp = c->big ? (int)(zx[i] * c->k) : 64;
        const int zyp = c->big ? (int)(zy[i] * c->k) : 0;
        lv_obj_set_pos(c->zz[i], c->pad_x + (flipped ? c->w - zxp - zw : zxp), c->pad_y + zyp);
    }
}

void char_update(char_ctl_t *c, const drive_ctx_t *ctx)
{
    const bool flipped = S.gear == GEAR_R;
    const int key = S.level * 2 + flipped;
    if (key != c->key) {
        c->key = key;
        const lv_image_dsc_t *src = flipped ? ui_char_flipped(S.level, c->big) : ui_char_img(S.level, c->big ? 'k' : 't');
        lv_image_set_src(c->img, src);
        place_extras(c, flipped);
        if (!flipped) ui_char_flip_release(c->big);
    }
    for (int i = 0; i < 2; i++) {
        const bool show = ctx->sleepy && (c->big || i == 0);
        if (show == lv_obj_has_flag(c->zz[i], LV_OBJ_FLAG_HIDDEN)) lv_obj_set_flag(c->zz[i], LV_OBJ_FLAG_HIDDEN, !show);
    }
}
