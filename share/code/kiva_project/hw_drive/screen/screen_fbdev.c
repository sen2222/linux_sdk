#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <errno.h>
#include "app_config.h"
#include "screen_fbdev.h"



struct screen_fbdev_s
{
    int screen_fd;
    struct fb_fix_screeninfo finfo;
    struct fb_var_screeninfo vinfo;
    struct fb_var_screeninfo original_vinfo;
    int mode_changed;
    int pan_failed;

    uint8_t show_buffer_index;
    uint8_t *framebuffer;
    size_t framebuffer_size;
};

static struct screen_fbdev_s screen_fbdev = {.screen_fd = -1, .framebuffer = MAP_FAILED};



static int get_screen_info(int fd)
{
    if (fd < 0)
    {
        APP_LOG(LOG_ERRO, "Invalid framebuffer file descriptor\n");
        errno = EBADF;
        return -1;
    }
    int ret;
    ret = ioctl(fd, FBIOGET_FSCREENINFO, &screen_fbdev.finfo);
    if(ret < 0)
    {
        APP_LOG(LOG_ERRO, "ioctl FBIOGET_FSCREENINFO failed: %s\n", strerror(errno));
        return -1;
    }
    ret = ioctl(fd, FBIOGET_VSCREENINFO, &screen_fbdev.vinfo);
    if(ret < 0)
    {
        APP_LOG(LOG_ERRO, "ioctl FBIOGET_VSCREENINFO failed: %s\n", strerror(errno));
        return -1;
    }
    APP_LOG(LOG_INFO, "========get screen info success========\n");
    APP_LOG(LOG_INFO, "screen id: %.16s\n", screen_fbdev.finfo.id);
    APP_LOG(LOG_INFO, "screen line length: %u\n", screen_fbdev.finfo.line_length);
    APP_LOG(LOG_INFO, "screen resolution: %u x %u\n", screen_fbdev.vinfo.xres, screen_fbdev.vinfo.yres);                                // 显示分辨率
    APP_LOG(LOG_INFO, "screen virtual resolution: %u x %u\n", screen_fbdev.vinfo.xres_virtual, screen_fbdev.vinfo.yres_virtual);        // 虚拟分辨率
    APP_LOG(LOG_INFO, "screen smem_len: %u\n", screen_fbdev.finfo.smem_len);
    APP_LOG(LOG_INFO, "screen bits per pixel: %u\n", screen_fbdev.vinfo.bits_per_pixel);
    APP_LOG(LOG_INFO, "===========================================\n");

    return 0;
}

/* 将指定显存页切换到屏幕显示，index 只能是0或1。 */
int screen_fbdev_set_show_index(uint8_t index)
{
    int ret;
    struct fb_var_screeninfo pan;
    if (index > 1)
    {
        return -1;
    }
    if (screen_fbdev.screen_fd < 0 || screen_fbdev.framebuffer == MAP_FAILED || screen_fbdev.pan_failed)
    {
        return -1;
    }
    pan = screen_fbdev.vinfo;
    pan.xoffset = 0;
    pan.yoffset = pan.yres * index;
    pan.activate = FB_ACTIVATE_VBL;
    ret = ioctl(screen_fbdev.screen_fd, FBIOPAN_DISPLAY, &pan);
    if (ret < 0)
    {
        screen_fbdev.pan_failed = 1;
        APP_LOG(LOG_ERRO, "Failed to pan display: %s\n", strerror(errno));
        return -1;
    }
    screen_fbdev.vinfo = pan;
    screen_fbdev.show_buffer_index = index;
    return 0;
}

int screen_fbdev_get_resolution(uint32_t *width, uint32_t *height)
{
    if (width == NULL || height == NULL)
    {
        return -1;
    }
    if (screen_fbdev.screen_fd < 0 || screen_fbdev.framebuffer == MAP_FAILED || screen_fbdev.pan_failed)
    {
        return -1;
    }
    *width = screen_fbdev.vinfo.xres;
    *height = screen_fbdev.vinfo.yres;
    return 0;
}

/* 初始化成功后，取得第0页或第1页显存的起始地址。 */
uint8_t *screen_fbdev_get_buffer(uint8_t index)
{
    if (index > 1)
    {
        return NULL;
    }
    if (screen_fbdev.screen_fd < 0 || screen_fbdev.framebuffer == MAP_FAILED || screen_fbdev.pan_failed)
    {
        return NULL;
    }
    return screen_fbdev.framebuffer + index * (screen_fbdev.framebuffer_size / 2);
}

int screen_fbdev_get_buffer_info(uint32_t *size, uint32_t *stride)
{
    if (size == NULL || stride == NULL)
    {
        return -1;
    }
    if (screen_fbdev.screen_fd < 0 || screen_fbdev.framebuffer == MAP_FAILED || screen_fbdev.pan_failed)
    {
        return -1;
    }
    *size = screen_fbdev.framebuffer_size / 2;
    *stride = screen_fbdev.finfo.line_length;
    return 0;
}


