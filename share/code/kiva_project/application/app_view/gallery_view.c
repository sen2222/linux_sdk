#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "lvgl/lvgl.h"
#include "app_config.h"
#include "tool/photo_store.h"
#include "view_class.h"
#include "gallery_view.h"

#define GALLERY_PAGE_SIZE 4
#define GALLERY_MAX_DIMENSION 8192U
#define GALLERY_MAX_PIXELS (8U * 1024U * 1024U)

/* LVGL的LodePNG解码接口返回lv_draw_buf_t句柄，供图库缓存完整图像。 */
extern unsigned lodepng_decode32_file(unsigned char **out, unsigned *width,
                                    unsigned *height, const char *filename);

struct _gallery_view
{
    lv_obj_t *root;
    lv_obj_t *grid;
    lv_obj_t *page_label;
    lv_obj_t *viewer;
    lv_obj_t *image;
    lv_obj_t *image_error;
    lv_obj_t *image_title;
    lv_obj_t *image_count;
    lv_obj_t *confirm;
    lv_obj_t *confirm_note;
    lv_draw_buf_t *thumbnails[GALLERY_PAGE_SIZE];
    lv_draw_buf_t *viewer_buffer;
    APP_PHOTO_LIST_S files;
    size_t page;
    size_t selected;
    int list_error;
    int closing;
    int viewer_closing;
};

static void app_gallery_show_page(struct _gallery_view *view);
static void app_gallery_page_step(struct _gallery_view *view, int direction);

static void app_gallery_slide_x(void *object, int32_t x)
{
    lv_obj_set_x(object, x);
}

static void app_gallery_slide(lv_obj_t *object, int x, int direction)
{
    lv_anim_t animation;
    lv_anim_delete(object, app_gallery_slide_x);
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, object);
    lv_anim_set_exec_cb(&animation, app_gallery_slide_x);
    lv_anim_set_values(&animation, x + direction * 752, x);
    lv_anim_set_duration(&animation, 180);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
}

static lv_obj_t *app_gallery_root(lv_obj_t *parent)
{
    lv_obj_t *root = lv_obj_create(parent);
    if (root == NULL)
        return NULL;
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, 800, 480);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_scrollable(root, false);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x0b1020), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(root, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(root, &lv_font_montserrat_16, LV_PART_MAIN);
    return root;
}

static lv_obj_t *app_gallery_label(lv_obj_t *parent, const char *text, int x, int y)
{
    lv_obj_t *label = lv_label_create(parent);
    if (label != NULL)
    {
        lv_label_set_text(label, text);
        lv_obj_set_pos(label, x, y);
    }
    return label;
}

static lv_obj_t *app_gallery_button(lv_obj_t *parent, const char *text, int x, int y,
                                    lv_event_cb_t callback, struct _gallery_view *view)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label;
    if (button == NULL)
        return NULL;
    lv_obj_set_size(button, 120, 36);
    lv_obj_set_pos(button, x, y);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, view);
    label = lv_label_create(button);
    if (label == NULL)
    {
        lv_obj_delete(button);
        return NULL;
    }
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

/* TJPGD accepts lowercase extensions only. This read-only alias driver lets
 * uppercase names use the same decoder without renaming or copying files.
 * All opens finish inside the synchronous snapshot; the registered driver has
 * static lifetime, and LVGL releases its registration at deinitialization. */
static lv_fs_drv_t gallery_jpeg_driver;

static void *app_gallery_jpeg_open(lv_fs_drv_t *driver, const char *path, lv_fs_mode_t mode)
{
    lv_fs_file_t *file;
    (void)path;
    if (driver->user_data == NULL || mode != LV_FS_MODE_RD)
        return NULL;
    file = calloc(1, sizeof(*file));
    if (file != NULL && lv_fs_open(file, driver->user_data, mode) != LV_FS_RES_OK)
    {
        free(file);
        file = NULL;
    }
    return file;
}

static lv_fs_res_t app_gallery_jpeg_close(lv_fs_drv_t *driver, void *file)
{
    lv_fs_res_t result = lv_fs_close(file);
    (void)driver;
    free(file);
    return result;
}

