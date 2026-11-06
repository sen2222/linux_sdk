#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

#include "app_config.h"
#include "ov5640.h"




struct _ov5640_ctx{
    int led_fd;
    uint8_t led_state;
};
static struct _ov5640_ctx g_ov5640_ctx = {.led_fd = -1};




int ov5640_led_on(void)
{
    if(g_ov5640_ctx.led_fd < 0)
    {
        APP_LOG(LOG_WARN, "led fd is invalid\n");
        return -1;
    }

    if (lseek(g_ov5640_ctx.led_fd, 0, SEEK_SET) < 0)
        return -1;
    int ret = write(g_ov5640_ctx.led_fd, "1", 1);
    if(ret != 1)
    {
        APP_LOG(LOG_WARN, "write led fd failed\n");
        return -1;
    }
    APP_LOG(LOG_INFO, "ov5640 led on\n");
    g_ov5640_ctx.led_state = 1;
    return 0;
}
int ov5640_led_off(void)
{
    if(g_ov5640_ctx.led_fd < 0)
    {
        APP_LOG(LOG_WARN, "led fd is invalid\n");
        return -1;
    }

    if (lseek(g_ov5640_ctx.led_fd, 0, SEEK_SET) < 0)
        return -1;
    int ret = write(g_ov5640_ctx.led_fd, "0", 1);
    if(ret != 1)
    {
        APP_LOG(LOG_WARN, "write led fd failed\n");
        return -1;
    }
    APP_LOG(LOG_INFO, "ov5640 led off\n");
    g_ov5640_ctx.led_state = 0;
    return 0;
}
int ov5640_led_toggle(void)
{
    if(g_ov5640_ctx.led_fd < 0)
    {
        APP_LOG(LOG_WARN, "led fd is invalid\n");
        return -1;
    }
    return g_ov5640_ctx.led_state ? ov5640_led_off() : ov5640_led_on();
}

int ov5640_led_get_state(void)
{
    return g_ov5640_ctx.led_fd < 0 ? -1 : g_ov5640_ctx.led_state;
}



int ov5640_init(void)
{
    if (g_ov5640_ctx.led_fd >= 0)
        return 0;
    g_ov5640_ctx.led_fd = open(OV5640_LED_DEVICE_PATH, O_WRONLY);
    if(g_ov5640_ctx.led_fd < 0)
    {
        APP_LOG(LOG_WARN, "open %s failed\n", OV5640_LED_DEVICE_PATH);
        return -1;
    }

    return 0;
}

#define CAMERA_BUFFER_COUNT 4

struct _camera_buffer
{
    void *data;
    size_t size;
};

struct _camera_capture
{
    int fd;
    int streaming;
    int allocated;
    unsigned count;
    uint32_t stride;
    size_t frame_size;
    struct _camera_buffer buffers[CAMERA_BUFFER_COUNT];
};

static struct _camera_capture g_camera = {.fd = -1};

static int camera_ioctl(unsigned long request, void *arg)
{
    int ret;
    do {
        ret = ioctl(g_camera.fd, request, arg);
    } while (ret < 0 && errno == EINTR);
    return ret;
}

void ov5640_camera_stop(void)
{
    int saved_errno = errno;
    if (g_camera.fd >= 0)
    {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        struct v4l2_requestbuffers request = {0};
        if (g_camera.streaming)
            camera_ioctl(VIDIOC_STREAMOFF, &type);
        for (unsigned i = 0; i < g_camera.count; i++)
        {
            if (g_camera.buffers[i].data != NULL && g_camera.buffers[i].data != MAP_FAILED)
                munmap(g_camera.buffers[i].data, g_camera.buffers[i].size);
        }
        if (g_camera.allocated)
        {
            request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            request.memory = V4L2_MEMORY_MMAP;
            camera_ioctl(VIDIOC_REQBUFS, &request);
        }
        close(g_camera.fd);
    }
    memset(&g_camera, 0, sizeof(g_camera));
    g_camera.fd = -1;
    errno = saved_errno;
}

