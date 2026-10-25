
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <stdatomic.h>

#include "lvgl/lvgl.h"
#include "lvgl_port.h"
#include "lvgl_fbdev.h"

#include "app_config.h"
#include "touch/touch_input.h"

struct _lvgl_ctx
{
    TOUCH_INPUT_S touch;
    int touch_fd;

    atomic_int      lvgl_timer_running;
    pthread_t       lvgl_timer_thread;
    pthread_mutex_t lvgl_timer_mutex;
};
static struct _lvgl_ctx lvgl_ctx = {0};



static void touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    int frame;
    (void)indev;

    frame = touch_input_read(&lvgl_ctx.touch, lvgl_ctx.touch_fd);
    data->continue_reading = frame > 0;
    data->point.x = lvgl_ctx.touch.x;
    data->point.y = lvgl_ctx.touch.y;
    data->state = lvgl_ctx.touch.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void * lvgl_prot_timer_thread(void *arg)
{
    (void)arg;
    uint32_t delay_ms;

    APP_LOG(LOG_INFO, "lvgl timer thread start\n");
    while(atomic_load(&lvgl_ctx.lvgl_timer_running))
    {
       lvgl_port_mutex_lock();
       delay_ms = lv_timer_handler();
       lvgl_port_mutex_unlock();
       if (delay_ms < 1)
           delay_ms = 1;
       if (delay_ms > 20)
           delay_ms = 20;
       usleep(delay_ms * 1000);
    }
    APP_LOG(LOG_INFO, "lvgl timer thread exit\n");
    return NULL;
}


void lvgl_port_mutex_lock(void)
{
    pthread_mutex_lock(&lvgl_ctx.lvgl_timer_mutex);
}
void lvgl_port_mutex_unlock(void)
{
    pthread_mutex_unlock(&lvgl_ctx.lvgl_timer_mutex);
}

int lvgl_port_init(void)
{
    lv_display_t *display;
    lv_indev_t  *input;

    lv_init();
    /* 默认先尝试自定义显示链路：双画布 -> PxP/CPU 拷贝 -> 双显存页 VSYNC 翻页。 */
    display = kiva_fbdev_create(LVGL_PORT_FBDEV_PATH);
    if (display == NULL)
    {
        /* 格式/设备不支持，或设置了 KIVA_FB_MODE=legacy 时，使用原版 fbdev。 */
        APP_LOG(LOG_WARN, "using legacy LVGL fbdev presenter\n");
        display = lv_linux_fbdev_create();
        if (display != NULL && lv_linux_fbdev_set_file(display, LVGL_PORT_FBDEV_PATH) != LV_RESULT_OK)
        {
            lv_display_delete(display);
            display = NULL;
        }
    }
    if(!display)
    {
        APP_LOG(LOG_ERRO, "Cannot initialize framebuffer\n");
        goto LVGL_PROT_INIT_ERRO;
    }
    APP_LOG(LOG_INFO, "fbdev init success\n");


    lvgl_ctx.touch_fd = lvgl_port_touch_init(&lvgl_ctx.touch, lv_display_get_horizontal_resolution(display), lv_display_get_vertical_resolution(display));
    if(lvgl_ctx.touch_fd < 0)
    {
        APP_LOG(LOG_ERRO, "Cannot initialize touchscreen\n");
        goto LVGL_PROT_INIT_ERRO;
    }
    APP_LOG(LOG_INFO, "touch init success width = %d, height = %d\n", lvgl_ctx.touch.width, lvgl_ctx.touch.height);
    input = lv_indev_create();
    if (!input) {
        APP_LOG(LOG_ERRO, "Cannot create input device\n");
        goto LVGL_PROT_INIT_ERRO;
    }
    lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(input, touch_read);
    lv_indev_set_display(input, display);


    pthread_mutex_init(&lvgl_ctx.lvgl_timer_mutex, NULL);
    atomic_store(&lvgl_ctx.lvgl_timer_running, 1);
    pthread_create(&lvgl_ctx.lvgl_timer_thread, NULL, lvgl_prot_timer_thread, NULL);

    APP_LOG(LOG_INFO, "lvgl port init success\n");

    return 0;

LVGL_PROT_INIT_ERRO:
    lvgl_port_deinit();
    if(lvgl_ctx.touch_fd >= 0) {close(lvgl_ctx.touch_fd); lvgl_ctx.touch_fd = -1;}
    return -1;
}
void lvgl_port_deinit(void)
{
    atomic_store(&lvgl_ctx.lvgl_timer_running, 0);
    pthread_join(lvgl_ctx.lvgl_timer_thread, NULL);
    pthread_mutex_destroy(&lvgl_ctx.lvgl_timer_mutex);
    lv_deinit();
    if (lvgl_ctx.touch_fd >= 0) 
    {
        close(lvgl_ctx.touch_fd);
        lvgl_ctx.touch_fd = -1;
    }
}