static lv_fs_res_t app_gallery_jpeg_read(lv_fs_drv_t *driver, void *file, void *data,
                                        uint32_t size, uint32_t *read)
{
    (void)driver;
    return lv_fs_read(file, data, size, read);
}

static lv_fs_res_t app_gallery_jpeg_seek(lv_fs_drv_t *driver, void *file, uint32_t pos,
                                        lv_fs_whence_t whence)
{
    (void)driver;
    return lv_fs_seek(file, pos, whence);
}

static lv_fs_res_t app_gallery_jpeg_tell(lv_fs_drv_t *driver, void *file, uint32_t *pos)
{
    (void)driver;
    return lv_fs_tell(file, pos);
}

static int app_gallery_jpeg_alias(const char *path, char *alias, size_t size)
{
    if (gallery_jpeg_driver.letter == 0 ||
        lv_fs_get_drv(gallery_jpeg_driver.letter) != &gallery_jpeg_driver)
    {
        char letter;
        for (letter = 'A'; letter <= 'Z'; letter++)
            if (lv_fs_get_drv(letter) == NULL)
                break;
        if (letter > 'Z')
            return -1;
        lv_fs_drv_init(&gallery_jpeg_driver);
        gallery_jpeg_driver.letter = letter;
        gallery_jpeg_driver.open_cb = app_gallery_jpeg_open;
        gallery_jpeg_driver.close_cb = app_gallery_jpeg_close;
        gallery_jpeg_driver.read_cb = app_gallery_jpeg_read;
        gallery_jpeg_driver.seek_cb = app_gallery_jpeg_seek;
        gallery_jpeg_driver.tell_cb = app_gallery_jpeg_tell;
        lv_fs_drv_register(&gallery_jpeg_driver);
        if (lv_fs_get_drv(letter) != &gallery_jpeg_driver)
            return -1;
    }
    if ((size_t)snprintf(alias, size, "%c%s.jpg", gallery_jpeg_driver.letter, path + 1) >= size)
        return -1;
    gallery_jpeg_driver.user_data = (void *)path;
    return 0;
}

static void app_gallery_buffer_free(lv_draw_buf_t **buffer)
{
    if (*buffer != NULL)
    {
        lv_draw_buf_destroy(*buffer);
        *buffer = NULL;
    }
}

static void app_gallery_thumbnails_free(struct _gallery_view *view)
{
    for (size_t i = 0; i < GALLERY_PAGE_SIZE; i++)
        app_gallery_buffer_free(&view->thumbnails[i]);
}

