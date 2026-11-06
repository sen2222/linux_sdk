#include "photo_store.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/* LVGL bundles this pure LodePNG encoder, but does not install its private
 * header. Keep this declaration identical to lodepng.c's public C function.
 * LV_USE_LODEPNG must be enabled and LV_USE_STDLIB_MALLOC must be CLIB:
 * its encoder allocations can then be freed with free() in this worker.
 * No LVGL widgets, global state, image decoder or filesystem API is used. */
extern unsigned lodepng_encode24(unsigned char **out, size_t *outsize,
                               const unsigned char *image,
                               unsigned width, unsigned height);

#define PHOTO_MAX_DIMENSION 8192U
#define PHOTO_MAX_PIXELS (8U * 1024U * 1024U)
#define PHOTO_MAX_SOURCE_BYTES (64U * 1024U * 1024U)

static int photo_write_all(int fd, const unsigned char *data, size_t size)
{
    while (size != 0) {
        ssize_t written = write(fd, data, size);
        if (written < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (written == 0) {
            errno = EIO;
            return -1;
        }
        data += written;
        size -= (size_t)written;
    }
    return 0;
}

int app_photo_save_rgb565(const uint8_t *data, uint32_t width, uint32_t height,
                         uint32_t stride, char *saved_path, size_t path_size)
{
    unsigned char *rgb = NULL, *png = NULL;
    size_t png_size = 0;
    uint64_t pixels, source_size;
    char timestamp[32], temporary[] = "res/.photo_XXXXXX";
    char final_path[APP_PHOTO_PATH_MAX], logical[APP_PHOTO_PATH_MAX];
    struct tm local;
    time_t now;
    int fd = -1, temporary_exists = 0, result = -1, error;
    unsigned encode_error, attempt;

    if (saved_path && path_size) saved_path[0] = '\0';
    if (!data || !saved_path || path_size == 0 || width == 0 || height == 0 ||
        width > PHOTO_MAX_DIMENSION || height > PHOTO_MAX_DIMENSION ||
        stride < (uint64_t)width * 2) {
        errno = EINVAL;
        return -1;
    }
    pixels = (uint64_t)width * height;
    source_size = (uint64_t)(height - 1) * stride + (uint64_t)width * 2;
    if (pixels > PHOTO_MAX_PIXELS || pixels > SIZE_MAX / 3 ||
        source_size > PHOTO_MAX_SOURCE_BYTES || source_size > SIZE_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    now = time(NULL);
    if (now == (time_t)-1 || !localtime_r(&now, &local) ||
        strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &local) == 0) {
        errno = EOVERFLOW;
        return -1;
    }
    /* Reject a short caller buffer before allocating or creating anything. */
    if ((size_t)snprintf(logical, sizeof(logical), "L:res/IMG_%s_XXXXXX.png",
                         timestamp) >= path_size) {
        errno = ENAMETOOLONG;
        return -1;
    }

    rgb = malloc((size_t)pixels * 3);
    if (!rgb) return -1;
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t *row = data + (size_t)y * stride;
        unsigned char *dest = rgb + (size_t)y * width * 3;
        for (uint32_t x = 0; x < width; ++x) {
            unsigned pixel = (unsigned)row[x * 2] | ((unsigned)row[x * 2 + 1] << 8);
            unsigned red = (pixel >> 11) & 31, green = (pixel >> 5) & 63;
            unsigned blue = pixel & 31;
            dest[x * 3] = (unsigned char)((red << 3) | (red >> 2));
            dest[x * 3 + 1] = (unsigned char)((green << 2) | (green >> 4));
            dest[x * 3 + 2] = (unsigned char)((blue << 3) | (blue >> 2));
        }
    }
    encode_error = lodepng_encode24(&png, &png_size, rgb, width, height);
    free(rgb);
    rgb = NULL;
    if (encode_error) {
        errno = encode_error == 83 ? ENOMEM : EIO;
        goto done;
    }
    if (mkdir("res", 0755) < 0 && errno != EEXIST) goto done;
    fd = mkstemp(temporary);
    if (fd < 0) goto done;
    temporary_exists = 1;
    if (photo_write_all(fd, png, png_size) < 0) goto done;
    while (fsync(fd) < 0) {
        if (errno != EINTR) goto done;
    }
    if (close(fd) < 0) {
        fd = -1;
        goto done;
    }
    fd = -1;

    for (attempt = 0; attempt < 1000; ++attempt) {
        if (attempt == 0)
            snprintf(final_path, sizeof(final_path), "res/IMG_%s_%s.png",
                     timestamp, temporary + strlen("res/.photo_"));
        else
            snprintf(final_path, sizeof(final_path), "res/IMG_%s_%s_%u.png",
                     timestamp, temporary + strlen("res/.photo_"), attempt);
        if ((size_t)snprintf(logical, sizeof(logical), "L:%s", final_path) >= path_size) {
            errno = ENAMETOOLONG;
            goto done;
        }
        /* A hard link publishes the fully written inode atomically and fails
         * with EEXIST rather than replacing any existing photo. */
        if (link(temporary, final_path) == 0) break;
        if (errno != EEXIST) goto done;
    }
    if (attempt == 1000) {
        errno = EEXIST;
        goto done;
    }
    if (unlink(temporary) < 0) {
        error = errno;
        unlink(final_path);
        errno = error;
        goto done;
    }
    temporary_exists = 0;
    memcpy(saved_path, logical, strlen(logical) + 1);
    result = 0;

done:
    error = errno;
    if (fd >= 0) close(fd);
    if (temporary_exists) unlink(temporary);
    free(rgb);
    free(png);
    errno = error;
    return result;
}

