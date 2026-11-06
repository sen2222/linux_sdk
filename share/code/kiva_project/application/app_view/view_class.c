#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl/lvgl.h"
#include "app_config.h"
#include "view_class.h"
#include "status_bar.h"
#include "refresh_demo.h"
#include "camera_view.h"
#include "gallery_view.h"
#include "lvgl_port/lvgl_port.h"

struct _main_view_data
{
    lv_obj_t *screen;
    lv_obj_t *apps;
    lv_obj_t *active_view;
};

struct _main_app
{
    const char *name;
    const char *symbol;
    uint32_t color;
    lv_obj_t *(*create)(lv_obj_t *parent);
};

#define MAIN_VIEW_APPS_PER_ROW 5

static struct _main_view_data *g_main_view = NULL;

static void app_main_child_delete_cb(lv_event_t *event)
{
    struct _main_view_data *main_view_data = lv_event_get_user_data(event);
    main_view_data->active_view = NULL;
}

static void app_main_delete_cb(lv_event_t *event)
{
    struct _main_view_data *main_view_data = lv_event_get_user_data(event);
    /* 父页面先收到DELETE，子页面随后销毁，先撤掉引用父数据的回调。 */
    if (main_view_data->active_view != NULL)
        lv_obj_remove_event_cb_with_user_data(main_view_data->active_view, app_main_child_delete_cb, main_view_data);
    if (g_main_view == main_view_data)
        g_main_view = NULL;
    free(main_view_data);
}

static void app_main_open_event_cb(lv_event_t *event)
{
    struct _main_view_data *main_view_data = lv_event_get_user_data(event);
    const struct _main_app *app = lv_obj_get_user_data(lv_event_get_current_target_obj(event));
    if (main_view_data->active_view != NULL)
        return;
    /* LVGL回调中直接创建子页面，删除子页面就是返回，无需另维护一份界面栈。 */
    main_view_data->active_view = app->create(main_view_data->screen);
    if (main_view_data->active_view == NULL)
    {
        APP_LOG(LOG_ERRO, "failed to open %s\n", app->name);
        return;
    }
    lv_obj_add_event_cb(main_view_data->active_view, app_main_child_delete_cb, LV_EVENT_DELETE, main_view_data);
}

static int app_main_create_icon(struct _main_view_data *main_view_data, const struct _main_app *app, unsigned index)
{
    lv_obj_t *button, *icon, *label;
    button = lv_button_create(main_view_data->apps);
    if (button == NULL)
        return -1;
    lv_obj_remove_style_all(button);
    /* 每个应用占一行的1/5；新增应用只需要在应用表后继续添加。 */
    lv_obj_set_size(button, LV_PCT(100 / MAIN_VIEW_APPS_PER_ROW), 136);
    lv_obj_set_pos(button, LV_PCT((int32_t)(index % MAIN_VIEW_APPS_PER_ROW) * 100 / MAIN_VIEW_APPS_PER_ROW),
                   index / MAIN_VIEW_APPS_PER_ROW * 156);
    lv_obj_set_scrollable(button, false);
    lv_obj_set_press_lock(button, false);
    lv_obj_set_style_radius(button, 16, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_user_data(button, (void *)app);
    lv_obj_add_event_cb(button, app_main_open_event_cb, LV_EVENT_CLICKED, main_view_data);

    icon = lv_obj_create(button);
    if (icon == NULL)
        return -1;
    lv_obj_remove_style_all(icon);
    lv_obj_set_size(icon, 84, 84);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_set_style_radius(icon, 20, LV_PART_MAIN);
    lv_obj_set_style_bg_color(icon, lv_color_hex(app->color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(icon, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_clickable(icon, false);
    lv_obj_set_scrollable(icon, false);

    label = lv_label_create(icon);
    if (label == NULL)
        return -1;
    lv_label_set_text(label, app->symbol);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(label);

    label = lv_label_create(button);
    if (label == NULL)
        return -1;
    lv_label_set_text(label, app->name);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -10);
    return 0;
}

static int app_main_view_init(void)
{
    static const struct _main_app apps[] = {
        {"TEST", LV_SYMBOL_LIST, 0x287be8, app_refresh_demo_create},
        {"CAMERA", LV_SYMBOL_VIDEO, 0xe59a30, app_camera_view_create},
        {"GALLERY", LV_SYMBOL_IMAGE, 0x38966e, app_gallery_view_create},
    };
    struct _main_view_data *main_view_data = calloc(1, sizeof(*main_view_data));
    if (main_view_data == NULL)
        return -1;
    main_view_data->screen = lv_obj_create(NULL);
    if (main_view_data->screen == NULL)
    {
        free(main_view_data);
        return -1;
    }
    lv_obj_add_event_cb(main_view_data->screen, app_main_delete_cb, LV_EVENT_DELETE, main_view_data);
    lv_obj_set_style_pad_all(main_view_data->screen, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(main_view_data->screen, false);
    lv_obj_set_style_bg_image_src(main_view_data->screen, "L:res/main_view_img.jpg", LV_PART_MAIN);
    main_view_data->apps = lv_obj_create(main_view_data->screen);
    if (main_view_data->apps == NULL)
        goto MAIN_VIEW_ERRO;
    lv_obj_remove_style_all(main_view_data->apps);
    lv_obj_set_size(main_view_data->apps, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(main_view_data->apps, 24, LV_PART_MAIN);
    lv_obj_set_style_pad_top(main_view_data->apps, 64, LV_PART_MAIN);
    lv_obj_set_scroll_dir(main_view_data->apps, LV_DIR_VER);
    lv_obj_set_clickable(main_view_data->apps, false);
    for (unsigned i = 0; i < sizeof(apps) / sizeof(apps[0]); i++)
    {
        if (app_main_create_icon(main_view_data, &apps[i], i) < 0)
            goto MAIN_VIEW_ERRO;
    }
    g_main_view = main_view_data;
    lv_screen_load(main_view_data->screen);
    return 0;
MAIN_VIEW_ERRO:
    lv_obj_delete(main_view_data->screen);
    return -1;
}

int app_view_init(void)
{
    int ret;
    lvgl_port_mutex_lock();
    if (g_main_view != NULL)
    {
        lvgl_port_mutex_unlock();
        return 0;
    }
    ret = app_status_bar_init();
    if (ret == 0)
        ret = app_main_view_init();
    if (ret < 0)
        app_status_bar_deinit();
    lvgl_port_mutex_unlock();
    return ret;
}

lv_obj_t *app_view_back_button_create(lv_obj_t *parent, lv_event_cb_t callback, void *user_data)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label;
    if (button == NULL)
        return NULL;
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, 44, 36);
    lv_obj_set_pos(button, 8, 40);
    lv_obj_set_scrollable(button, false);
    lv_obj_set_style_radius(button, 18, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_50, LV_STATE_PRESSED);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    label = lv_label_create(button);
    if (label == NULL)
    {
        lv_obj_delete(button);
        return NULL;
    }
    lv_label_set_text(label, "<");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(label);
    return button;
}
