#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "stdint.h"

#include "app_config.h"

#include "lvgl_port/lvgl_port.h"

#include "app_view/view_class.h"
#include "camera/ov5640.h"
#include "tool/system_time.h"



int main(int argc, char *argv[])
{
    
    if (system_time_init() < 0)
        return 1;
    if (lvgl_port_init() < 0)
        return 1;
    ov5640_init();
    if (app_view_init() < 0)
    {
        lvgl_port_deinit();
        return 1;
    }

    while (1) {
        sleep(10);
        if (!system_time_get_sync_flag())
            system_time_sync();
    }
    return 0;
}
