#ifndef GALLERY_VIEW_H_
#define GALLERY_VIEW_H_

#include "lvgl/lvgl.h"

/* 浏览L:res/下的PNG/JPG图片；缩略图和大图左右滑动切换，垃圾桶确认后删除当前文件。 */
lv_obj_t *app_gallery_view_create(lv_obj_t *parent);

#endif
