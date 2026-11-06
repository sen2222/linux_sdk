#include <stdint.h>
#include "lvgl/lvgl.h"
#include "app_config.h"
#include "refresh_demo.h"
#include "view_class.h"

struct _refresh_demo
{
    lv_obj_t *root;
    lv_obj_t *fps_label;
    lv_obj_t *touch_label;
    lv_obj_t *blocks[3];
    lv_display_t *display;
    lv_timer_t *timer;
    uint32_t started;
    uint32_t sampled;
    unsigned frames;
    uint8_t closing;
};

/* 只统计一帧最后一次 flush 返回的次数，不把 16ms 动画定时器次数当成实际 FPS。 */
static void app_refresh_demo_frame_cb(lv_event_t *event)
{
    struct _refresh_demo *demo = lv_event_get_user_data(event);
    if (lv_display_flush_is_last(demo->display))
        demo->frames++;
}

static void app_refresh_demo_delete_cb(lv_event_t *event)
{
    struct _refresh_demo *demo = lv_event_get_user_data(event);
    if (demo->timer != NULL)
        lv_timer_delete(demo->timer);
    lv_display_remove_event_cb_with_user_data(demo->display, app_refresh_demo_frame_cb, demo);
    lv_free(demo);
}

static void app_refresh_demo_exit_cb(lv_event_t *event)
{
    struct _refresh_demo *demo = lv_event_get_user_data(event);
    if (demo->closing)
        return;
    demo->closing = 1;
    /* 按钮在 root 内部，延后删除整个 demo，避免打断当前按钮事件分发。 */
    lv_obj_delete_async(demo->root);
}

static void app_refresh_demo_touch_cb(lv_event_t *event)
{
    struct _refresh_demo *demo = lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *indev;
    lv_point_t point;
    int pressed;
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING &&
        code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST)
        return;
    indev = lv_event_get_indev(event);
    if (indev == NULL || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER)
        return;
    lv_indev_get_point(indev, &point);
    pressed = code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING;
    lv_label_set_text_fmt(demo->touch_label, "TOUCH: %s   X: %d   Y: %d",
                          pressed ? "DOWN" : "UP", (int)point.x, (int)point.y);
}

static void app_refresh_demo_timer_cb(lv_timer_t *timer)
{
    struct _refresh_demo *demo = lv_timer_get_user_data(timer);
    uint32_t elapsed = lv_tick_elaps(demo->started);
    uint32_t sample_ms = lv_tick_elaps(demo->sampled);
    int32_t travel = lv_obj_get_width(demo->root) - 48 - 112;
    unsigned i;
    if (travel < 0)
        travel = 0;
    for (i = 0; i < 3; i++)
    {
        uint32_t period = 3200 + i * 600;
        uint32_t phase = (elapsed + i * 600) % period;
        uint32_t distance = phase < period / 2 ? phase * 2 : (period - phase) * 2;
        int32_t x = 24 + (uint64_t)distance * travel / period;
        lv_obj_set_x(demo->blocks[i], x);
    }
    if (sample_ms >= 1000)
    {
        unsigned fps_tenths = (uint64_t)demo->frames * 10000 / sample_ms;
        lv_label_set_text_fmt(demo->fps_label, "FPS: %u.%u", fps_tenths / 10, fps_tenths % 10);
        demo->frames = 0;
        demo->sampled = lv_tick_get();
    }
    /* 强制整屏重绘，用于观察完整绘制和VSYNC呈现，不只移动几块脏区域。 */
    lv_obj_invalidate(demo->root);
}

