#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "stdint.h"

#include "app_config.h"

#include "lvgl_port/lvgl_port.h"

#include "app_view/view_class.h"


int main(int argc, char *argv[])
{
    
    lvgl_port_init();
    app_view_init();

    while (1) {
        sleep(1);
    }
    return 0;
}
