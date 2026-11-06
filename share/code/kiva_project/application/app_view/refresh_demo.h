#ifndef KIVA_REFRESH_DEMO_H
#define KIVA_REFRESH_DEMO_H

#include "lvgl/lvgl.h"

/* 在LVGL锁内创建LCD刷屏及触摸坐标测试；BACK或父页面删除会清理定时器和统计回调。 */
lv_obj_t *app_refresh_demo_create(lv_obj_t *parent);

#endif