static int photo_extension(const char *name)
{
    const char *extension = strrchr(name, '.');
    return extension && (strcasecmp(extension, ".png") == 0 ||
                         strcasecmp(extension, ".jpg") == 0 ||
                         strcasecmp(extension, ".jpeg") == 0);
}

static int photo_compare(const void *left, const void *right)
{
    const APP_PHOTO_ENTRY_S *a = left, *b = right;
    if (a->modified != b->modified) return a->modified > b->modified ? -1 : 1;
    return strcmp(a->name, b->name);
}

int app_photo_list(APP_PHOTO_LIST_S *list)
{
    DIR *directory;
    struct dirent *entry;
    size_t capacity = 0;
    int error = 0;

    if (!list) {
        errno = EINVAL;
        return -1;
    }
    list->entries = NULL;
    list->count = 0;
    directory = opendir("res");
    if (!directory) return errno == ENOENT ? 0 : -1;
    for (;;) {
        struct stat info;
        char path[APP_PHOTO_PATH_MAX];
        size_t name_size;
        errno = 0;
        entry = readdir(directory);
        if (!entry) {
            error = errno;
            break;
        }
        if (entry->d_name[0] == '.' || !photo_extension(entry->d_name)) continue;
        name_size = strlen(entry->d_name);
        if (name_size >= sizeof(list->entries[0].name)) continue;
        snprintf(path, sizeof(path), "res/%s", entry->d_name);
        if (lstat(path, &info) < 0) {
            if (errno == ENOENT) continue; /* Removed during enumeration. */
            error = errno;
            break;
        }
        if (!S_ISREG(info.st_mode)) continue;
        if (list->count == capacity) {
            APP_PHOTO_ENTRY_S *entries;
            size_t next = capacity ? capacity * 2 : 16;
            if (next < capacity || next > SIZE_MAX / sizeof(*entries)) {
                error = EOVERFLOW;
                break;
            }
            entries = realloc(list->entries, next * sizeof(*entries));
            if (!entries) {
                error = ENOMEM;
                break;
            }
            list->entries = entries;
            capacity = next;
        }
        memcpy(list->entries[list->count].name, entry->d_name, name_size + 1);
        list->entries[list->count].modified = info.st_mtime;
        ++list->count;
    }
    if (closedir(directory) < 0 && !error) error = errno;
    if (error) {
        app_photo_list_free(list);
        errno = error;
        return -1;
    }
    if (list->count > 1)
        qsort(list->entries, list->count, sizeof(*list->entries), photo_compare);
    return 0;
}

void app_photo_list_free(APP_PHOTO_LIST_S *list)
{
    if (!list) return;
    free(list->entries);
    list->entries = NULL;
    list->count = 0;
}

int app_photo_delete(const char *name)
{
    struct stat info;
    int directory, ret, error;
    if (name == NULL || name[0] == '.' || name[0] == '\0' ||
        strchr(name, '/') != NULL || strchr(name, '\\') != NULL ||
        strlen(name) >= sizeof(((APP_PHOTO_ENTRY_S *)0)->name) || !photo_extension(name)) {
        errno = EINVAL;
        return -1;
    }
    directory = open("res", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory < 0) return errno == ENOENT ? 0 : -1;
    ret = fstatat(directory, name, &info, AT_SYMLINK_NOFOLLOW);
    if (ret == 0 && !S_ISREG(info.st_mode)) {
        errno = EINVAL;
        ret = -1;
    }
    else if (ret == 0)
        ret = unlinkat(directory, name, 0);
    error = errno;
    close(directory);
    if (ret < 0 && error == ENOENT) return 0;
    errno = error;
    return ret;
}