lv_obj_t *app_refresh_demo_create(lv_obj_t *parent)
{
    static const uint32_t colors[3] = {0x27d9ff, 0xff936a, 0xa58bff};
    struct _refresh_demo *demo;
    lv_obj_t *label;
    unsigned i;
    if (parent == NULL)
        return NULL;
    demo = lv_malloc_zeroed(sizeof(*demo));
    if (demo == NULL)
        return NULL;
    demo->display = lv_obj_get_display(parent);
    demo->root = lv_obj_create(parent);
    if (demo->root == NULL)
    {
        lv_free(demo);
        return NULL;
    }
    lv_obj_add_event_cb(demo->root, app_refresh_demo_delete_cb, LV_EVENT_DELETE, demo);
    lv_obj_remove_style_all(demo->root);
    lv_obj_set_size(demo->root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(demo->root, 0, 0);
    lv_obj_set_scrollable(demo->root, false);
    lv_obj_set_clickable(demo->root, true);
    lv_obj_set_press_lock(demo->root, true);
    lv_obj_add_event_cb(demo->root, app_refresh_demo_touch_cb, LV_EVENT_ALL, demo);
    lv_obj_set_style_bg_color(demo->root, lv_color_hex(0x0b1020), LV_PART_MAIN);
    /* 覆盖主界面的 JPG 壁纸，让本 demo 的结果不受重复图片解码影响；退出后壁纸恢复。 */
    lv_obj_set_style_bg_opa(demo->root, LV_OPA_COVER, LV_PART_MAIN);

    label = lv_label_create(demo->root);
    if (label == NULL)
        goto failed;
    lv_label_set_text(label, "TEST / LCD + TOUCH");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_pos(label, 76, 56);

    label = lv_label_create(demo->root);
    if (label == NULL)
        goto failed;
    lv_label_set_text(label, "Drag on the screen to test touch");
    lv_obj_set_style_text_color(label, lv_color_hex(0x96a4bd), LV_PART_MAIN);
    lv_obj_set_pos(label, 24, 88);

    demo->fps_label = lv_label_create(demo->root);
    if (demo->fps_label == NULL)
        goto failed;
    lv_label_set_text(demo->fps_label, "FPS: --");
    lv_obj_set_style_text_font(demo->fps_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(demo->fps_label, lv_color_hex(0x27d9ff), LV_PART_MAIN);
    lv_obj_align(demo->fps_label, LV_ALIGN_TOP_RIGHT, -24, 56);

    demo->touch_label = lv_label_create(demo->root);
    if (demo->touch_label == NULL)
        goto failed;
    lv_label_set_text(demo->touch_label, "TOUCH: --   X: --   Y: --");
    lv_obj_set_style_text_font(demo->touch_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(demo->touch_label, lv_color_hex(0x55ff99), LV_PART_MAIN);
    lv_obj_align(demo->touch_label, LV_ALIGN_BOTTOM_LEFT, 24, -30);

    for (i = 0; i < 3; i++)
    {
        lv_obj_t *track = lv_obj_create(demo->root);
        if (track == NULL)
            goto failed;
        lv_obj_remove_style_all(track);
        lv_obj_set_size(track, LV_PCT(100), 2);
        lv_obj_set_pos(track, 0, 178 + i * 84);
        lv_obj_set_style_bg_color(track, lv_color_hex(0x26324b), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(track, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_clickable(track, false);
        demo->blocks[i] = lv_obj_create(demo->root);
        if (demo->blocks[i] == NULL)
            goto failed;
        lv_obj_remove_style_all(demo->blocks[i]);
        lv_obj_set_size(demo->blocks[i], 112, 52);
        lv_obj_set_pos(demo->blocks[i], 24, 152 + i * 84);
        lv_obj_set_style_radius(demo->blocks[i], 10, LV_PART_MAIN);
        lv_obj_set_style_bg_color(demo->blocks[i], lv_color_hex(colors[i]), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(demo->blocks[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_clickable(demo->blocks[i], false);
        lv_obj_set_scrollable(demo->blocks[i], false);
    }
    if (app_view_back_button_create(demo->root, app_refresh_demo_exit_cb, demo) == NULL)
        goto failed;

    if (lv_display_add_event_cb(demo->display, app_refresh_demo_frame_cb, LV_EVENT_FLUSH_FINISH, demo) == NULL)
        goto failed;
    demo->started = demo->sampled = lv_tick_get();
    demo->timer = lv_timer_create(app_refresh_demo_timer_cb, 16, demo);
    if (demo->timer == NULL)
        goto failed;
    APP_LOG(LOG_INFO, "TEST started: LCD full-frame refresh + touch coordinates, tap < to exit\n");
    return demo->root;
failed:
    lv_obj_delete(demo->root);
    return NULL;
}