static int app_gallery_set_image(lv_obj_t *image, const char *path,
                                  lv_draw_buf_t **buffer, lv_image_header_t *header)
{
    const char *extension = strrchr(path, '.');
    const char *source = path;
    char alias[APP_PHOTO_PATH_MAX + 4];
    int jpeg = extension != NULL &&
               (!strcasecmp(extension, ".jpg") || !strcasecmp(extension, ".jpeg"));
    lv_obj_t *screen = NULL, *unscaled;
    int result = -1;

    /* LVGL keeps the pointer of a variable source; detach it before freeing. */
    lv_image_set_src(image, NULL);
    app_gallery_buffer_free(buffer);
    if (jpeg && strcmp(extension, ".jpg") && strcmp(extension, ".jpeg"))
    {
        if (app_gallery_jpeg_alias(path, alias, sizeof(alias)) < 0)
            return -1;
        source = alias;
    }
    if (lv_image_decoder_get_info(source, header) != LV_RESULT_OK)
        goto done;
    if (header->w == 0 || header->h == 0 ||
        header->w > GALLERY_MAX_DIMENSION || header->h > GALLERY_MAX_DIMENSION ||
        (uint64_t)header->w * header->h > GALLERY_MAX_PIXELS)
        goto done;
    if (!jpeg)
    {
        unsigned width = 0, height = 0;
        unsigned char *decoded = NULL;
        lv_fs_file_t file;
        uint32_t file_size = 0;
        int valid_size;
        if (lv_fs_open(&file, source, LV_FS_MODE_RD) != LV_FS_RES_OK)
            goto done;
        valid_size = lv_fs_seek(&file, 0, LV_FS_SEEK_END) == LV_FS_RES_OK &&
                     lv_fs_tell(&file, &file_size) == LV_FS_RES_OK && file_size <= 64U * 1024U * 1024U;
        lv_fs_close(&file);
        if (!valid_size)
            goto done;
        /* 真正解码一次，既检查PNG内容，也避免每次缩略图重绘都重新解压。 */
        unsigned error = lodepng_decode32_file(&decoded, &width, &height, source);
        *buffer = (lv_draw_buf_t *)decoded;
        if (error || *buffer == NULL || width != header->w || height != header->h)
        {
            app_gallery_buffer_free(buffer);
            goto done;
        }
        /* LodePNG输出RGBA；LVGL的ARGB8888在小端内存中采用BGRA。 */
        for (size_t i = 0; i < (size_t)width * height; i++)
        {
            uint8_t *pixel = (*buffer)->data + i * 4;
            uint8_t red = pixel[0];
            pixel[0] = pixel[2];
            pixel[2] = red;
        }
        lv_image_set_src(image, *buffer);
        result = lv_image_get_src(image) == *buffer ? 0 : -1;
        if (result < 0)
            app_gallery_buffer_free(buffer);
        goto done;
    }

    /* TJPGD produces MCU blocks and cannot scale while decoding. Render at
     * native size once, then scale the complete RGB565 buffer in the widget. */
    screen = lv_obj_create(NULL);
    if (screen == NULL)
        goto done;
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, header->w, header->h);
    lv_obj_set_scrollable(screen, false);
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    unscaled = lv_image_create(screen);
    if (unscaled == NULL)
        goto done;
    lv_obj_remove_style_all(unscaled);
    lv_obj_set_size(unscaled, header->w, header->h);
    lv_obj_set_pos(unscaled, 0, 0);
    lv_image_set_inner_align(unscaled, LV_IMAGE_ALIGN_TOP_LEFT);
    lv_image_set_src(unscaled, source);
    if (lv_image_get_src(unscaled) == NULL)
        goto done;
    *buffer = lv_snapshot_take(unscaled, LV_COLOR_FORMAT_RGB565);
    if (*buffer != NULL)
    {
        lv_image_set_src(image, *buffer);
        result = lv_image_get_src(image) == *buffer ? 0 : -1;
        if (result < 0)
            app_gallery_buffer_free(buffer);
    }
done:
    if (screen != NULL)
        lv_obj_delete(screen);
    gallery_jpeg_driver.user_data = NULL;
    return result;
}

static void app_gallery_viewer_delete_cb(lv_event_t *event)
{
    struct _gallery_view *view = lv_event_get_user_data(event);
    if (view->image != NULL)
    {
        lv_anim_delete(view->image, app_gallery_slide_x);
        lv_image_set_src(view->image, NULL);
    }
    app_gallery_buffer_free(&view->viewer_buffer);
    view->image = NULL;
    view->viewer = NULL;
    view->viewer_closing = 0;
}

static void app_gallery_delete_cb(lv_event_t *event)
{
    struct _gallery_view *view = lv_event_get_user_data(event);
    /* DELETE is delivered before children are deleted. Destroy them while
     * their callbacks and image sources still have a live context. */
    lv_obj_clean(view->root);
    app_gallery_thumbnails_free(view);
    app_photo_list_free(&view->files);
    free(view);
}

static void app_gallery_back_cb(lv_event_t *event)
{
    struct _gallery_view *view = lv_event_get_user_data(event);
    if (view->closing)
        return;
    view->closing = 1;
    lv_obj_delete_async(view->root);
}

static void app_gallery_viewer_back_cb(lv_event_t *event)
{
    struct _gallery_view *view = lv_event_get_user_data(event);
    if (view->viewer != NULL && !view->viewer_closing && view->confirm == NULL)
    {
        view->viewer_closing = 1;
        lv_obj_delete_async(view->viewer);
    }
}

static void app_gallery_show_image(struct _gallery_view *view)
{
    char path[APP_PHOTO_PATH_MAX];
    lv_image_header_t header;
    lv_anim_delete(view->image, app_gallery_slide_x);
    lv_obj_set_x(view->image, 24);
    snprintf(path, sizeof(path), "L:res/%s", view->files.entries[view->selected].name);
    lv_label_set_text(view->image_title, view->files.entries[view->selected].name);
    lv_label_set_text_fmt(view->image_count, "%zu / %zu", view->selected + 1, view->files.count);
    if (app_gallery_set_image(view->image, path, &view->viewer_buffer, &header) == 0)
    {
        lv_obj_set_hidden(view->image, false);
        lv_obj_set_hidden(view->image_error, true);
    }
    else
    {
        lv_image_set_src(view->image, NULL);
        lv_obj_set_hidden(view->image, true);
        lv_obj_set_hidden(view->image_error, false);
    }
}

