/*
 * 显示提交层：把 LVGL 已画好的图像送到 LCD 正在使用的 framebuffer。
 *
 * 正常双缓冲路径中有两组内存，合计四块整屏缓冲，不能混为一组：
 *   draw[0] / draw[1]：LVGL 的画布，CPU 在这里画控件、文字和图片。
 *   framebuffer 第 0 / 1 页：LCDIF 扫描输出的显存，一页显示，一页待更新。
 *
 * 每帧流程：
 *   LVGL 画完 -> PxP DMA 将画布拷到后台显存 -> 等待 DMA 完成
 *            -> FBIOPAN_DISPLAY 等待 VSYNC 切换显存页 -> 通知 LVGL 刷新完成。
 *
 * PxP 不可用时改用 CPU 拷贝；显存双页可用时仍然保留 VSYNC 翻页。
 * 本文件负责“搬运与呈现”，控件本身仍由 LVGL 软件绘制。
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "app_config.h"
#include "lvgl_fbdev.h"
#include "lvgl_pxp.h"

struct _kiva_fbdev
{
    int fd;                                 /* /dev/fb0 文件描述符 */
    struct fb_fix_screeninfo fixed;          /* 显存物理地址、总容量、每行字节数等 */
    struct fb_var_screeninfo original;       /* 打开设备时的配置，退出时用于恢复 */
    struct fb_var_screeninfo variable;       /* 当前分辨率、虚拟高度、显示偏移 */
    void *framebuffer;                      /* mmap 得到的显存虚拟地址，供 CPU 回退拷贝使用 */
    size_t mapped_bytes;                    /* 实际映射的显存长度，单位：字节 */
    size_t page_bytes;                      /* 一页显存 = 内核行跨度 × 屏幕高度 */
    size_t draw_bytes;                      /* 一张 LVGL 画布 = 画布行跨度 × 屏幕高度 */
    unsigned draw_stride;                   /* LVGL 画布每行占用的字节数，可能与显存不同 */
    unsigned front;                         /* 当前显示的显存页号，正常为 0 或 1 */
    int double_page;                        /* 显存双页和 VSYNC 翻页是否实际可用 */
    int mode_changed;                       /* 是否扩大过虚拟高度，决定退出时如何恢复 */
    int dma_enabled;                        /* 后续帧是否尝试用 PxP 搬运 */
    int stopped;                            /* DMA/翻页状态不明确时停止继续提交 */
    KIVA_PXP_S *pxp;                        /* PxP 通道和两张物理连续画布的管理对象 */
    void *draw[2];                          /* 注册给 LVGL 的两张画布，都是虚拟地址 */
    void *allocation[2];                    /* CPU 模式下的原始分配地址，释放时不能用对齐后的 draw */
    /* 以下计数仅用于 KIVA_FB_STATS 日志，不参与缓冲切换。 */
    unsigned dma_frames;
    int dma_first_logged;
    unsigned cpu_frames;
    uint64_t stats_start;
    uint64_t transfer_ns;
    uint64_t flip_ns;
    int stats_enabled;
};

/* 使用单调时钟：校准系统日期不会影响 LVGL tick 或耗时统计。 */
static uint64_t monotonic_ns(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ULL + now.tv_nsec;
}

static uint32_t tick_get(void)
{
    return monotonic_ns() / 1000000ULL;
}

/* 初始化失败和显示对象删除共用此清理入口；先处理在途 DMA，再释放内存。 */
static void release_fbdev(struct _kiva_fbdev *fb)
{
    unsigned i;
    if (fb == NULL)
        return;
    kiva_pxp_destroy(fb->pxp);
    /* PxP 拥有的画布由上一步释放；CPU 模式才使用 allocation[]。 */
    for (i = 0; i < 2; i++)
        lv_free(fb->allocation[i]);
    if (fb->framebuffer != MAP_FAILED)
        munmap(fb->framebuffer, fb->mapped_bytes);
    if (fb->fd >= 0)
    {
        /* 扩大过虚拟高度则恢复原配置，否则只恢复原来的显示页。 */
        if (fb->mode_changed)
            ioctl(fb->fd, FBIOPUT_VSCREENINFO, &fb->original);
        else if (fb->double_page)
            ioctl(fb->fd, FBIOPAN_DISPLAY, &fb->original);
        close(fb->fd);
    }
    lv_free(fb);
}

