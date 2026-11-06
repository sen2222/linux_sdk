#ifndef APP_PHOTO_STORE_H
#define APP_PHOTO_STORE_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_PHOTO_PATH_MAX 384

typedef struct {
    char name[256];
    time_t modified;
} APP_PHOTO_ENTRY_S;

typedef struct {
    APP_PHOTO_ENTRY_S *entries;
    size_t count;
} APP_PHOTO_LIST_S;

/* Snapshot is little-endian RGB565; stride is bytes between input rows.
 * Caller retains the snapshot until this synchronous worker helper returns.
 * Success returns 0 and a logical L:res/IMG_*.png path; failure sets errno. */
int app_photo_save_rgb565(const uint8_t *data, uint32_t width, uint32_t height,
                         uint32_t stride, char *saved_path, size_t path_size);

/* Returns regular, non-hidden PNG/JPG/JPEG names, newest first. Missing res/
 * is an empty list. Free a successful list before loading another into it. */
int app_photo_list(APP_PHOTO_LIST_S *list);
void app_photo_list_free(APP_PHOTO_LIST_S *list);

/* 仅删除res/下的普通PNG/JPG/JPEG文件；name是列表中的文件名，不接受路径。 */
int app_photo_delete(const char *name);

#ifdef __cplusplus
}
#endif

#endif
