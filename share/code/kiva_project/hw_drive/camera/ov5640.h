#ifndef __OV5640_H__
#define __OV5640_H__






#define OV5640_LED_DEVICE_PATH "/sys/class/leds/ov5640-flash/brightness"




int ov5640_led_on(void);
int ov5640_led_off(void);
int ov5640_led_toggle(void);
int ov5640_init(void);








#endif