/* 与 LVGL 显示对象绑定，lv_display_delete/lv_deinit 时自动回收本驱动资源。 */
static void delete_cb(lv_event_t *event)
{
    lv_display_t *display = lv_event_get_target(event);
    struct _kiva_fbdev *fb = lv_display_get_driver_data(display);
    lv_display_set_driver_data(display, NULL);
    release_fbdev(fb);
}

/* 不确定 DMA 是否结束或哪页正在显示时，继续写显存可能与硬件冲突。 */
static void stop_presenter(lv_display_t *display, struct _kiva_fbdev *fb, const char *reason)
{
    fb->stopped = 1;
    lv_timer_pause(lv_display_get_refr_timer(display));
    APP_LOG(LOG_ERRO, "%s: %s; display refresh paused, check kernel log\n", reason, strerror(errno));
}

/*
 * LVGL 刷新回调。DIRECT 模式的 pixels 指向整张画布起点，不只是 area 的像素。
 * 本实现同步等待搬运和翻页完成，再调用 lv_display_flush_ready()；没有额外的 DMA 工作线程。
 */
static void flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    struct _kiva_fbdev *fb = lv_display_get_driver_data(display);
    unsigned back;
    uint64_t start, transferred;
    int result = -1;
    (void)area;
    /* 一帧可能有多个脏矩形；前几个只确认回调结束，最后一个才提交完整画布。 */
    if (fb->stopped || !lv_display_flush_is_last(display))
    {
        lv_display_flush_ready(display);
        return;
    }
    /* 双页时只写后台页；只有单页可用时才退回直接更新当前页。 */
    back = fb->double_page ? 1 - fb->front : fb->front;
    start = monotonic_ns();
    if (fb->dma_enabled)
    {
        /* DMA 使用物理地址：显存起始物理地址 + 后台页偏移。不能传 mmap 虚拟地址。 */
        result = kiva_pxp_copy(fb->pxp, pixels,
                              (uint64_t)fb->fixed.smem_start + back * fb->page_bytes,
                              fb->variable.xres, fb->variable.yres, fb->fixed.line_length);
        if (result == -2)
        {
            /* -2 表示 DMA 可能仍在运行：不能立即 CPU 回退、翻页或继续复用画布。 */
            stop_presenter(display, fb, "PxP DMA completion failed");
            lv_display_flush_ready(display);
            return;
        }
        if (result < 0)
        {
            APP_LOG(LOG_WARN, "PxP setup/cache clean failed: %s; using CPU copy\n", strerror(errno));
            /* -1 表示尚未成功提交 DMA，本帧及后续帧可以安全改用 CPU。 */
            fb->dma_enabled = 0;
        }
        else
        {
            if (!fb->dma_first_logged)
            {
                APP_LOG(LOG_INFO, "PxP DMA first frame completed\n");
                fb->dma_first_logged = 1;
            }
            fb->dma_frames++;
        }
    }
    if (result < 0)
    {
        unsigned y;
        uint8_t *destination = (uint8_t *)fb->framebuffer + back * fb->page_bytes;
        /* 按行拷贝，分别使用画布和显存的 stride，跳过显存行尾可能存在的填充。 */
        for (y = 0; y < fb->variable.yres; y++)
            memcpy(destination + y * fb->fixed.line_length, pixels + y * fb->draw_stride, fb->variable.xres * 2);
        fb->cpu_frames++;
    }
    transferred = monotonic_ns();
    fb->transfer_ns += transferred - start;
    if (fb->double_page)
    {
        struct fb_var_screeninfo pan = fb->variable;
        /* 两页在显存中上下排列：第 0 页 yoffset=0，第 1 页 yoffset=屏幕高度。 */
        pan.yoffset = back * fb->variable.yres;
        pan.activate = FB_ACTIVATE_VBL;
        /* 本 BSP 的 mxsfb 驱动会等待翻页完成，因此成功返回后才可交换前后台角色。 */
        if (ioctl(fb->fd, FBIOPAN_DISPLAY, &pan) < 0)
        {
            /* 失败时硬件可能已切换，也可能未切换；不能猜测当前显示页。 */
            stop_presenter(display, fb, "framebuffer VSYNC flip failed");
            lv_display_flush_ready(display);
            return;
        }
        fb->front = back;
        fb->variable.yoffset = pan.yoffset;
    }
    fb->flip_ns += monotonic_ns() - transferred;
    /* 统计实际提交帧数，包含静止时间；静态页面的低 FPS 不代表硬件刷新上限低。 */
    if (fb->stats_enabled && monotonic_ns() - fb->stats_start >= 2000000000ULL)
    {
        unsigned frames = fb->dma_frames + fb->cpu_frames;
        double seconds = (monotonic_ns() - fb->stats_start) / 1000000000.0;
        APP_LOG(LOG_INFO, "present fps=%.1f dma=%u cpu=%u transfer=%.2fms flip=%.2fms\n",
                frames / seconds, fb->dma_frames, fb->cpu_frames,
                fb->transfer_ns / (1000000.0 * frames), fb->flip_ns / (1000000.0 * frames));
        fb->stats_start = monotonic_ns();
        fb->dma_frames = fb->cpu_frames = 0;
        fb->transfer_ns = fb->flip_ns = 0;
    }
    /* 到这里搬运与翻页都完成，LVGL 才能放心复用本次提交的画布。 */
    lv_display_flush_ready(display);
}