static void app_gallery_image_step(struct _gallery_view *view, int direction)
{
    if (view->closing || view->viewer_closing || view->confirm != NULL || view->files.count < 2)
        return;
    if (direction < 0)
    {
        view->selected = view->selected ? view->selected - 1 : view->files.count - 1;
    }
    else
    {
        view->selected = (view->selected + 1) % view->files.count;
    }
    app_gallery_show_image(view);
    if (!lv_obj_is_hidden(view->image))
    {
        /* 不额外缓存一张大图，也不逐帧重新解码。 */
        app_gallery_slide(view->image, 24, direction);
    }
}

static void app_gallery_swipe_cb(lv_event_t *event)
{
    struct _gallery_view *view = lv_event_get_user_data(event);
    lv_indev_t *indev = lv_event_get_indev(event);
    int direction = lv_event_get_code(event) == LV_EVENT_GESTURE_LEFT ? 1 : -1;
    /* 一次手势只切一张，松手时不再触发手指下面的按钮。 */
    if (indev != NULL)
        lv_indev_wait_release(indev);
    if (lv_event_get_current_target_obj(event) == view->viewer)
        app_gallery_image_step(view, direction);
    else
        app_gallery_page_step(view, direction);
}

static void app_gallery_confirm_delete_cb(lv_event_t *event)
{
    struct _gallery_view *view = lv_event_get_user_data(event);
    view->confirm = NULL;
    view->confirm_note = NULL;
}

static void app_gallery_cancel_cb(lv_event_t *event)
{
    struct _gallery_view *view = lv_event_get_user_data(event);
    if (view->confirm != NULL)
        lv_obj_delete(view->confirm);
}

static void app_gallery_remove_cb(lv_event_t *event)
{
    struct _gallery_view *view = lv_event_get_user_data(event);
    if (view->confirm == NULL || view->closing || view->viewer_closing ||
        view->selected >= view->files.count)
        return;
    if (app_photo_delete(view->files.entries[view->selected].name) < 0)
    {
        lv_label_set_text_fmt(view->confirm_note, "Delete failed: %s", strerror(errno));
        return;
    }
    lv_obj_delete(view->confirm);
    /* 在原列表中移除，保留排序和当前浏览位置；缩略图和计数随后一起重建。 */
    view->files.count--;
    memmove(&view->files.entries[view->selected], &view->files.entries[view->selected + 1],
            (view->files.count - view->selected) * sizeof(*view->files.entries));
    if (view->selected >= view->files.count)
        view->selected = view->files.count ? view->files.count - 1 : 0;
    view->page = view->selected / GALLERY_PAGE_SIZE;
    app_gallery_show_page(view);
    if (view->files.count != 0)
        app_gallery_show_image(view);
    else
    {
        view->viewer_closing = 1;
        lv_obj_delete_async(view->viewer);
    }
}

static void app_gallery_trash_cb(lv_event_t *event)
{
    struct _gallery_view *view = lv_event_get_user_data(event);
    lv_obj_t *panel, *label;
    if (view->confirm != NULL || view->closing || view->viewer_closing || view->files.count == 0)
        return;
    view->confirm = lv_obj_create(view->viewer);
    if (view->confirm == NULL)
        return;
    lv_obj_add_event_cb(view->confirm, app_gallery_confirm_delete_cb, LV_EVENT_DELETE, view);
    lv_obj_remove_style_all(view->confirm);
    lv_obj_set_size(view->confirm, 800, 480);
    lv_obj_set_scrollable(view->confirm, false);
    lv_obj_set_gesture_bubble(view->confirm, false);
    lv_obj_set_style_bg_color(view->confirm, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(view->confirm, LV_OPA_60, LV_PART_MAIN);
    panel = lv_obj_create(view->confirm);
    if (panel == NULL)
        goto CONFIRM_ERRO;
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 480, 200);
    lv_obj_center(panel);
    lv_obj_set_scrollable(panel, false);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x17243a), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 12, LV_PART_MAIN);
    view->confirm_note = app_gallery_label(panel, "Delete this image?", 30, 24);
    label = app_gallery_label(panel, view->files.entries[view->selected].name, 30, 66);
    if (view->confirm_note == NULL || label == NULL ||
        app_gallery_button(panel, "CANCEL", 64, 124, app_gallery_cancel_cb, view) == NULL ||
        app_gallery_button(panel, "DELETE", 296, 124, app_gallery_remove_cb, view) == NULL)
        goto CONFIRM_ERRO;
    lv_obj_set_width(view->confirm_note, 420);
    lv_obj_set_size(label, 420, 20);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    return;
