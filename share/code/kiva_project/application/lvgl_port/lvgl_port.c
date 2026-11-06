
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

#include "lvgl/lvgl.h"
#include "lvgl_port.h"

#include "app_config.h"
#include "touch/touch_input.h"
#include "screen/screen_fbdev.h"

struct _lvgl_ctx
{
    TOUCH_INPUT_S touch;
    int touch_fd;
    int lvgl_initialized;
    int thread_started;
    int display_failed;

    atomic_uint     lvgl_timer_running;
    pthread_t       lvgl_timer_thread;
    pthread_mutex_t lvgl_timer_mutex;
};
static struct _lvgl_ctx lvgl_ctx = {.touch_fd = -1, .lvgl_timer_mutex = PTHREAD_MUTEX_INITIALIZER};



/* LVGL使用经过的毫秒数，不需要校准系统日期。 */
static uint32_t tick_get(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void display_flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    uint8_t index;
    (void)area;

    /* DIRECT模式一帧可能刷新多个区域，只在最后一个区域画完后翻页。 */
    if (lvgl_ctx.display_failed || !lv_display_flush_is_last(display))
    {
        lv_display_flush_ready(display);
        return;
    }
    if (pixels != NULL && pixels == screen_fbdev_get_buffer(0))
    {
        index = 0;
    }
    else if (pixels != NULL && pixels == screen_fbdev_get_buffer(1))
    {
        index = 1;
    }
    else
    {
        APP_LOG(LOG_ERRO, "Invalid framebuffer buffer\n");
        lvgl_ctx.display_failed = 1;
        lv_display_enable_invalidation(display, false);
        lv_timer_pause(lv_display_get_refr_timer(display));
        lv_display_flush_ready(display);
        return;
    }
    if (screen_fbdev_set_show_index(index) < 0)
    {
        APP_LOG(LOG_ERRO, "Framebuffer page flip failed, display refresh paused\n");
        lvgl_ctx.display_failed = 1;
        lv_display_enable_invalidation(display, false);
        lv_timer_pause(lv_display_get_refr_timer(display));
    }
    lv_display_flush_ready(display);
}

static lv_display_t *display_init(void)
{
    lv_display_t *display;
    uint32_t width, height;
    uint32_t buffer_size, stride;
    uint8_t *buffer0, *buffer1;

    if (screen_fbdev_init() < 0)
        return NULL;
    if (screen_fbdev_get_resolution(&width, &height) < 0 || screen_fbdev_get_buffer_info(&buffer_size, &stride) < 0)
        return NULL;
    buffer0 = screen_fbdev_get_buffer(0);
    buffer1 = screen_fbdev_get_buffer(1);
    if (buffer0 == NULL || buffer1 == NULL || buffer0 != lv_draw_buf_align(buffer0, LV_COLOR_FORMAT_RGB565) || buffer1 != lv_draw_buf_align(buffer1, LV_COLOR_FORMAT_RGB565))
    {
        APP_LOG(LOG_ERRO, "Framebuffer buffers do not meet LVGL alignment requirements\n");
        return NULL;
    }
    display = lv_display_create(width, height);
    if (display == NULL)
        return NULL;
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers_with_stride(display, buffer1, buffer0, buffer_size, stride, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, display_flush_cb);
    APP_LOG(LOG_INFO, "fbdev %ux%u RGB565 stride=%u draw_buffers=2 DIRECT VSYNC_flip=on\n", width, height, stride);
    return display;
}

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
       delay_ms = lvgl_ctx.display_failed ? 20 : lv_timer_handler();
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
    int ret;

    if (lvgl_ctx.lvgl_initialized)
        return lvgl_ctx.display_failed ? -1 : 0;
    lv_init();
    lvgl_ctx.lvgl_initialized = 1;
    lv_tick_set_cb(tick_get);
    display = display_init();
    if(!display)
    {
        APP_LOG(LOG_ERRO, "Cannot initialize framebuffer\n");
        goto LVGL_PROT_INIT_ERRO;
    }
    APP_LOG(LOG_INFO, "fbdev init success\n");


    lvgl_ctx.touch_fd = touch_init(&lvgl_ctx.touch, lv_display_get_horizontal_resolution(display), lv_display_get_vertical_resolution(display));
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
    /* 按累计位移识别滑动；慢拖时不清零，移动约100像素即可触发。 */
    lv_indev_set_gesture_min_velocity(input, 0);
    lv_indev_set_gesture_min_distance(input, 100);


    atomic_store(&lvgl_ctx.lvgl_timer_running, 1);
    ret = pthread_create(&lvgl_ctx.lvgl_timer_thread, NULL, lvgl_prot_timer_thread, NULL);
    if (ret != 0)
    {
        APP_LOG(LOG_ERRO, "Cannot create LVGL timer thread: %s\n", strerror(ret));
        goto LVGL_PROT_INIT_ERRO;
    }
    lvgl_ctx.thread_started = 1;

    APP_LOG(LOG_INFO, "lvgl port init success\n");

    return 0;

LVGL_PROT_INIT_ERRO:
    lvgl_port_deinit();
    return -1;
}
void lvgl_port_deinit(void)
{
    atomic_store(&lvgl_ctx.lvgl_timer_running, 0);
    if (lvgl_ctx.thread_started)
    {
        int ret = pthread_join(lvgl_ctx.lvgl_timer_thread, NULL);
        if (ret != 0)
        {
            APP_LOG(LOG_ERRO, "Cannot join LVGL timer thread: %s\n", strerror(ret));
            return;
        }
        lvgl_ctx.thread_started = 0;
    }
    if (lvgl_ctx.lvgl_initialized)
    {
        lv_deinit();
        lvgl_ctx.lvgl_initialized = 0;
    }
    if (lvgl_ctx.touch_fd >= 0) 
    {
        close(lvgl_ctx.touch_fd);
        lvgl_ctx.touch_fd = -1;
    }
    /* 显示对象已销毁，LVGL不会再访问映射的显存。 */
    screen_fbdev_deinit();
    lvgl_ctx.display_failed = 0;
}
