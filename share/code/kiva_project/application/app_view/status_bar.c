#include <string.h>
#include <time.h>

#include "lvgl/lvgl.h"

#include "app_config.h"
#include "status_bar.h"


struct _status_bar_data
{
    lv_obj_t    *bar;
    lv_obj_t    *time_label;
    lv_timer_t  *timer;
};

static struct _status_bar_data g_status_bar = {0};


static void app_status_bar_update_time(lv_timer_t *timer)
{
    time_t now = time(NULL);
    struct tm local_time;
    char text[6] = "--:--";
    (void)timer;

    if (now != (time_t)-1 && localtime_r(&now, &local_time) != NULL)
        strftime(text, sizeof(text), "%H:%M", &local_time);

    if (strcmp(lv_label_get_text(g_status_bar.time_label), text) != 0)
        lv_label_set_text(g_status_bar.time_label, text);
}

static void app_status_bar_delete_cb(lv_event_t *event)
{
    (void)event;
    if (g_status_bar.timer != NULL)
    {
        lv_timer_delete(g_status_bar.timer);
        g_status_bar.timer = NULL;
    }
    memset(&g_status_bar, 0, sizeof(g_status_bar));
}

int app_status_bar_init(void)
{
    lv_obj_t *top_layer;
    if (g_status_bar.bar != NULL)
    {
        APP_LOG(LOG_WARN, "status bar already initialized\n");
        return 0;
    }
    top_layer = lv_layer_top();
    if (top_layer == NULL)
    {
        APP_LOG(LOG_ERRO, "failed to get top layer\n");
        return -1;
    }

    g_status_bar.bar = lv_obj_create(top_layer);
    if (g_status_bar.bar == NULL)
    {
        APP_LOG(LOG_ERRO, "failed to create status bar\n");
        return -1;
    }
    lv_obj_add_event_cb(g_status_bar.bar, app_status_bar_delete_cb, LV_EVENT_DELETE, NULL);

    /* 顶层半透明状态栏，不占页面布局，也不拦截触摸。 */
    lv_obj_remove_style_all(g_status_bar.bar);
    lv_obj_set_style_bg_color(g_status_bar.bar, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_status_bar.bar, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_size(g_status_bar.bar, LV_PCT(100), 32);
    lv_obj_align(g_status_bar.bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_clickable(g_status_bar.bar, false);
    lv_obj_set_scrollable(g_status_bar.bar, false);

    g_status_bar.time_label = lv_label_create(g_status_bar.bar);
    if (g_status_bar.time_label == NULL)
    {
        APP_LOG(LOG_ERRO, "failed to create time label\n"); 
        app_status_bar_deinit();
        return -1;
    }
    lv_obj_set_clickable(g_status_bar.time_label, false);
    lv_obj_set_style_text_font(g_status_bar.time_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_status_bar.time_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_outline_stroke_color(g_status_bar.time_label, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_text_outline_stroke_width(g_status_bar.time_label, 1, LV_PART_MAIN);
    lv_obj_set_style_text_outline_stroke_opa(g_status_bar.time_label, LV_OPA_50, LV_PART_MAIN);
    lv_obj_align(g_status_bar.time_label, LV_ALIGN_LEFT_MID, 16, 0);

    app_status_bar_update_time(NULL);
    g_status_bar.timer = lv_timer_create(app_status_bar_update_time, 1000, NULL);
    if (g_status_bar.timer == NULL)
    {
        APP_LOG(LOG_ERRO, "failed to create status bar timer\n");
        app_status_bar_deinit();
        return -1;
    }
    return 0;
}

void app_status_bar_deinit(void)
{
    if (g_status_bar.bar != NULL)
        lv_obj_delete(g_status_bar.bar);
}
