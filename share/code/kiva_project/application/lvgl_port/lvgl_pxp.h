#ifndef KIVA_LVGL_PXP_H
#define KIVA_LVGL_PXP_H

#include <stddef.h>
#include <stdint.h>

typedef struct _kiva_pxp KIVA_PXP_S;

/* frame_bytes 为一张画布的字节数；内部申请两张画布，失败返回 NULL。 */
KIVA_PXP_S *kiva_pxp_create(size_t frame_bytes);
/* 获取第 0/1 张画布的虚拟地址，所有权仍属于 pxp，调用方不能自行释放。 */
void *kiva_pxp_buffer(KIVA_PXP_S *pxp, unsigned index);
/*
 * 同步搬运整帧，同一实例必须串行调用。
 * source：上述画布虚拟地址；destination：后台显存物理地址；destination_stride：显存每行字节数。
 * 返回 0：DMA 已完成，可以翻页。
 * 返回 -1：尚未成功提交 DMA，可以改用 CPU 拷贝。
 * 返回 -2：任务已入队或完成状态不明，禁止立即 CPU 回退，应停止刷新并处理错误。
 */
int kiva_pxp_copy(KIVA_PXP_S *pxp, const void *source, uint64_t destination,
                  unsigned width, unsigned height, unsigned destination_stride);
/* 先确认在途任务完成，再释放画布/通道；仍无法确认时保留资源并打印错误。 */
void kiva_pxp_destroy(KIVA_PXP_S *pxp);

#endif
