#ifndef __OV5640_H__
#define __OV5640_H__

#include <stdint.h>
#include <stddef.h>





#define OV5640_LED_DEVICE_PATH "/sys/class/leds/ov5640-flash/brightness"
#define OV5640_CAMERA_WIDTH 640
#define OV5640_CAMERA_HEIGHT 480

typedef struct _ov5640_frame
{
    const uint8_t *data;
    size_t size;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    unsigned index;
} OV5640_FRAME_S;




int ov5640_led_on(void);
int ov5640_led_off(void);
int ov5640_led_toggle(void);
int ov5640_led_get_state(void);
int ov5640_init(void);

/* 以下采集接口由同一个线程顺序调用，不包含LVGL。 */
int ov5640_camera_start(unsigned fps);
/* 返回1表示有帧，0表示超时或丢弃损坏帧，-1表示错误。 */
int ov5640_camera_read(OV5640_FRAME_S *frame, int timeout_ms);
/* frame.data属于驱动；处理完后必须归还，归还后不能再访问。 */
int ov5640_camera_release(const OV5640_FRAME_S *frame);
void ov5640_camera_stop(void);








#endif
