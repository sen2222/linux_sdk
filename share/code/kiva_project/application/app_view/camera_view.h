#ifndef CAMERA_VIEW_H_
#define CAMERA_VIEW_H_

#include "lvgl/lvgl.h"

/* 在LVGL锁内调用；640x480预览和160x480设置栏，删除时自动停止采集。 */
lv_obj_t *app_camera_view_create(lv_obj_t *parent);

#endif
