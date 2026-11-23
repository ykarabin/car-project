/* Boot splash (2.4 s): the crest fades in, a light sweeps across it, it fades out, then the drive screen.
 * The crest is an opaque bitmap on the flat screen colour. The sweep band is clipped to its outline by an
 * A8 mask (img_crest_mask), which is its only purpose: without it the band would smear over the black
 * background outside the shield.
 * LVGL renders every object with a bitmap mask, and every container with opacity < 100 % and children,
 * through an intermediate ARGB layer. So the fades use the image's own opacity (no layer), and the masked
 * object is only shown for the 0.8 s sweep, while the crest is fully visible. */
#include "ui_priv.h"

static lv_obj_t *crest_img, *mask, *band;
static lv_timer_t *done_timer;

static void opa_cb(void *o, int32_t v) { lv_obj_set_style_image_opa(o, (lv_opa_t)v, 0); }
static void x_cb(void *o, int32_t v) { lv_obj_set_x(o, v); }

static void run(void *var, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t delay, uint32_t dur,
                lv_anim_path_cb_t path)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, var);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_duration(&a, dur);
    lv_anim_set_path_cb(&a, path ? path : lv_anim_path_linear);
    lv_anim_set_early_apply(&a, false);
    lv_anim_start(&a);
}

void splash_create(lv_obj_t *p)
{
    crest_img = lv_image_create(p);
    lv_image_set_src(crest_img, &img_crest);
    lv_obj_center(crest_img);
    lv_obj_remove_flag(crest_img, LV_OBJ_FLAG_CLICKABLE);
    /* same box as the crest; shown only while the band sweeps */
    mask = lv_obj_create(p);
    lv_obj_remove_style_all(mask);
    lv_obj_set_size(mask, img_crest.header.w, img_crest.header.h);
    lv_obj_center(mask);
    lv_obj_set_style_bitmap_mask_src(mask, &img_crest_mask, 0);
    lv_obj_remove_flag(mask, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(mask, LV_OBJ_FLAG_HIDDEN);
    band = lv_image_create(mask);
    lv_image_set_src(band, &img_band);                              /* A8: only the alpha is stored ... */
    lv_obj_set_style_image_recolor(band, lv_color_white(), 0);      /* ... and it is drawn in white */
}

static void done_cb(lv_timer_t *t)
{
    (void)t;
    done_timer = NULL;
    ui_go_main();
}

static void sweep_start_cb(lv_anim_t *a)
{
    (void)a;
    lv_obj_remove_flag(mask, LV_OBJ_FLAG_HIDDEN);
}

static void sweep_done_cb(lv_anim_t *a)
{
    (void)a;
    lv_obj_add_flag(mask, LV_OBJ_FLAG_HIDDEN);
}

void splash_stop(void)
{
    lv_anim_delete(crest_img, NULL);
    lv_anim_delete(band, NULL);
    lv_obj_add_flag(mask, LV_OBJ_FLAG_HIDDEN);
    if (done_timer) lv_timer_delete(done_timer);
    done_timer = NULL;
}

void splash_play(void)
{
    splash_stop();

    lv_obj_set_style_image_opa(crest_img, LV_OPA_TRANSP, 0);
    lv_obj_set_x(band, -90);

    /* crest: in 0-690 ms, hold, out 1790-2300 ms; sweep 800-1600 ms (the crest is fully visible then) */
    run(crest_img, opa_cb, 0, 255, 0, 690, lv_anim_path_ease_out);
    run(crest_img, opa_cb, 255, 0, 1790, 510, lv_anim_path_ease_in);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, band);
    lv_anim_set_exec_cb(&a, x_cb);
    lv_anim_set_values(&a, -90, img_crest.header.w);
    lv_anim_set_delay(&a, 800);
    lv_anim_set_duration(&a, 800);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_early_apply(&a, false);
    lv_anim_set_start_cb(&a, sweep_start_cb);
    lv_anim_set_completed_cb(&a, sweep_done_cb);
    lv_anim_start(&a);

    done_timer = lv_timer_create(done_cb, 2400, NULL);
    lv_timer_set_repeat_count(done_timer, 1);
}