int ov5640_camera_start(unsigned fps)
{
    struct v4l2_control control = {.id = V4L2_CID_VFLIP, .value = 1};
    struct v4l2_capability capability;
    struct v4l2_streamparm parameter = {0};
    struct v4l2_format format = {0};
    struct v4l2_requestbuffers request = {0};
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    const char *step = "find mx6s-csi";
    char path[32];
    if (fps != 15 && fps != 30)
    {
        errno = EINVAL;
        return -1;
    }
    ov5640_camera_stop();
    /* PxP也可能占用video0，按驱动名寻找CSI，不能只凭设备编号。 */
    for (unsigned i = 0; i < 16; i++)
    {
        uint32_t caps;
        snprintf(path, sizeof(path), "/dev/video%u", i);
        g_camera.fd = open(path, O_RDWR | O_NONBLOCK);
        if (g_camera.fd < 0)
            continue;
        memset(&capability, 0, sizeof(capability));
        if (camera_ioctl(VIDIOC_QUERYCAP, &capability) == 0 &&
            strcmp((const char *)capability.driver, "mx6s-csi") == 0)
        {
            caps = capability.capabilities & V4L2_CAP_DEVICE_CAPS ?
                   capability.device_caps : capability.capabilities;
            if ((caps & (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING)) ==
                (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING))
                break;
        }
        close(g_camera.fd);
        g_camera.fd = -1;
    }
    if (g_camera.fd < 0)
    {
        errno = ENODEV;
        goto CAMERA_START_ERRO;
    }
    /* 该BSP先用capturemode=0设置VGA，再协商像素格式。 */
    parameter.type = type;
    parameter.parm.capture.capturemode = 0;
    parameter.parm.capture.timeperframe.numerator = 1;
    parameter.parm.capture.timeperframe.denominator = fps;
    step = "set frame rate";
    if (camera_ioctl(VIDIOC_S_PARM, &parameter) < 0)
        goto CAMERA_START_ERRO;
    if (parameter.parm.capture.timeperframe.numerator == 0 ||
        parameter.parm.capture.timeperframe.denominator !=
        fps * parameter.parm.capture.timeperframe.numerator)
    {
        errno = EINVAL;
        goto CAMERA_START_ERRO;
    }
    format.type = type;
    format.fmt.pix.width = OV5640_CAMERA_WIDTH;
    format.fmt.pix.height = OV5640_CAMERA_HEIGHT;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
    format.fmt.pix.field = V4L2_FIELD_NONE;
    step = "set RGB565 format (new kernel required)";
    if (camera_ioctl(VIDIOC_S_FMT, &format) < 0)
        goto CAMERA_START_ERRO;
    g_camera.stride = format.fmt.pix.bytesperline;
    if (format.fmt.pix.width != OV5640_CAMERA_WIDTH || format.fmt.pix.height != OV5640_CAMERA_HEIGHT ||
        format.fmt.pix.pixelformat != V4L2_PIX_FMT_RGB565 || g_camera.stride < OV5640_CAMERA_WIDTH * 2 ||
        g_camera.stride > UINT16_MAX || (g_camera.stride & 1))
    {
        errno = EINVAL;
        goto CAMERA_START_ERRO;
    }
    g_camera.frame_size = (size_t)g_camera.stride * OV5640_CAMERA_HEIGHT;
    if (format.fmt.pix.sizeimage < g_camera.frame_size)
    {
        errno = EINVAL;
        goto CAMERA_START_ERRO;
    }
    step = "set vertical flip (new kernel required)";
    if (camera_ioctl(VIDIOC_S_CTRL, &control) < 0)
        goto CAMERA_START_ERRO;
    request.type = type;
    request.memory = V4L2_MEMORY_MMAP;
    request.count = CAMERA_BUFFER_COUNT;
    step = "allocate DMA buffers";
    if (camera_ioctl(VIDIOC_REQBUFS, &request) < 0)
        goto CAMERA_START_ERRO;
    g_camera.allocated = 1;
    /* 保留一帧供LVGL显示时，仍需至少两帧供CSI的双DMA地址轮换。 */
    if (request.count < 3 || request.count > CAMERA_BUFFER_COUNT)
    {
        errno = ENOBUFS;
        goto CAMERA_START_ERRO;
    }
    g_camera.count = request.count;
    for (unsigned i = 0; i < g_camera.count; i++)
    {
        struct v4l2_buffer buffer = {0};
        buffer.type = type;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = i;
        step = "map DMA buffer";
        if (camera_ioctl(VIDIOC_QUERYBUF, &buffer) < 0)
            goto CAMERA_START_ERRO;
        if (buffer.length < g_camera.frame_size)
        {
            errno = EINVAL;
            goto CAMERA_START_ERRO;
        }
        g_camera.buffers[i].size = buffer.length;
        g_camera.buffers[i].data = mmap(NULL, buffer.length, PROT_READ | PROT_WRITE,
                                        MAP_SHARED, g_camera.fd, buffer.m.offset);
        if (g_camera.buffers[i].data == MAP_FAILED || camera_ioctl(VIDIOC_QBUF, &buffer) < 0)
            goto CAMERA_START_ERRO;
    }
    step = "start CSI DMA";
    if (camera_ioctl(VIDIOC_STREAMON, &type) < 0)
        goto CAMERA_START_ERRO;
    g_camera.streaming = 1;
    APP_LOG(LOG_INFO, "camera %s: 640x480 RGB565 %u fps, DMA MMAP buffers=%u\n", path, fps, g_camera.count);
    return 0;
CAMERA_START_ERRO:
    APP_LOG(LOG_ERRO, "camera start failed (%s): %s\n", step, strerror(errno));
    ov5640_camera_stop();
    return -1;
}

