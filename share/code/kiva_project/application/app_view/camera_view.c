#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lvgl/lvgl.h"
#include "app_config.h"
#include "camera/ov5640.h"
#include "camera_view.h"
#include "view_class.h"
#include "tool/photo_store.h"

enum _camera_state
{
    CAMERA_OPENING,
    CAMERA_RUNNING,
    CAMERA_ERROR,
};

enum _photo_state
{
    PHOTO_IDLE,
    PHOTO_QUEUED,
    PHOTO_SAVING,
    PHOTO_DONE,
    PHOTO_FAILED,
};

struct _camera_view
{
    lv_obj_t *root;
    lv_obj_t *image;
    lv_obj_t *message;
    lv_obj_t *light_switch;
    lv_obj_t *shot_button;
    lv_obj_t *photo_note;
    lv_timer_t *timer;
    lv_image_dsc_t image_data;
    OV5640_FRAME_S held_frame;
    int held_valid;
    int light_on;
    int closing;
    int thread_started;
    int last_state;
    int last_photo_state;
    uint8_t *photo_data;
    char photo_path[APP_PHOTO_PATH_MAX];
    int photo_error;
    atomic_int photo_state;
    unsigned frames;
    uint32_t sampled;
    pthread_t thread;
    pthread_mutex_t capture_mutex;
    atomic_uint request;
    atomic_uint requested_fps;
    atomic_uint applied_request;
    atomic_int state;
    atomic_int error;
    atomic_int capture_error;
    atomic_int stop;
    atomic_int done;
};

static void app_camera_save_pending(struct _camera_view *view)
{
    int ret;
    if (atomic_load(&view->photo_state) != PHOTO_QUEUED)
        return;
    atomic_store(&view->photo_state, PHOTO_SAVING);
    ret = app_photo_save_rgb565(view->photo_data, OV5640_CAMERA_WIDTH, OV5640_CAMERA_HEIGHT,
                                OV5640_CAMERA_WIDTH * 2, view->photo_path, sizeof(view->photo_path));
    view->photo_error = ret < 0 ? errno : 0;
    free(view->photo_data);
    view->photo_data = NULL;
    if (ret == 0)
        APP_LOG(LOG_INFO, "photo saved: %s\n", view->photo_path);
    else
        APP_LOG(LOG_ERRO, "photo save failed: %s\n", strerror(view->photo_error));
    atomic_store(&view->photo_state, ret < 0 ? PHOTO_FAILED : PHOTO_DONE);
}

static void *app_camera_worker(void *arg)
{
    struct _camera_view *view = arg;
    unsigned handled = 0;
    while (!atomic_load(&view->stop))
    {
        unsigned request = atomic_load(&view->request);
        int error = atomic_load(&view->capture_error);
        if (error)
        {
            atomic_store(&view->error, error);
            atomic_store(&view->state, CAMERA_ERROR);
            pthread_mutex_lock(&view->capture_mutex);
            ov5640_camera_stop();
            atomic_store(&view->capture_error, 0);
            pthread_mutex_unlock(&view->capture_mutex);
        }
        if (request != handled)
        {
            unsigned fps = atomic_load(&view->requested_fps);
            int ret;
            atomic_store(&view->state, CAMERA_OPENING);
            pthread_mutex_lock(&view->capture_mutex);
            /* UI先解除图像引用，再发出request，旧DMA映射此时才允许释放。 */
            ov5640_camera_stop();
            if (atomic_load(&view->stop))
            {
                pthread_mutex_unlock(&view->capture_mutex);
                break;
            }
            ret = ov5640_camera_start(fps);
            atomic_store(&view->error, ret < 0 ? errno : 0);
            atomic_store(&view->applied_request, request);
            atomic_store(&view->state, ret < 0 ? CAMERA_ERROR : CAMERA_RUNNING);
            pthread_mutex_unlock(&view->capture_mutex);
            handled = request;
        }
        app_camera_save_pending(view);
        usleep(20000);
    }
    pthread_mutex_lock(&view->capture_mutex);
    ov5640_camera_stop();
    pthread_mutex_unlock(&view->capture_mutex);
    /* 已按下快门的照片在离开相机时仍完成保存。 */
    app_camera_save_pending(view);
    atomic_store(&view->done, 1);
    return NULL;
}

