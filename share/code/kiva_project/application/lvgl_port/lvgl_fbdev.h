#ifndef KIVA_LVGL_FBDEV_H
#define KIVA_LVGL_FBDEV_H

#include "lvgl/lvgl.h"

/*
 * 创建 RGB565 显示提交器，需要先调用 lv_init。当前在启动 LVGL 线程前创建；
 * 如果线程已经运行，调用时需持有同一把 LVGL 互斥锁。
 * device 通常为 "/dev/fb0"。内部申请两个整屏画布，优先用 PxP DMA 搬到后台显存后翻页。
 * 返回显示对象表示初始化成功；DMA 不可用时可能使用 CPU 拷贝，实际模式会打印日志。
 * 返回 NULL 表示不可用或显式选择了 legacy，由调用方切换到原版 LVGL fbdev。
 * 不需要单独的销毁接口，lv_display_delete/lv_deinit 会触发内部资源回收。
 */
lv_display_t *kiva_fbdev_create(const char *device);

#endif
