#ifndef VIEW_CLASS_H_
#define VIEW_CLASS_H_


#include "lvgl/lvgl.h"

typedef struct _app_view_info APP_VIEW_INFO_S;


struct _app_view_info
{
    int (*view_init)(APP_VIEW_INFO_S *view_data);
    void (*view_deinit)(APP_VIEW_INFO_S *view_data);

    void        *view_data;
    lv_obj_t    *view_screen;
};








int app_switch_new_view(APP_VIEW_INFO_S *view_info);
int app_back_old_view(void);
int app_view_init(void);



#endif
