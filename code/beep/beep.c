#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>


#define BEEP_DEV_PATH    "/dev/beep_gpio_chdev"

typedef enum 
{
    BEEP_OFF = 0,
    BEEP_ON
}LED_STATE;

int main(int argc, char *argv[])
{
    int fd, ret;
    char data_buf[1];
    fd = open(BEEP_DEV_PATH, O_RDWR);
    if(fd < 0)
    {
        printf("open failed\n");
        exit(1);
    }
    for(;;)
    {
        data_buf[0] = BEEP_ON;
        write(fd, data_buf, 1);
        sleep(1);

        data_buf[0] = BEEP_OFF;
        write(fd, data_buf, 1);
        sleep(1);
    }

    sleep(3);
    close(fd);
    return 0;
}