/* 硬件路径只处理标准 RGB565，其他像素布局交给调用方的原版 fbdev 回退路径。 */
static int rgb565_layout(const struct fb_var_screeninfo *v)
{
    return v->xres > 0 && v->yres > 0 && v->xres <= UINT16_MAX && v->yres <= UINT16_MAX &&
           v->bits_per_pixel == 16 && v->grayscale == 0 && v->nonstd == 0 && v->xoffset == 0 &&
           v->red.offset == 11 && v->red.length == 5 && !v->red.msb_right &&
           v->green.offset == 5 && v->green.length == 6 && !v->green.msb_right &&
           v->blue.offset == 0 && v->blue.length == 5 && !v->blue.msb_right;
}

/* 初始化顺序：查询显存 -> 协商双页 -> 映射显存 -> 分配双画布 -> 注册 LVGL 回调。 */
lv_display_t *kiva_fbdev_create(const char *device)
{
    struct _kiva_fbdev *fb;
    lv_display_t *display;
    const char *mode = getenv("KIVA_FB_MODE");
    const char *stats = getenv("KIVA_FB_STATS");
    unsigned i;
    int saved_errno;
    /* auto/未设置：优先 DMA；cpu：仅 CPU 搬运；legacy：由调用方使用原版驱动。 */
    if (mode != NULL && strcmp(mode, "legacy") == 0)
        return NULL;
    fb = lv_malloc_zeroed(sizeof(*fb));
    if (fb == NULL)
        return NULL;
    fb->framebuffer = MAP_FAILED;
    fb->fd = open(device, O_RDWR | O_CLOEXEC);
    if (fb->fd < 0 || ioctl(fb->fd, FBIOGET_VSCREENINFO, &fb->original) < 0 ||
        ioctl(fb->fd, FBIOGET_FSCREENINFO, &fb->fixed) < 0)
        goto failed;
    /* 保存原配置，初始化失败时也要撤销已经做过的虚拟分辨率调整。 */
    fb->variable = fb->original;
    if (!rgb565_layout(&fb->variable) || fb->fixed.line_length < fb->variable.xres * 2 ||
        fb->fixed.line_length % 2 != 0)
    {
        errno = ENOTSUP;
        goto failed;
    }
    if ((uint64_t)fb->fixed.line_length * fb->variable.yres > SIZE_MAX)
        goto failed;
    /* 这里按内核实际行跨度计算，不能简单假定每行一定是 width * 2。 */
    fb->page_bytes = (size_t)fb->fixed.line_length * fb->variable.yres;
    if (fb->page_bytes == 0 || fb->page_bytes > fb->fixed.smem_len ||
        (fb->variable.yoffset != 0 && fb->variable.yoffset != fb->variable.yres))
    {
        errno = EINVAL;
        goto failed;
    }
    fb->front = fb->variable.yoffset / fb->variable.yres;
    if ((uint64_t)(fb->front + 1) * fb->page_bytes > fb->fixed.smem_len)
        goto failed;
    /* 双页需要足够显存，并且硬件允许按整屏高度进行 Y 方向平移。 */
    fb->double_page = fb->page_bytes <= fb->fixed.smem_len / 2 && fb->fixed.ypanstep != 0 &&
                      fb->variable.yres % fb->fixed.ypanstep == 0;
    if (fb->double_page && fb->variable.yres_virtual < fb->variable.yres * 2)
    {
        /* 可见高度不变，只把虚拟显存布局扩为上下两屏，例如 800×960。 */
        fb->variable.yres_virtual = fb->variable.yres * 2;
        if (ioctl(fb->fd, FBIOPUT_VSCREENINFO, &fb->variable) < 0)
        {
            fb->variable = fb->original;
            fb->double_page = 0;
        }
        else
        {
            fb->mode_changed = 1;
            if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &fb->variable) < 0 ||
                fb->variable.xres != fb->original.xres || fb->variable.yres != fb->original.yres ||
                fb->variable.yres_virtual < fb->variable.yres * 2 || !rgb565_layout(&fb->variable))
                goto failed;
        }
    }
    /* 先翻到当前页，验证 PAN 确实可用；探测失败则保留单显存页模式。 */
    if (fb->double_page && ioctl(fb->fd, FBIOPAN_DISPLAY, &fb->variable) < 0)
        fb->double_page = 0;
    fb->mapped_bytes = fb->page_bytes * ((fb->double_page || fb->front) ? 2 : 1);
    if (fb->mapped_bytes > fb->fixed.smem_len)
        goto failed;
    /* 此映射供 CPU 回退拷贝使用；DMA 目标地址来自 fixed.smem_start。 */
    fb->framebuffer = mmap(NULL, fb->mapped_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);
    if (fb->framebuffer == MAP_FAILED)
        goto failed;
    fb->draw_stride = lv_draw_buf_width_to_stride(fb->variable.xres, LV_COLOR_FORMAT_RGB565);
    if ((uint64_t)fb->draw_stride * fb->variable.yres > UINT32_MAX - LV_DRAW_BUF_ALIGN)
        goto failed;
    fb->draw_bytes = (size_t)fb->draw_stride * fb->variable.yres;
    /* 本适配层的 PxP 输入要求紧密排列的 RGB565，宽高按 8 像素对齐。 */
    if ((mode == NULL || strcmp(mode, "cpu") != 0) && fb->draw_stride == fb->variable.xres * 2 &&
        fb->variable.xres % 8 == 0 && fb->variable.yres % 8 == 0)
    {
        fb->pxp = kiva_pxp_create(fb->draw_bytes);
        if (fb->pxp == NULL)
            APP_LOG(LOG_WARN, "PxP DMA unavailable: %s; using CPU copy\n", strerror(errno));
    }
    fb->dma_enabled = fb->pxp != NULL;
    for (i = 0; i < 2; i++)
    {
        /* DMA 模式使用驱动分配的物理连续内存；CPU 模式使用普通堆内存。 */
        if (fb->pxp != NULL)
            fb->draw[i] = kiva_pxp_buffer(fb->pxp, i);
        else
        {
            fb->allocation[i] = lv_malloc_zeroed(fb->draw_bytes + LV_DRAW_BUF_ALIGN - 1);
            if (fb->allocation[i] == NULL)
                goto failed;
            fb->draw[i] = lv_draw_buf_align(fb->allocation[i], LV_COLOR_FORMAT_RGB565);
        }
    }
    display = lv_display_create(fb->variable.xres, fb->variable.yres);
    if (display == NULL)
        goto failed;
    lv_tick_set_cb(tick_get);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_driver_data(display, fb);
    lv_display_set_flush_cb(display, flush_cb);
    /* DIRECT 只重画脏区域；LVGL 负责同步两张画布的旧内容，使提交时仍是完整画面。 */
    lv_display_set_buffers(display, fb->draw[0], fb->draw[1], fb->draw_bytes, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_add_event_cb(display, delete_cb, LV_EVENT_DELETE, NULL);
    fb->stats_enabled = stats != NULL && strcmp(stats, "0") != 0;
    fb->stats_start = monotonic_ns();
    APP_LOG(LOG_INFO, "fbdev %ux%u RGB565 draw_buffers=2 fb_pages=%d transfer=%s VSYNC_flip=%s\n",
            fb->variable.xres, fb->variable.yres, fb->double_page ? 2 : 1,
            fb->dma_enabled ? "PxP DMA ready" : "CPU copy", fb->double_page ? "on" : "off");
    return display;
failed:
    /* 清理时的 ioctl/close 可能改变 errno，先保存最初失败原因供日志输出。 */
    saved_errno = errno;
    release_fbdev(fb);
    errno = saved_errno;
    APP_LOG(LOG_WARN, "accelerated fbdev unavailable: %s\n", strerror(errno));
    return NULL;
}