int ov5640_camera_release(const OV5640_FRAME_S *frame)
{
    struct v4l2_buffer buffer = {0};
    if (frame == NULL || g_camera.fd < 0 || frame->index >= g_camera.count)
    {
        errno = EINVAL;
        return -1;
    }
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.index = frame->index;
    return camera_ioctl(VIDIOC_QBUF, &buffer);
}

int ov5640_camera_read(OV5640_FRAME_S *frame, int timeout_ms)
{
    struct v4l2_buffer buffer = {0};
    struct pollfd poll_fd = {.fd = g_camera.fd, .events = POLLIN};
    int ret;
    if (frame == NULL || !g_camera.streaming || timeout_ms < 0)
    {
        errno = EINVAL;
        return -1;
    }
    do {
        ret = poll(&poll_fd, 1, timeout_ms);
    } while (ret < 0 && errno == EINTR);
    if (ret <= 0)
        return ret;
    if (!(poll_fd.revents & POLLIN) || (poll_fd.revents & (POLLERR | POLLHUP | POLLNVAL)))
    {
        errno = EIO;
        return -1;
    }
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    if (camera_ioctl(VIDIOC_DQBUF, &buffer) < 0)
        return errno == EAGAIN ? 0 : -1;
    if (buffer.index >= g_camera.count)
    {
        errno = EIO;
        return -1;
    }
    frame->data = g_camera.buffers[buffer.index].data;
    frame->size = buffer.bytesused;
    frame->width = OV5640_CAMERA_WIDTH;
    frame->height = OV5640_CAMERA_HEIGHT;
    frame->stride = g_camera.stride;
    frame->index = buffer.index;
    if (buffer.flags & V4L2_BUF_FLAG_ERROR)
        return ov5640_camera_release(frame) < 0 ? -1 : 0;
    if (buffer.bytesused < g_camera.frame_size || buffer.bytesused > g_camera.buffers[buffer.index].size)
    {
        ov5640_camera_release(frame);
        errno = EIO;
        return -1;
    }
    return 1;
}