static void app_camera_detach(struct _camera_view *view)
{
    if (view->image != NULL)
        lv_image_set_src(view->image, NULL);
    view->held_valid = 0;
}

static void app_camera_delete_cb(lv_event_t *event)
{
    struct _camera_view *view = lv_event_get_user_data(event);
    if (view->timer != NULL)
        lv_timer_delete(view->timer);
    app_camera_detach(view);
    atomic_store(&view->stop, 1);
    if (view->thread_started)
        pthread_join(view->thread, NULL);
    if (view->light_on)
        ov5640_led_off();
    free(view->photo_data);
    pthread_mutex_destroy(&view->capture_mutex);
    free(view);
}

static void app_camera_back_cb(lv_event_t *event)
{
    struct _camera_view *view = lv_event_get_user_data(event);
    if (view->closing)
        return;
    app_camera_detach(view);
    view->closing = 1;
    atomic_store(&view->stop, 1);
    lv_label_set_text(view->message, "Closing camera...");
    lv_obj_set_hidden(view->message, false);
    /* 由定时器等线程完成后删除，BACK不等待传感器寄存器配置。 */
}

static void app_camera_light_cb(lv_event_t *event)
{
    struct _camera_view *view = lv_event_get_user_data(event);
    int checked = (lv_obj_get_state(view->light_switch) & LV_STATE_CHECKED) != 0;
    int ret;
    if (view->closing)
        return;
    ret = checked ? ov5640_led_on() : ov5640_led_off();
    if (ret < 0)
    {
        lv_obj_set_state(view->light_switch, LV_STATE_CHECKED, view->light_on);
        lv_label_set_text(view->photo_note, "Light unavailable");
        return;
    }
    view->light_on = checked;
}

static void app_camera_shot_cb(lv_event_t *event)
{
    struct _camera_view *view = lv_event_get_user_data(event);
    int state = atomic_load(&view->photo_state);
    if (view->closing || !view->thread_started || state == PHOTO_QUEUED || state == PHOTO_SAVING)
        return;
    if (!view->held_valid)
    {
        lv_label_set_text(view->photo_note, "No frame yet");
        return;
    }
    view->photo_data = malloc(OV5640_CAMERA_WIDTH * OV5640_CAMERA_HEIGHT * 2);
    if (view->photo_data == NULL)
    {
        lv_label_set_text(view->photo_note, "Out of memory");
        return;
    }
    /* 仅拍照时复制一帧，之后归还DMA帧也不会影响正在编码的照片。 */
    for (unsigned y = 0; y < OV5640_CAMERA_HEIGHT; y++)
        memcpy(view->photo_data + y * OV5640_CAMERA_WIDTH * 2,
               view->held_frame.data + y * view->held_frame.stride, OV5640_CAMERA_WIDTH * 2);
    lv_label_set_text(view->photo_note, "Saving...");
    lv_obj_set_state(view->shot_button, LV_STATE_DISABLED, true);
    view->last_photo_state = PHOTO_QUEUED;
    atomic_store(&view->photo_state, PHOTO_QUEUED);
}