CONFIRM_ERRO:
    lv_obj_delete(view->confirm);
}

static void app_gallery_open_image_cb(lv_event_t *event)
{
    struct _gallery_view *view = lv_event_get_user_data(event);
    lv_obj_t *button = lv_event_get_current_target_obj(event);
    if (view->viewer != NULL || view->closing)
        return;
    view->selected = (size_t)(uintptr_t)lv_obj_get_user_data(button);
    if (view->selected >= view->files.count)
        return;
    view->viewer = app_gallery_root(view->root);
    if (view->viewer == NULL)
        return;
    lv_obj_add_event_cb(view->viewer, app_gallery_viewer_delete_cb, LV_EVENT_DELETE, view);
    lv_obj_set_gesture_bubble(view->viewer, false);
    lv_obj_add_event_cb(view->viewer, app_gallery_swipe_cb, LV_EVENT_GESTURE_LEFT, view);
    lv_obj_add_event_cb(view->viewer, app_gallery_swipe_cb, LV_EVENT_GESTURE_RIGHT, view);
    view->image_title = app_gallery_label(view->viewer, "", 72, 48);
    view->image_count = app_gallery_label(view->viewer, "", 350, 442);
    view->image_error = app_gallery_label(view->viewer, "Cannot open image", 0, 0);
    view->image = lv_image_create(view->viewer);
    if (view->image_title == NULL || view->image_count == NULL || view->image_error == NULL ||
        view->image == NULL ||
        app_view_back_button_create(view->viewer, app_gallery_viewer_back_cb, view) == NULL)
    {
        lv_obj_delete(view->viewer);
        return;
    }
    lv_obj_set_size(view->image_title, 680, 20);
    lv_label_set_long_mode(view->image_title, LV_LABEL_LONG_DOT);
    lv_obj_center(view->image_error);
    lv_obj_set_size(view->image, 752, 328);
    lv_obj_set_pos(view->image, 24, 92);
    lv_image_set_inner_align(view->image, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_set_clickable(view->image, false);
    lv_obj_t *trash = app_gallery_button(view->viewer, LV_SYMBOL_TRASH, 732, 428,
                                          app_gallery_trash_cb, view);
    if (trash == NULL)
    {
        lv_obj_delete(view->viewer);
        return;
    }
    lv_obj_set_size(trash, 44, 44);
    lv_obj_set_style_bg_color(trash, lv_color_hex(0x17243a), LV_PART_MAIN);
    app_gallery_show_image(view);
}

static void app_gallery_show_page(struct _gallery_view *view)
{
    size_t pages = (view->files.count + GALLERY_PAGE_SIZE - 1) / GALLERY_PAGE_SIZE;
    lv_anim_delete(view->grid, app_gallery_slide_x);
    lv_obj_set_x(view->grid, 24);
    lv_obj_clean(view->grid);
    app_gallery_thumbnails_free(view);
    lv_label_set_text_fmt(view->page_label, "%zu / %zu", pages ? view->page + 1 : 0, pages);
    if (view->files.count == 0)
    {
        lv_obj_t *label = lv_label_create(view->grid);
        if (label != NULL)
        {
            lv_label_set_text(label, view->list_error ? "Cannot read L:res/" : "No images in L:res/");
            lv_obj_center(label);
        }
        return;
    }
    for (size_t i = 0; i < GALLERY_PAGE_SIZE; i++)
    {
        size_t index = view->page * GALLERY_PAGE_SIZE + i;
        const APP_PHOTO_ENTRY_S *entry;
        lv_obj_t *button, *image, *label;
        lv_image_header_t header;
        char path[APP_PHOTO_PATH_MAX], text[40];
        struct tm local_time;
        if (index >= view->files.count)
            break;
        entry = &view->files.entries[index];
        button = lv_button_create(view->grid);
        if (button == NULL)
            break;
        lv_obj_remove_style_all(button);
        lv_obj_set_size(button, 368, 144);
        lv_obj_set_pos(button, (i % 2) * 384, (i / 2) * 156);
        lv_obj_set_scrollable(button, false);
        lv_obj_set_style_radius(button, 12, LV_PART_MAIN);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x17243a), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_user_data(button, (void *)(uintptr_t)index);
        lv_obj_add_event_cb(button, app_gallery_open_image_cb, LV_EVENT_CLICKED, view);
        snprintf(path, sizeof(path), "L:res/%s", entry->name);
        image = lv_image_create(button);
        snprintf(text, sizeof(text), "Unavailable");
        if (image != NULL)
        {
            lv_obj_set_size(image, 176, 132);
            lv_obj_set_pos(image, 6, 6);
            lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CONTAIN);
            lv_obj_set_clickable(image, false);
            if (app_gallery_set_image(image, path, &view->thumbnails[i], &header) == 0)
                snprintf(text, sizeof(text), "%u x %u", (unsigned)header.w, (unsigned)header.h);
            else
                lv_obj_set_hidden(image, true);
        }
        label = app_gallery_label(button, entry->name, 196, 24);
        if (label != NULL)
        {
            lv_obj_set_size(label, 156, 20);
            lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        }
        app_gallery_label(button, text, 196, 60);
        if (localtime_r(&entry->modified, &local_time) != NULL &&
            strftime(text, sizeof(text), "%m-%d %H:%M", &local_time) > 0)
            app_gallery_label(button, text, 196, 92);
    }
}

