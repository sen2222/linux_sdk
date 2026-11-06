#ifndef VIEW_CLASS_H_
#define VIEW_CLASS_H_


#include "lvgl/lvgl.h"

int app_view_init(void);
lv_obj_t *app_view_back_button_create(lv_obj_t *parent, lv_event_cb_t callback, void *user_data);



#endif