static void app_camera_timer_cb(lv_timer_t *timer)
{
    struct _camera_view *view = lv_timer_get_user_data(timer);
    int state = atomic_load(&view->state);
    unsigned fps = atomic_load(&view->requested_fps);
    uint32_t elapsed;
    int photo_state;
    if (view->closing)
    {
        if (view->closing == 1 && atomic_load(&view->done))
        {
            view->closing = 2;
            lv_obj_delete_async(view->root);
        }
        return;
    }
    if (atomic_load(&view->request) != atomic_load(&view->applied_request))
        state = CAMERA_OPENING;
    if (view->last_state != state)
    {
        if (state == CAMERA_ERROR)
        {
            lv_label_set_text_fmt(view->message, "Camera unavailable\n%s", strerror(atomic_load(&view->error)));
            lv_obj_set_hidden(view->message, false);
        }
        else if (state == CAMERA_OPENING)
        {
            lv_label_set_text(view->message, "Opening camera...");
            lv_obj_set_hidden(view->message, false);
        }
        view->last_state = state;
    }
    /* 不等待采集：寄存器配置在工作线程，UI只尝试获取锁并做非阻塞DQBUF。 */
    if (state == CAMERA_RUNNING && !atomic_load(&view->capture_error) &&
        pthread_mutex_trylock(&view->capture_mutex) == 0)
    {
        OV5640_FRAME_S frame;
        int ret = ov5640_camera_read(&frame, 0);
        if (ret > 0)
        {
            OV5640_FRAME_S old_frame = view->held_frame;
            int old_valid = view->held_valid;
            view->image_data.header.magic = LV_IMAGE_HEADER_MAGIC;
            view->image_data.header.cf = LV_COLOR_FORMAT_RGB565;
            view->image_data.header.w = frame.width;
            view->image_data.header.h = frame.height;
            view->image_data.header.stride = frame.stride;
            view->image_data.data_size = frame.size;
            view->image_data.data = frame.data;
            lv_image_set_src(view->image, &view->image_data);
            view->held_frame = frame;
            view->held_valid = 1;
            lv_obj_set_hidden(view->message, true);
            if (strcmp(lv_label_get_text(view->photo_note), "No frame yet") == 0)
                lv_label_set_text(view->photo_note, "");
            view->frames++;
            /* 显示帧一直持有到下一次替换；归还后CSI DMA才可以覆盖它。 */
            if (old_valid && ov5640_camera_release(&old_frame) < 0)
                ret = -1;
        }
        if (ret < 0)
        {
            int error = errno;
            app_camera_detach(view);
            atomic_store(&view->capture_error, error ? error : EIO);
        }
        pthread_mutex_unlock(&view->capture_mutex);
    }
    photo_state = atomic_load(&view->photo_state);
    if (photo_state != view->last_photo_state)
    {
        if (photo_state == PHOTO_DONE || photo_state == PHOTO_FAILED)
        {
            lv_obj_set_state(view->shot_button, LV_STATE_DISABLED, false);
            lv_label_set_text(view->photo_note, photo_state == PHOTO_DONE ? "Saved" : "Save failed");
        }
        view->last_photo_state = photo_state;
    }
    elapsed = lv_tick_elaps(view->sampled);
    if (elapsed >= 5000 && state == CAMERA_RUNNING)
    {
        unsigned tenths = (uint64_t)view->frames * 10000 / elapsed;
        APP_LOG(LOG_INFO, "camera preview: %u.%u fps, target=%u\n", tenths / 10, tenths % 10, fps);
        view->frames = 0;
        view->sampled = lv_tick_get();
    }
}

static lv_obj_t *app_camera_label(lv_obj_t *parent, const char *text, int x, int y)
{
    lv_obj_t *label = lv_label_create(parent);
    if (label != NULL)
    {
        lv_label_set_text(label, text);
        lv_obj_set_pos(label, x, y);
    }
    return label;
}

static lv_obj_t *app_camera_button(lv_obj_t *parent, const char *text, int y,
                                  lv_event_cb_t callback, struct _camera_view *view, lv_obj_t **button_out)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label;
    if (button == NULL)
        return NULL;
    lv_obj_set_size(button, 128, 44);
    lv_obj_set_pos(button, 16, y);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, view);
    label = lv_label_create(button);
    if (label == NULL)
        return NULL;
    lv_label_set_text(label, text);
    lv_obj_center(label);
    if (button_out != NULL)
        *button_out = button;
    return label;
}

