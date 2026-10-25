/*
 * PxP DMA 操作层，调用本 SDK Linux 4.1.15 的 /dev/pxp_device 接口。
 * i.MX6ULL 对应内核 CONFIG_MXC_PXP_V3 驱动中的 V3P 分支。
 *
 * 这里不绘制控件，只负责：申请两张 DMA 可访问的画布、清理 CPU cache、
 * 配置 RGB565 整帧搬运、等待硬件完成。显示到哪一页由 lvgl_fbdev.c 决定。
 *
 * 同一张画布有两个地址：
 *   buffer[i]           是 CPU/LVGL 使用的虚拟地址。
 *   memory[i].phys_addr 是 PxP 使用的物理地址，不能用普通 malloc 指针代替。
 */
#define _LARGEFILE64_SOURCE
#include <sys/ioctl.h>
#include "pxp_uapi/pxp_device.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "lvgl_pxp.h"
#include "app_config.h"

struct _kiva_pxp
{
    int fd;                            /* /dev/pxp_device 文件描述符 */
    unsigned channel;                  /* 驱动返回的通道句柄，0 也是合法句柄 */
    int channel_owned;                 /* 是否已成功申请通道，不能用 channel != 0 判断 */
    int queued;                        /* CONFIG 已提交任务，完成确认前一直保持为 1 */
    int started;                       /* START 已成功，硬件任务已发出 */
    unsigned allocated;                /* 已申请的画布数量，初始化中途失败时用于回收 */
    struct pxp_mem_desc memory[2];      /* 内核分配的物理内存描述：句柄、大小、物理地址 */
    void *buffer[2];                    /* 上述两块物理内存映射到用户进程后的地址 */
};

/*
 * 清理前必须先确认队列中的任务结束。
 * 本 BSP 的释放通道操作不能保证立即停止硬件，所以不能直接释放 DMA 仍可能访问的内存。
 */
void kiva_pxp_destroy(KIVA_PXP_S *pxp)
{
    unsigned i;
    if (pxp == NULL)
        return;
    if (pxp->queued)
    {
        struct pxp_chan_handle complete = {0};
        complete.handle = pxp->channel;
        /* CONFIG 成功但 START 失败时，先尝试启动队列，否则直接 WAIT 可能永远等不到完成。 */
        if (!pxp->started && ioctl(pxp->fd, PXP_IOC_START_CHAN, &pxp->channel) < 0)
        {
            APP_LOG(LOG_ERRO, "cannot drain queued PxP DMA; keeping its memory, restart board\n");
            return;
        }
        while (ioctl(pxp->fd, PXP_IOC_WAIT4CMPLT, &complete) < 0)
        {
            if (errno == EINTR)
                continue;
            /* 完成状态仍不明确时保留通道、映射和内存，避免硬件访问已经释放的页面。 */
            APP_LOG(LOG_ERRO, "cannot confirm PxP DMA completion; keeping its memory, restart board\n");
            return;
        }
    }
    /* 正常结束后依次释放通道、用户映射、内核物理内存和文件描述符。 */
    if (pxp->channel_owned)
        ioctl(pxp->fd, PXP_IOC_PUT_CHAN, &pxp->channel);
    for (i = 0; i < pxp->allocated; i++)
    {
        if (pxp->buffer[i] != MAP_FAILED)
            munmap(pxp->buffer[i], pxp->memory[i].size);
        ioctl(pxp->fd, PXP_IOC_PUT_PHYMEM, &pxp->memory[i]);
    }
    if (pxp->fd >= 0)
        close(pxp->fd);
    free(pxp);
}

/* 申请一个 PxP 通道和两张画布；所有资源由返回的对象统一管理。 */
KIVA_PXP_S *kiva_pxp_create(size_t frame_bytes)
{
    KIVA_PXP_S *pxp;
    unsigned i;
    int saved_errno;
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0 || frame_bytes == 0 || frame_bytes > UINT32_MAX - (unsigned long)page_size + 1)
        return NULL;
    /* mmap 以页为单位：例如 768000 字节按 4 KiB 页向上取整为 770048 字节。 */
    frame_bytes = ((frame_bytes + page_size - 1) / page_size) * page_size;
    pxp = calloc(1, sizeof(*pxp));
    if (pxp == NULL)
        return NULL;
    /* 该节点需要匹配的 PxP 内核驱动成功 probe 后才会出现。 */
    pxp->fd = open("/dev/pxp_device", O_RDWR | O_CLOEXEC);
    if (pxp->fd < 0)
        goto failed;
    if (ioctl(pxp->fd, PXP_IOC_GET_CHAN, &pxp->channel) < 0)
        goto failed;
    pxp->channel_owned = 1;
    for (i = 0; i < 2; i++)
    {
        pxp->memory[i].size = frame_bytes;
        /* 可缓存映射让 CPU 软件绘制更快，但交给 DMA 前必须执行 CACHE_CLEAN。 */
        pxp->memory[i].mtype = MEMORY_TYPE_CACHED;
        if (ioctl(pxp->fd, PXP_IOC_GET_PHYMEM, &pxp->memory[i]) < 0)
            goto failed;
        pxp->allocated++;
        pxp->buffer[i] = MAP_FAILED;
        /* 驱动用物理地址作为映射偏移；使用 64 位偏移避免高于 2 GiB 的地址变为负数。 */
        pxp->buffer[i] = mmap64(NULL, frame_bytes, PROT_READ | PROT_WRITE, MAP_SHARED,
                                pxp->fd, (off64_t)pxp->memory[i].phys_addr);
        if (pxp->buffer[i] == MAP_FAILED)
            goto failed;
        memset(pxp->buffer[i], 0, frame_bytes);
    }
    return pxp;
