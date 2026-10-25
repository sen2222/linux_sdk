#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>

#include "app_config.h"
#include "ov5640.h"




struct _ov5640_ctx{
    int led_fd;
    uint8_t led_state;
};
static struct _ov5640_ctx g_ov5640_ctx = {0};




int ov5640_led_on(void)
{
    if(g_ov5640_ctx.led_fd < 0)
    {
        APP_LOG(LOG_WARN, "led fd is invalid\n");
        return -1;
    }

    int ret = write(g_ov5640_ctx.led_fd, "1", 1);
    if(ret < 0)
    {
        APP_LOG(LOG_WARN, "write led fd failed\n");
        return -1;
    }
    APP_LOG(LOG_INFO, "ov5640 led on\n");
    return 0;
}
int ov5640_led_off(void)
{
    if(g_ov5640_ctx.led_fd < 0)
    {
        APP_LOG(LOG_WARN, "led fd is invalid\n");
        return -1;
    }

    int ret = write(g_ov5640_ctx.led_fd, "0", 1);
    if(ret < 0)
    {
        APP_LOG(LOG_WARN, "write led fd failed\n");
        return -1;
    }
    APP_LOG(LOG_INFO, "ov5640 led off\n");
    return 0;
}
int ov5640_led_toggle(void)
{
    if(g_ov5640_ctx.led_fd < 0)
    {
        APP_LOG(LOG_WARN, "led fd is invalid\n");
        return -1;
    }
    int ret = 0;
    int new_state = !g_ov5640_ctx.led_state;
    ret = write(g_ov5640_ctx.led_fd, new_state ? "1" : "0", 1);
    if(ret < 0)
    {   
        APP_LOG(LOG_WARN, "write led fd failed\n");
        return -1;
    }
    g_ov5640_ctx.led_state = new_state;
    APP_LOG(LOG_INFO, "ov5640 led %s\n", new_state ? "on" : "off");
    return 0;
}



int ov5640_init(void)
{
    g_ov5640_ctx.led_fd = open(OV5640_LED_DEVICE_PATH, O_WRONLY);
    if(g_ov5640_ctx.led_fd < 0)
    {
        APP_LOG(LOG_WARN, "open %s failed\n", OV5640_LED_DEVICE_PATH);
    }

    return 0;
}