int screen_fbdev_init(void)
{
    int ret;
    int saved_errno;
    uint64_t page_size;
    if (screen_fbdev.screen_fd >= 0)
    {
        return screen_fbdev.pan_failed ? -1 : 0;
    }
    screen_fbdev.screen_fd = open(KIVA_SCREEN_FBDEV_PATH, O_RDWR | O_CLOEXEC);
    if (screen_fbdev.screen_fd < 0)
    {
        APP_LOG(LOG_ERRO, "Failed to open framebuffer device: %s\n", strerror(errno));
        return -1;
    }
    if (get_screen_info(screen_fbdev.screen_fd) < 0)
        goto SCREEN_FBDEV_INIT_ERRO;
    screen_fbdev.original_vinfo = screen_fbdev.vinfo;
    if (screen_fbdev.vinfo.yres == 0 || screen_fbdev.vinfo.yres > UINT32_MAX / 2)
    {
        errno = EINVAL;
        goto SCREEN_FBDEV_INIT_ERRO;
    }
    if (screen_fbdev.vinfo.yres_virtual < screen_fbdev.vinfo.yres * 2)
    {
        screen_fbdev.vinfo.yres_virtual = screen_fbdev.vinfo.yres * 2;
        screen_fbdev.vinfo.xoffset = 0;
        screen_fbdev.vinfo.yoffset = 0;
        screen_fbdev.vinfo.activate = FB_ACTIVATE_NOW;
        ret = ioctl(screen_fbdev.screen_fd, FBIOPUT_VSCREENINFO, &screen_fbdev.vinfo);     // 设置双缓冲
        if(ret < 0)
        {
            APP_LOG(LOG_ERRO, "set double buffer failed: %s\n", strerror(errno));
            goto SCREEN_FBDEV_INIT_ERRO;
        }
        screen_fbdev.mode_changed = 1;
    }
    /* 当前屏幕使用标准RGB565；确认两页显存和行跨度都有效后才交给上层。 */
    page_size = (uint64_t)screen_fbdev.finfo.line_length * screen_fbdev.vinfo.yres;
    if (screen_fbdev.vinfo.xres == 0 || screen_fbdev.vinfo.yres == 0 ||
        screen_fbdev.vinfo.xres > INT32_MAX || screen_fbdev.vinfo.yres > INT32_MAX ||
        screen_fbdev.vinfo.xres != screen_fbdev.original_vinfo.xres ||
        screen_fbdev.vinfo.yres != screen_fbdev.original_vinfo.yres ||
        screen_fbdev.vinfo.yres_virtual < screen_fbdev.vinfo.yres * 2 ||
        screen_fbdev.vinfo.bits_per_pixel != 16 || screen_fbdev.vinfo.grayscale || screen_fbdev.vinfo.nonstd ||
        screen_fbdev.vinfo.red.offset != 11 || screen_fbdev.vinfo.red.length != 5 || screen_fbdev.vinfo.red.msb_right ||
        screen_fbdev.vinfo.green.offset != 5 || screen_fbdev.vinfo.green.length != 6 || screen_fbdev.vinfo.green.msb_right ||
        screen_fbdev.vinfo.blue.offset != 0 || screen_fbdev.vinfo.blue.length != 5 || screen_fbdev.vinfo.blue.msb_right ||
        (uint64_t)screen_fbdev.finfo.line_length < (uint64_t)screen_fbdev.vinfo.xres * 2 ||
        screen_fbdev.finfo.line_length % 2 || page_size == 0 ||
        page_size > screen_fbdev.finfo.smem_len / 2 || page_size > SIZE_MAX / 2 ||
        screen_fbdev.finfo.ypanstep == 0 || screen_fbdev.vinfo.yres % screen_fbdev.finfo.ypanstep)
    {
        errno = EINVAL;
        APP_LOG(LOG_ERRO, "Invalid RGB565 double-buffer layout\n");
        goto SCREEN_FBDEV_INIT_ERRO;
    }
    screen_fbdev.framebuffer_size = (size_t)page_size * 2;
    screen_fbdev.framebuffer = mmap(NULL, screen_fbdev.framebuffer_size, PROT_READ | PROT_WRITE, MAP_SHARED, screen_fbdev.screen_fd, 0);
    if (screen_fbdev.framebuffer == MAP_FAILED)
    {
        APP_LOG(LOG_ERRO, "Failed to map framebuffer memory: %s\n", strerror(errno));
        goto SCREEN_FBDEV_INIT_ERRO;
    }

    /** 设置显示0区域 */
    APP_LOG(LOG_INFO, "screen double buffer: pages=2 page_bytes=%zu yoffset=%u, selecting page 0\n",
            screen_fbdev.framebuffer_size / 2, screen_fbdev.vinfo.yoffset);
    if (screen_fbdev_set_show_index(0) < 0)
        goto SCREEN_FBDEV_INIT_ERRO;
    APP_LOG(LOG_INFO, "screen init success\n");
    return 0;
SCREEN_FBDEV_INIT_ERRO:
    saved_errno = errno;
    screen_fbdev_deinit();
    errno = saved_errno;
    return -1;
}

void screen_fbdev_deinit(void)
{
    if (screen_fbdev.screen_fd >= 0 && screen_fbdev.original_vinfo.yres != 0)
    {
        if (screen_fbdev.mode_changed)
        {
            if (ioctl(screen_fbdev.screen_fd, FBIOPUT_VSCREENINFO, &screen_fbdev.original_vinfo) < 0)
                APP_LOG(LOG_ERRO, "Failed to restore framebuffer mode: %s\n", strerror(errno));
        }
        else if (screen_fbdev.framebuffer != MAP_FAILED)
        {
            if (ioctl(screen_fbdev.screen_fd, FBIOPAN_DISPLAY, &screen_fbdev.original_vinfo) < 0)
                APP_LOG(LOG_ERRO, "Failed to restore framebuffer page: %s\n", strerror(errno));
        }
    }
    if (screen_fbdev.framebuffer != MAP_FAILED){munmap(screen_fbdev.framebuffer, screen_fbdev.framebuffer_size);}
    if(screen_fbdev.screen_fd >= 0){close(screen_fbdev.screen_fd);}
    memset(&screen_fbdev, 0, sizeof(screen_fbdev));
    screen_fbdev.screen_fd = -1;
    screen_fbdev.framebuffer = MAP_FAILED;
}