lv_obj_t *app_camera_view_create(lv_obj_t *parent)
{
    struct _camera_view *view;
    lv_obj_t *panel;
    int ret;
    if (parent == NULL || lv_display_get_horizontal_resolution(lv_obj_get_display(parent)) != 800 ||
        lv_display_get_vertical_resolution(lv_obj_get_display(parent)) != 480)
        return NULL;
    view = calloc(1, sizeof(*view));
    if (view == NULL)
        return NULL;
    ret = pthread_mutex_init(&view->capture_mutex, NULL);
    if (ret != 0)
    {
        free(view);
        return NULL;
    }
    atomic_init(&view->request, 1);
    atomic_init(&view->requested_fps, 15);
    atomic_init(&view->applied_request, 0);
    atomic_init(&view->state, CAMERA_OPENING);
    atomic_init(&view->error, 0);
    atomic_init(&view->capture_error, 0);
    atomic_init(&view->stop, 0);
    atomic_init(&view->done, 0);
    atomic_init(&view->photo_state, PHOTO_IDLE);
    view->last_state = -1;
    view->last_photo_state = PHOTO_IDLE;
    view->sampled = lv_tick_get();
    view->root = lv_obj_create(parent);
    if (view->root == NULL)
    {
        pthread_mutex_destroy(&view->capture_mutex);
        free(view);
        return NULL;
    }
    lv_obj_add_event_cb(view->root, app_camera_delete_cb, LV_EVENT_DELETE, view);
    lv_obj_remove_style_all(view->root);
    lv_obj_set_size(view->root, 800, 480);
    lv_obj_set_pos(view->root, 0, 0);
    lv_obj_set_scrollable(view->root, false);
    lv_obj_set_style_bg_color(view->root, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(view->root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(view->root, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(view->root, &lv_font_montserrat_16, LV_PART_MAIN);
    view->image = lv_image_create(view->root);
    if (view->image == NULL)
        goto CAMERA_VIEW_ERRO;
    lv_obj_set_size(view->image, 640, 480);
    lv_obj_set_pos(view->image, 0, 0);
    view->message = lv_label_create(view->root);
    if (view->message == NULL)
        goto CAMERA_VIEW_ERRO;
    lv_label_set_text(view->message, "Opening camera...");
    lv_obj_set_width(view->message, 600);
    lv_obj_set_style_text_align(view->message, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(view->message, LV_ALIGN_CENTER, -80, 0);
    panel = lv_obj_create(view->root);
    if (panel == NULL)
        goto CAMERA_VIEW_ERRO;
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 160, 480);
    lv_obj_set_pos(panel, 640, 0);
    lv_obj_set_scrollable(panel, false);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x101827), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    if (app_camera_label(panel, "CAMERA", 16, 56) == NULL ||
        app_camera_label(panel, "640 x 480", 16, 82) == NULL ||
        app_camera_label(panel, "LIGHT", 16, 120) == NULL ||
        app_camera_label(panel, "FRAME RATE", 16, 208) == NULL ||
        app_camera_label(panel, "15 FPS", 16, 238) == NULL)
        goto CAMERA_VIEW_ERRO;
    view->photo_note = app_camera_label(panel, "", 16, 408);
    view->light_switch = lv_switch_create(panel);
    if (view->photo_note == NULL || view->light_switch == NULL)
        goto CAMERA_VIEW_ERRO;
    lv_obj_set_width(view->photo_note, 128);
    lv_obj_set_style_text_font(view->photo_note, LV_FONT_DEFAULT, LV_PART_MAIN);
    lv_obj_set_pos(view->light_switch, 16, 150);
    lv_obj_set_size(view->light_switch, 64, 30);
    ret = ov5640_led_get_state();
    view->light_on = ret > 0;
    /* 先设置初值，再注册用户事件；进入页面不写灯光节点。 */
    lv_obj_set_state(view->light_switch, LV_STATE_CHECKED, view->light_on);
    lv_obj_set_state(view->light_switch, LV_STATE_DISABLED, ret < 0);
    lv_obj_add_event_cb(view->light_switch, app_camera_light_cb, LV_EVENT_VALUE_CHANGED, view);
    if (app_camera_button(panel, "SHOOT", 294, app_camera_shot_cb, view, &view->shot_button) == NULL ||
        app_view_back_button_create(view->root, app_camera_back_cb, view) == NULL)
        goto CAMERA_VIEW_ERRO;
    view->timer = lv_timer_create(app_camera_timer_cb, 16, view);
    if (view->timer == NULL)
        goto CAMERA_VIEW_ERRO;
    ret = pthread_create(&view->thread, NULL, app_camera_worker, view);
    if (ret != 0)
    {
        atomic_store(&view->error, ret);
        atomic_store(&view->applied_request, 1);
        atomic_store(&view->state, CAMERA_ERROR);
        atomic_store(&view->done, 1);
        lv_obj_set_state(view->shot_button, LV_STATE_DISABLED, true);
    }
    else
        view->thread_started = 1;
    return view->root;
CAMERA_VIEW_ERRO:
    lv_obj_delete(view->root);
    return NULL;
}
