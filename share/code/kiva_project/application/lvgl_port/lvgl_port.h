#ifndef KIVA_LVGL_PORT_H
#define KIVA_LVGL_PORT_H


#include <stdint.h>





void lvgl_port_mutex_lock(void);
void lvgl_port_mutex_unlock(void);
int lvgl_port_init(void);
void lvgl_port_deinit(void);

#endif
