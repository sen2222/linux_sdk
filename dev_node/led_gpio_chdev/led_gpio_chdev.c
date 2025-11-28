#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>


#define LED_DST_CHDEV_NAME      "led_gpio_chdev"
#define LED_DEV_MINOR_CNT       1    
#define LED_ON                  1      
#define LED_OFF                 0
#define LED_DEV_NODE_PATH       "/gpio_led"

struct led_dev
{
    struct cdev cdev;
    dev_t dev;
    struct class *class;
    struct device *device;
    u32 major;
    u32 minor;
    struct device_node *node;
    u32 led_gpio;
};

static struct led_dev led_gpio_chdev_info = {0};
static int led_gpio_chdev_open(struct inode *inode, struct file *file)
{
    int ret = 0;
    file->private_data = (void*)&led_gpio_chdev_info;

    led_gpio_chdev_info.node = of_find_node_by_path(LED_DEV_NODE_PATH);
    if(led_gpio_chdev_info.node == NULL)
    {
        printk(KERN_ERR "%s: can not find node %s\n", LED_DST_CHDEV_NAME, LED_DEV_NODE_PATH);
        return -ENODEV;
    }

    /* 获取LED的GPIO编号 */
    led_gpio_chdev_info.led_gpio = of_get_named_gpio(led_gpio_chdev_info.node, "led-gpio", 0);
    if(led_gpio_chdev_info.led_gpio < 0)
    {
        printk(KERN_ERR "%s: can not get led-gpio\n", LED_DST_CHDEV_NAME);
        ret = -ENODEV;
    }
    /* 申请LED的GPIO */
    ret = gpio_request(led_gpio_chdev_info.led_gpio, LED_DST_CHDEV_NAME);
    if(ret < 0)
    {
        printk(KERN_ERR "%s: can not request led-gpio\n", LED_DST_CHDEV_NAME);
        return -EACCES;
    }

    ret = gpio_direction_output(led_gpio_chdev_info.led_gpio, 1);       // 设置为输出模式，并输出高电平
    if(ret < 0)
    {
        printk(KERN_ERR "%s: can not set direction of led-gpio\n", LED_DST_CHDEV_NAME);
        gpio_free(led_gpio_chdev_info.led_gpio);
        return -EIO;
    }

    return 0;
}

static void led_gpio_chdev_release(struct inode *inode, struct file *file)
{
    gpio_set_value(led_gpio_chdev_info.led_gpio, 1);        // 设置为高电平(关灯)
    gpio_free(led_gpio_chdev_info.led_gpio);

    if(file->private_data)
    {
        file->private_data = NULL;
    }
}

static int led_gpio_chdev_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    u32 data_buf[count];
    struct led_dev *led_data = (struct led_dev*)file->private_data;

    ret = copy_from_user(data_buf, buf, count);
    if(ret < 0)
    {
        printk("kernrl write failed\n");
        return -EFAULT;
    }
    if(data_buf[0] == LED_ON)
    {
        gpio_set_value(led_data->led_gpio, 0);        // 0为低电平(开灯)
    }
    else if(data_buf[0] == LED_OFF)
    {
        gpio_set_value(led_data->led_gpio, 1);        // 1为高电平(关灯)
    }
    else
    {
        printk("write error\n");
        return -ENOEXEC;
    }
    return ret;
}
static size_t led_gpio_chdev_read(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    int val;
    struct led_dev *led_data = (struct led_dev*)file->private_data;
    // 读取LED状态
    val = gpio_get_value(led_data->led_gpio);
    if(val < 0)
    {
        printk("read error\n");
        return -EIO;
    }
    return copy_to_user(buf, &val, 1);
}
static struct file_operations led_gpio_chdev_fops = {
    .owner = THIS_MODULE,
    .open = led_gpio_chdev_open,
    .release = led_gpio_chdev_release,
    .write = led_gpio_chdev_write,
    .read = led_gpio_chdev_read,
};

static __init int led_gpio_chdev_init(void)
{
    int ret = 0;
    // 申请设备号
    if(led_gpio_chdev_info.major)
    {
        led_gpio_chdev_info.dev = MKDEV(led_gpio_chdev_info.major, led_gpio_chdev_info.minor);
        ret = register_chrdev_region(led_gpio_chdev_info.dev, LED_DEV_MINOR_CNT, LED_DST_CHDEV_NAME);
        if(ret < 0)
        {
            printk(KERN_ERR "register_chrdev_region error\n");
            ret = -EINVAL;
            goto REGION_DEV_ERRO;
        }
    }
    else
    {
        // 申请一个设备号
        ret = alloc_chrdev_region(&led_gpio_chdev_info.dev, led_gpio_chdev_info.minor, LED_DEV_MINOR_CNT, LED_DST_CHDEV_NAME);
        if(ret < 0)
        {
            printk(KERN_ERR "alloc_chrdev_region error\n");
            ret = -EINVAL;
            goto REGION_DEV_ERRO;
        }
        led_gpio_chdev_info.major = MAJOR(led_gpio_chdev_info.dev);
        led_gpio_chdev_info.minor = MINOR(led_gpio_chdev_info.dev);
    }

    // 注册设备驱动
    led_gpio_chdev_info.cdev.owner = THIS_MODULE;
    cdev_init(&led_gpio_chdev_info.cdev, &led_gpio_chdev_fops);
    ret = cdev_add(&led_gpio_chdev_info.cdev, led_gpio_chdev_info.dev, LED_DEV_MINOR_CNT);
    if(ret < 0)
    {
        printk(KERN_ERR "cdev_add error\n");
        ret = -EINVAL;
        goto CDEV_INIT_ERRO;
    }

    // 申请类
    led_gpio_chdev_info.class = class_create(THIS_MODULE, LED_DST_CHDEV_NAME);
    if(IS_ERR(led_gpio_chdev_info.class))
    {
        printk(KERN_ERR "class_create error\n");
        ret = -EINVAL;
        goto CLASS_INIT_ERRO;
    }
    led_gpio_chdev_info.device = device_create(led_gpio_chdev_info.class, NULL, led_gpio_chdev_info.dev, NULL, LED_DST_CHDEV_NAME);
    if(IS_ERR(led_gpio_chdev_info.device))
    {
        printk(KERN_ERR "device_create error\n");
        ret = -EINVAL;
        goto DEVICE_CREATE_ERRO;
    }

    return 0;

DEVICE_CREATE_ERRO:
    device_destroy(led_gpio_chdev_info.class, led_gpio_chdev_info.dev);
CLASS_INIT_ERRO:
    cdev_del(&led_gpio_chdev_info.cdev);
    class_destroy(led_gpio_chdev_info.class);
CDEV_INIT_ERRO:
    unregister_chrdev_region(led_gpio_chdev_info.dev, LED_DEV_MINOR_CNT);
REGION_DEV_ERRO:
    return ret;
}

static __exit void led_gpio_chdev_exit(void)
{
    device_destroy(led_gpio_chdev_info.class, led_gpio_chdev_info.dev);
    class_destroy(led_gpio_chdev_info.class);
    cdev_del(&led_gpio_chdev_info.cdev);
    unregister_chrdev_region(led_gpio_chdev_info.dev, LED_DEV_MINOR_CNT);
}

module_init(led_gpio_chdev_init);
module_exit(led_gpio_chdev_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("sen");
