#ifndef __SCREEN_FBDEV_H__
#define __SCREEN_FBDEV_H__



#include <stdint.h>

#define KIVA_SCREEN_FBDEV_PATH                          "/dev/fb0"






int screen_fbdev_get_resolution(uint32_t *width, uint32_t *height);
uint8_t *screen_fbdev_get_buffer(uint8_t index);
/* 返回一页显存的字节数和每行跨度，单位均为字节。 */
int screen_fbdev_get_buffer_info(uint32_t *size, uint32_t *stride);
int screen_fbdev_set_show_index(uint8_t index);
int screen_fbdev_init(void);
void screen_fbdev_deinit(void);








#endif