failed:
    /* 无论失败在第几个申请步骤，destroy 都只回收已经拿到的资源。 */
    saved_errno = errno;
    kiva_pxp_destroy(pxp);
    errno = saved_errno;
    return NULL;
}

/* 只借出虚拟地址给 LVGL，调用方不能自行 free/munmap；对象销毁时统一回收。 */
void *kiva_pxp_buffer(KIVA_PXP_S *pxp, unsigned index)
{
    return pxp != NULL && index < 2 ? pxp->buffer[index] : NULL;
}

/*
 * 同步拷贝一帧：source 是画布虚拟地址，destination 是后台显存物理地址。
 * destination_stride 的单位是字节；传给 PxP 的 stride 字段则以像素为单位。
 * 本接口只接受本对象管理的两张画布，不能把任意用户内存直接交给 DMA。
 */
int kiva_pxp_copy(KIVA_PXP_S *pxp, const void *source, uint64_t destination,
                  unsigned width, unsigned height, unsigned destination_stride)
{
    struct pxp_config_data config;
    struct pxp_mem_flush clean;
    struct pxp_chan_handle complete;
    unsigned index;
    /* 上次任务没有确认结束时，禁止再提交，避免两次任务读写同一块缓冲。 */
    if (pxp != NULL && pxp->queued)
    {
        errno = EBUSY;
        return -2;
    }
    if (pxp == NULL || width == 0 || height == 0 || width > UINT16_MAX || height > UINT16_MAX ||
        destination_stride % 2 != 0 || destination_stride / 2 > UINT16_MAX ||
        destination_stride < width * 2 || destination > UINT32_MAX ||
        destination + (uint64_t)destination_stride * height > UINT32_MAX)
    {
        errno = EINVAL;
        return -1;
    }
    /* 从虚拟地址找到对应物理内存描述，后面由 phys_addr 配置输入地址。 */
    for (index = 0; index < 2 && pxp->buffer[index] != source; index++);
    if (index == 2 || (uint64_t)width * height * 2 > pxp->memory[index].size)
    {
        errno = EINVAL;
        return -1;
    }
    /* CPU 刚画好的像素可能还在 cache 中，先写回内存，PxP 才能读到最新画面。 */
    clean.handle = pxp->memory[index].handle;
    clean.type = CACHE_CLEAN;
    if (ioctl(pxp->fd, PXP_IOC_FLUSH_PHYMEM, &clean) < 0)
        return -1;

    memset(&config, 0, sizeof(config));
    config.handle = pxp->channel;
    /* s0 是输入画布：RGB565、无行尾填充，所以输入 stride 就是屏幕宽度。 */
    config.s0_param.width = width;
    config.s0_param.height = height;
    config.s0_param.stride = width;
    config.s0_param.pixel_fmt = PXP_PIX_FMT_RGB565;
    config.s0_param.paddr = pxp->memory[index].phys_addr;
    /* out 是后台显存：RGB565 每像素 2 字节，因此内核行跨度要除以 2。 */
    config.out_param.width = width;
    config.out_param.height = height;
    config.out_param.stride = destination_stride / 2;
    config.out_param.pixel_fmt = PXP_PIX_FMT_RGB565;
    config.out_param.paddr = destination;
    /* 输入输出矩形大小相同，位置默认是 (0,0)：只做拷贝，不缩放、不旋转。 */
    config.proc_data.srect.width = config.proc_data.drect.width = width;
    config.proc_data.srect.height = config.proc_data.drect.height = height;
    /* LEGACY 是此内核的处理流程名称，V3P 也支持，不代表选择 PxP V2 驱动。 */
    config.proc_data.working_mode = PXP_MODE_LEGACY;
    if (ioctl(pxp->fd, PXP_IOC_CONFIG_CHAN, &config) < 0)
        return -1;
    pxp->queued = 1;
    /* CONFIG 已把任务入队。此后若失败返回 -2，调用方不能立即回退 CPU 覆盖目标。 */
    if (ioctl(pxp->fd, PXP_IOC_START_CHAN, &pxp->channel) < 0)
        return -2;
    pxp->started = 1;
    memset(&complete, 0, sizeof(complete));
    complete.handle = pxp->channel;
    /* 本 BSP 的 WAIT 没有内核超时；EINTR 只是信号打断，要继续等待真实完成。 */
    while (ioctl(pxp->fd, PXP_IOC_WAIT4CMPLT, &complete) < 0)
    {
        if (errno != EINTR)
            return -2;
    }
    /* 只有 WAIT 成功才能清除在途状态，并允许 framebuffer 层进行 VSYNC 翻页。 */
    pxp->queued = pxp->started = 0;
    return 0;
}
