#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>


// #define LED_DEV_PATH    "dev/led_chdev"
// #define LED_DEV_PATH    "dev/led_new_chdev"
// #define LED_DEV_PATH    "dev/led_dts_chdev"
// #define LED_DEV_PATH    "/dev/led_gpio_chdev"
#define LED_DEV_PATH    "/dev/led_device"

typedef enum 
{
    LED_OFF = 0,
    LED_ON
}LED_STATE;

int main(int argc, char *argv[])
{
    int fd, ret;
    char data_buf[1];
    fd = open(LED_DEV_PATH, O_RDWR);
    if(fd < 0)
    {
        printf("open failed\n");
        exit(1);
    }
    for(;;)
    {
        // data_buf[0] = LED_ON;
        // write(fd, data_buf, 1);
        // sleep(1);

        // data_buf[0] = LED_OFF;
        // write(fd, data_buf, 1);
        sleep(1);
    }

    sleep(3);
    close(fd);
    return 0;
}