static void app_gallery_page_step(struct _gallery_view *view, int direction)
{
    size_t pages = (view->files.count + GALLERY_PAGE_SIZE - 1) / GALLERY_PAGE_SIZE;
    if (view->closing || view->viewer != NULL || pages < 2)
        return;
    if (direction < 0)
    {
        view->page = view->page ? view->page - 1 : pages - 1;
    }
    else
    {
        view->page = (view->page + 1) % pages;
    }
    app_gallery_show_page(view);
    app_gallery_slide(view->grid, 24, direction);
}

lv_obj_t *app_gallery_view_create(lv_obj_t *parent)
{
    struct _gallery_view *view;
    if (parent == NULL)
        return NULL;
    view = calloc(1, sizeof(*view));
    if (view == NULL)
        return NULL;
    view->list_error = app_photo_list(&view->files) < 0;
    if (view->list_error)
        APP_LOG(LOG_ERRO, "gallery list failed: %s\n", strerror(errno));
    view->root = app_gallery_root(parent);
    if (view->root == NULL)
    {
        app_photo_list_free(&view->files);
        free(view);
        return NULL;
    }
    lv_obj_add_event_cb(view->root, app_gallery_delete_cb, LV_EVENT_DELETE, view);
    lv_obj_set_gesture_bubble(view->root, false);
    lv_obj_add_event_cb(view->root, app_gallery_swipe_cb, LV_EVENT_GESTURE_LEFT, view);
    lv_obj_add_event_cb(view->root, app_gallery_swipe_cb, LV_EVENT_GESTURE_RIGHT, view);
    view->grid = lv_obj_create(view->root);
    view->page_label = app_gallery_label(view->root, "", 360, 442);
    if (view->grid == NULL || view->page_label == NULL ||
        app_gallery_label(view->root, "GALLERY", 72, 48) == NULL ||
        app_view_back_button_create(view->root, app_gallery_back_cb, view) == NULL)
    {
        lv_obj_delete(view->root);
        return NULL;
    }
    lv_obj_remove_style_all(view->grid);
    lv_obj_set_size(view->grid, 752, 308);
    lv_obj_set_pos(view->grid, 24, 100);
    lv_obj_set_scrollable(view->grid, false);
    app_gallery_show_page(view);
    return view->root;
}
