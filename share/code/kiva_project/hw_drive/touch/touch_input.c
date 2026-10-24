#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/input.h>
#include <errno.h>

#include "touch_input.h"

static int scale_coordinate(int value, int min, int max, int size)
{
    if (size <= 1 || max <= min || value <= min)
        return 0;
    if (value >= max)
        return size - 1;
    return (int)(((int64_t)value - min) * (size - 1) / ((int64_t)max - min));
}

static void touch_input_feed(TOUCH_INPUT_S *touch, const struct input_event *event)
{
    TOUCH_CONTACT_S *contact;
    int i;

    if (event->type == EV_SYN && event->code == SYN_DROPPED)
    {
        memset(touch->contacts, 0, sizeof(touch->contacts));
        touch->pressed = 0;
        touch->primary = -1;
        touch->dropped = 1;
        return;
    }
    if (touch->dropped)
    {
        if (event->type == EV_SYN && event->code == SYN_REPORT)
            touch->dropped = 0;
        return;
    }
    if (event->type == EV_SYN && event->code == SYN_REPORT)
    {
        if (touch->primary < 0 || !touch->contacts[touch->primary].down)
        {
            touch->primary = -1;
            for (i = 0; i < TOUCH_INPUT_SLOTS; i++)
            {
                if (touch->contacts[i].down)
                {
                    touch->primary = i;
                    break;
                }
            }
        }
        touch->pressed = touch->primary >= 0;
        if (touch->pressed)
        {
            contact = &touch->contacts[touch->primary];
            touch->x = scale_coordinate(contact->x, touch->min_x,
                                        touch->max_x, touch->width);
            touch->y = scale_coordinate(contact->y, touch->min_y,
                                        touch->max_y, touch->height);
        }
        return;
    }
    if (touch->multitouch && event->type == EV_ABS && event->code == ABS_MT_SLOT)
    {
        touch->slot = event->value >= 0 && event->value < TOUCH_INPUT_SLOTS ? event->value : -1;
        return;
    }
    if (touch->slot < 0 || touch->slot >= TOUCH_INPUT_SLOTS)
        return;
    contact = &touch->contacts[touch->slot];
    if (touch->multitouch && event->type == EV_ABS)
    {
        if (event->code == ABS_MT_TRACKING_ID)
            contact->down = event->value >= 0;
        else if (event->code == ABS_MT_POSITION_X)
            contact->x = event->value;
        else if (event->code == ABS_MT_POSITION_Y)
            contact->y = event->value;
    }
    else if (!touch->multitouch)
    {
        if (event->type == EV_ABS && event->code == ABS_X)
            contact->x = event->value;
        else if (event->type == EV_ABS && event->code == ABS_Y)
            contact->y = event->value;
        else if (event->type == EV_KEY && event->code == BTN_TOUCH)
            contact->down = event->value != 0;
    }
}
static int touch_input_resync(TOUCH_INPUT_S *touch, int fd)
{
    struct input_event report;
    struct input_absinfo axis;
    int ids[TOUCH_INPUT_SLOTS + 1];
    int xs[TOUCH_INPUT_SLOTS + 1] = {0};
    int ys[TOUCH_INPUT_SLOTS + 1] = {0};
    unsigned char keys[(KEY_MAX + 8) / 8] = {0};
    int i;

    memset(touch->contacts, 0, sizeof(touch->contacts));
    touch->primary = -1;
    touch->pressed = 0;
    touch->dropped = 0;
    if (touch->multitouch)
    {
        for (i = 0; i <= TOUCH_INPUT_SLOTS; i++)
            ids[i] = -1;
        ids[0] = ABS_MT_TRACKING_ID;
        xs[0] = ABS_MT_POSITION_X;
        ys[0] = ABS_MT_POSITION_Y;
        if (ioctl(fd, EVIOCGABS(ABS_MT_SLOT), &axis) < 0 ||
            ioctl(fd, EVIOCGMTSLOTS(sizeof(ids)), ids) < 0 ||
            ioctl(fd, EVIOCGMTSLOTS(sizeof(xs)), xs) < 0 ||
            ioctl(fd, EVIOCGMTSLOTS(sizeof(ys)), ys) < 0)
            return -1;
        touch->slot = axis.value >= 0 && axis.value < TOUCH_INPUT_SLOTS ? axis.value : -1;
        for (i = 0; i < TOUCH_INPUT_SLOTS; i++)
        {
            touch->contacts[i].down = ids[i + 1] >= 0;
            touch->contacts[i].x = xs[i + 1];
            touch->contacts[i].y = ys[i + 1];
        }
    }
    else
    {
        if (ioctl(fd, EVIOCGABS(ABS_X), &axis) < 0)
            return -1;
        touch->contacts[0].x = axis.value;
        if (ioctl(fd, EVIOCGABS(ABS_Y), &axis) < 0 ||
            ioctl(fd, EVIOCGKEY(sizeof(keys)), keys) < 0)
            return -1;
        touch->contacts[0].y = axis.value;
        touch->contacts[0].down = (keys[BTN_TOUCH / 8] >> (BTN_TOUCH % 8)) & 1;
    }
    memset(&report, 0, sizeof(report));
    report.type = EV_SYN;
    report.code = SYN_REPORT;
    touch_input_feed(touch, &report);
    return 0;
}

int lvgl_port_touch_init(TOUCH_INPUT_S *touch, int width, int height)
{
    struct input_absinfo x, y, slots;
    int fd = open(LVGL_PORT_TOUCH_PATH, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    int mt;
    touch->width = width;
    touch->height = height;
    mt = ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &x) == 0 && ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &y) == 0 && ioctl(fd, EVIOCGABS(ABS_MT_SLOT), &slots) == 0;
    if ((!mt && (ioctl(fd, EVIOCGABS(ABS_X), &x) < 0 ||
                 ioctl(fd, EVIOCGABS(ABS_Y), &y) < 0)) ||
        (mt && (slots.minimum != 0 || slots.maximum >= TOUCH_INPUT_SLOTS)) ||
        x.maximum <= x.minimum || y.maximum <= y.minimum)
    {
        close(fd);
        return -1;
    }
    touch->multitouch = mt;
    touch->min_x = x.minimum;
    touch->max_x = x.maximum;
    touch->min_y = y.minimum;
    touch->max_y = y.maximum;
    if (touch_input_resync(touch, fd) < 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}


/* Return one synchronized frame so a queued release cannot hide a quick tap. */
int touch_input_read(TOUCH_INPUT_S *touch, int fd)
{
    struct input_event event;
    ssize_t bytes;
    int resync;

    for (;;)
    {
        bytes = read(fd, &event, sizeof(event));
        if (bytes == sizeof(event))
        {
            resync = touch->dropped;
            touch_input_feed(touch, &event);
            if (event.type == EV_SYN && event.code == SYN_REPORT)
            {
                if (resync && touch_input_resync(touch, fd) < 0)
                    return -1;
                return 1;
            }
        }
        else if (bytes < 0 && errno == EINTR)
        {
            continue;
        }
        else if (bytes < 0 && errno == EAGAIN)
        {
            return 0;
        }
        else
        {
            touch->pressed = 0;
            return -1;
        }
    }
}
