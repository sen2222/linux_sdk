#include <stdint.h>
#include "lvgl/lvgl.h"
#include "app_config.h"
#include "refresh_demo.h"

struct _refresh_demo
{
    lv_obj_t *root;
    lv_obj_t *fps_label;
    lv_obj_t *blocks[3];
    lv_display_t *display;
    lv_timer_t *timer;
    uint32_t started;
    uint32_t sampled;
    unsigned frames;
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
    /* 按钮在 root 内部，延后删除整个 demo，避免打断当前按钮事件分发。 */
    lv_obj_delete_async(demo->root);
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
    /* 强制整屏重绘，用于比较完整绘制、DMA/CPU 搬运和 VSYNC 呈现，不只移动几块脏区域。 */
    lv_obj_invalidate(demo->root);
}

lv_obj_t *app_refresh_demo_create(lv_obj_t *parent)
{
    static const uint32_t colors[3] = {0x27d9ff, 0xff936a, 0xa58bff};
    struct _refresh_demo *demo;
    lv_obj_t *label, *button;
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
    lv_obj_set_style_bg_color(demo->root, lv_color_hex(0x0b1020), LV_PART_MAIN);
    /* 覆盖主界面的 JPG 壁纸，让本 demo 的结果不受重复图片解码影响；退出后壁纸恢复。 */
    lv_obj_set_style_bg_opa(demo->root, LV_OPA_COVER, LV_PART_MAIN);

    label = lv_label_create(demo->root);
    if (label == NULL)
        goto failed;
    lv_label_set_text(label, "KIVA REFRESH DEMO");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_pos(label, 24, 56);

    label = lv_label_create(demo->root);
    if (label == NULL)
        goto failed;
    lv_label_set_text(label, "Continuous full-screen refresh");
    lv_obj_set_style_text_color(label, lv_color_hex(0x96a4bd), LV_PART_MAIN);
    lv_obj_set_pos(label, 24, 88);

    demo->fps_label = lv_label_create(demo->root);
    if (demo->fps_label == NULL)
        goto failed;
    lv_label_set_text(demo->fps_label, "FPS: --");
    lv_obj_set_style_text_font(demo->fps_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(demo->fps_label, lv_color_hex(0x27d9ff), LV_PART_MAIN);
    lv_obj_align(demo->fps_label, LV_ALIGN_TOP_RIGHT, -24, 56);

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
    button = lv_button_create(demo->root);
    if (button == NULL)
        goto failed;
    lv_obj_set_size(button, 140, 44);
    lv_obj_align(button, LV_ALIGN_BOTTOM_RIGHT, -24, -20);
    lv_obj_add_event_cb(button, app_refresh_demo_exit_cb, LV_EVENT_CLICKED, demo);
    label = lv_label_create(button);
    if (label == NULL)
        goto failed;
    lv_label_set_text(label, "EXIT DEMO");
    lv_obj_center(label);

    if (lv_display_add_event_cb(demo->display, app_refresh_demo_frame_cb, LV_EVENT_FLUSH_FINISH, demo) == NULL)
        goto failed;
    demo->started = demo->sampled = lv_tick_get();
    demo->timer = lv_timer_create(app_refresh_demo_timer_cb, 16, demo);
    if (demo->timer == NULL)
        goto failed;
    APP_LOG(LOG_INFO, "refresh demo started: continuous full-frame redraw, tap EXIT DEMO to stop\n");
    return demo->root;
failed:
    lv_obj_delete(demo->root);
    return NULL;
}
