#ifndef KIVA_TOUCH_INPUT_H
#define KIVA_TOUCH_INPUT_H

#include <stdint.h>


#define LVGL_PORT_TOUCH_PATH                    "/dev/input/event1"
#define TOUCH_INPUT_SLOTS 16

typedef struct {
    int x;
    int y;
    int down;
} TOUCH_CONTACT_S;

typedef struct {
    TOUCH_CONTACT_S contacts[TOUCH_INPUT_SLOTS];
    int slot;
    int primary;
    int multitouch;
    int dropped;
    int width;
    int height;
    int min_x;
    int max_x;
    int min_y;
    int max_y;
    int x;
    int y;
    int pressed;
} TOUCH_INPUT_S;


int lvgl_port_touch_init(TOUCH_INPUT_S *touch, int width, int height);
int touch_input_read(TOUCH_INPUT_S *touch, int fd);

#endif
