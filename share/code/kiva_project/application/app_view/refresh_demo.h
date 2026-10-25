#ifndef KIVA_REFRESH_DEMO_H
#define KIVA_REFRESH_DEMO_H

#include "lvgl/lvgl.h"

/* 在 LVGL 锁内创建；默认持续运行。退出按钮或父页面删除会自动清理定时器和统计回调。 */
lv_obj_t *app_refresh_demo_create(lv_obj_t *parent);

#endif
