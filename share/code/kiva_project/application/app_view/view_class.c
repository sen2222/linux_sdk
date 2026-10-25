#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lvgl/lvgl.h"


#include "view_class.h"
#include "status_bar.h"
#include "refresh_demo.h"
#include "app_config.h"

#include "ds/ds_stack.h"
#include "lvgl_port/lvgl_port.h"
#include "camera/ov5640.h"

struct _view_class
{
    APP_VIEW_INFO_S             main_view;
    DS_STACK_S                  *g_view_stack;
};

struct _main_view_data
{
    lv_obj_t    *label;
    lv_obj_t    *btn;
};


static struct _view_class g_view_class = {0};


static void app_main_btn_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    switch (code)
    {
        case LV_EVENT_PRESSED:
        {
            ov5640_led_on();
        }break;
        case LV_EVENT_RELEASED:
        {
            ov5640_led_off();
        }break;
        default:
            break;
    }
}
static int app_main_view_init(APP_VIEW_INFO_S *view_info)
{
   
    if(view_info == NULL)
    {
        APP_LOG(LOG_ERRO, "view data is NULL\n");
        return -1;
    }
    view_info->view_screen = lv_obj_create(NULL);
    if (view_info->view_screen == NULL)
    {
        APP_LOG(LOG_ERRO, "failed to create main view screen\n");
        return -1;
    }

    struct _main_view_data *main_view_data = malloc(sizeof(struct _main_view_data));
    if (main_view_data == NULL)
    {
        APP_LOG(LOG_ERRO, "failed to allocate main view data\n");
        return -1;
    }
    memset(main_view_data, 0, sizeof(struct _main_view_data));


    /* 设置主界面壁纸 */
    lv_obj_set_style_bg_image_src(
        view_info->view_screen,
        "L:res/main_view_img.jpg",
        LV_PART_MAIN);

    /* 创建标题 */
    main_view_data->label = lv_label_create(view_info->view_screen);
    lv_label_set_text(main_view_data->label, "KIVA MAIN VIEW");
    lv_obj_align(main_view_data->label, LV_ALIGN_CENTER, 0, -60);

    /* 创建按钮 */
    main_view_data->btn = lv_button_create(view_info->view_screen);
    lv_obj_set_size(main_view_data->btn, 160, 60);
    lv_obj_align(main_view_data->btn, LV_ALIGN_CENTER, 0, 40);
    lv_obj_add_event_cb(main_view_data->btn, app_main_btn_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(main_view_data->btn, app_main_btn_event_cb, LV_EVENT_RELEASED, NULL);

    

    view_info->view_data = main_view_data;
    /* 临时刷屏演示：默认启动，点击 EXIT DEMO 后回到主界面。 */
    if (app_refresh_demo_create(view_info->view_screen) == NULL)
        APP_LOG(LOG_WARN, "failed to create refresh demo\n");
    return 0;
}
static void app_main_view_deinit(APP_VIEW_INFO_S *view_info)
{
    struct _main_view_data *main_view_data = (struct _main_view_data *)view_info->view_data;
    if (main_view_data == NULL)
    {
        APP_LOG(LOG_ERRO, "main view data is NULL\n");
        return;
    }
    lv_obj_del(view_info->view_screen);
    free(main_view_data);
    view_info->view_data = NULL;
}


int app_switch_new_view(APP_VIEW_INFO_S *view_info)
{
    int ret = 0;
    APP_VIEW_INFO_S *top_view_data = NULL;
    if (view_info == NULL || view_info->view_init == NULL || view_info->view_deinit == NULL)
    {
        APP_LOG(LOG_ERRO, "view info is NULL || view_init or view_deinit is NULL\n");
        return -1;
    }
    top_view_data = (APP_VIEW_INFO_S *)ds_stack_top(g_view_class.g_view_stack);         // 先取栈顶界面
    if(top_view_data == view_info)                                                      // 如果是同一个界面，直接返回
    {
        APP_LOG(LOG_WARN,"already in the same view\n");
        return 0;
    }

    lvgl_port_mutex_lock();
    ret = view_info->view_init(view_info);
    if (ret < 0)
    {
        APP_LOG(LOG_ERRO, "failed to initialize view data\n");
        lvgl_port_mutex_unlock();
        return -1;
    }
    ds_stack_push(g_view_class.g_view_stack, (void *)view_info);  
    lv_screen_load(view_info->view_screen);
    if (top_view_data != NULL)
        top_view_data->view_deinit(top_view_data);
    lvgl_port_mutex_unlock();

    return 0;
    
}

int app_back_old_view(void)
{
    int ret = 0;
    APP_VIEW_INFO_S *old_view_data = NULL, *new_view_data = NULL;

    // 先取栈顶界面反初始化
    old_view_data = (APP_VIEW_INFO_S *)ds_stack_top(g_view_class.g_view_stack);
    if(old_view_data == NULL)
    {
        APP_LOG(LOG_ERRO, "failed to pop view data from stack\n");
        return -1;
    }else if(old_view_data == &g_view_class.main_view)                                  // 如果是主界面，直接返回
    {
        APP_LOG(LOG_WARN,"already in main view\n");
        return 0;
    }
    old_view_data = (APP_VIEW_INFO_S *)ds_stack_pop(g_view_class.g_view_stack);             // 先出栈顶的旧界面
    new_view_data = (APP_VIEW_INFO_S *)ds_stack_top(g_view_class.g_view_stack);             // 再取栈顶的新界面

    lvgl_port_mutex_lock();
    ret = new_view_data->view_init(new_view_data);                                          // 再初始化
    if(ret < 0)
    {
        APP_LOG(LOG_ERRO, "failed to initialize view data\n");
        ds_stack_push(g_view_class.g_view_stack, old_view_data);
        lvgl_port_mutex_unlock();
        return -1;
    }
    lv_screen_load(new_view_data->view_screen);
    old_view_data->view_deinit(old_view_data);
    lvgl_port_mutex_unlock();
    return 0;
}



int app_view_init(void)
{
    if (g_view_class.g_view_stack != NULL)
    {
        APP_LOG(LOG_ERRO, "view stack already initialized\n");
        return -1;
    }

    int ret = 0;
    g_view_class.g_view_stack = ds_stack_create();
    if(g_view_class.g_view_stack == NULL)
    {
        APP_LOG(LOG_ERRO, "failed to create view stack\n");
        return -1;
    }
    // 进入主界面
    g_view_class.main_view.view_init = app_main_view_init;
    g_view_class.main_view.view_deinit = app_main_view_deinit;
    g_view_class.main_view.view_data = NULL;

    lvgl_port_mutex_lock();
    app_status_bar_init();
    lvgl_port_mutex_unlock();
    app_switch_new_view(&g_view_class.main_view);
    
    return 0;
}
