#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
int main(int argc, char *argv[])
{
    int fd, ret;
    char write_buf[] = "hello world";
    char read_buf[100];
    fd = open("/dev/chdev", O_RDWR);
    if(fd < 0)
    {
        printf("open failed\n");
        exit(1);
    }
    while(1)
    {
        ret = write(fd, write_buf, sizeof(write_buf));
        if(ret < 0)
        {
            printf("write failed\n");
        }
        sleep(1);
        memset(read_buf, 0, sizeof(read_buf));
        ret = read(fd, read_buf, sizeof(write_buf));
        if(ret < 0)
        {
            printf("read failed\n");
        }
        printf("read data: %s\n", read_buf);
        sleep(1);

    }
    return 0;
}
