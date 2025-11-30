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
#include <linux/slab.h>

#define LED_DEVICE_MINOR_CNT    1      // 次设备号的数量
#define LED_DEVICE_NAME         "led_device"
#define LED_DEVICE_COMPATIBLE   "sen,gpio-led"

#define LED_ON      0
#define LED_OFF     1


struct led_device_info
{
    u8 led_gpio_init_flag;
    u32 led_gpio_num;

    struct cdev cdev;
    dev_t dev;
    int major;
    int minor;
    struct file_operations led_device_fops;
    
    struct class *class;
    struct device *device;
};

static struct led_device_info *led_info = NULL;

static int led_device_open(struct inode *inode, struct file *file)
{
    int ret;
    struct device_node *node;
    
    if(led_info->led_gpio_init_flag) 
        return 0;

    // 根据compatible属性寻找节点
    node = of_find_compatible_node(NULL, NULL, LED_DEVICE_COMPATIBLE);
    if(!node)
    {
        printk(KERN_ERR "%s: can not find compatible node %s\n", LED_DEVICE_NAME, LED_DEVICE_COMPATIBLE);
        return -ENODEV;
    }
    // 获取gpio编号
    led_info->led_gpio_num = of_get_named_gpio(node, "led-gpio", 0);
    of_node_put(node);      // 这里表示把这个节点释放掉
    if(led_info->led_gpio_num < 0)
    {
        printk(KERN_ERR "%s: can not get led-gpio\n", LED_DEVICE_NAME);
        return -ENODEV; 
    }
    
    // 申请gpio
    ret = gpio_request(led_info->led_gpio_num, LED_DEVICE_NAME);
    if(ret < 0)
    {
        printk(KERN_ERR "%s: can not request led-gpio[%d]\n", LED_DEVICE_NAME, led_info->led_gpio_num);
        return -EACCES;
    }

    ret = gpio_direction_output(led_info->led_gpio_num, LED_OFF);
    if(ret < 0)
    {
        printk(KERN_ERR "%s: can not set direction of led-gpio[%d]", LED_DEVICE_NAME, led_info->led_gpio_num);
        gpio_free(led_info->led_gpio_num);
        return -EIO;
    }
    led_info->led_gpio_init_flag = 1;
    return 0;
}
static int led_device_release(struct inode *inode, struct file *file)
{
    if(led_info && led_info->led_gpio_init_flag)
    {
        gpio_set_value(led_info->led_gpio_num, LED_OFF);
    }
    return 0;
}

static ssize_t led_device_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    int val;
    if (count < 1) {
        return -EINVAL;
    }
    val = gpio_get_value(led_info->led_gpio_num);
    if(val < 0)
    {
        printk(KERN_ERR "%s: read led-gpio[%d]\n", LED_DEVICE_NAME, led_info->led_gpio_num);
        return -EIO;
    }
    ret = copy_to_user((void*)buf, &val, 1);
    if(ret < 0)
    {
        printk(KERN_ERR "%s: copy_to_user filed\n", LED_DEVICE_NAME);
        return -EFAULT;
    }
    return 1;
}

static ssize_t led_device_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    u32 val[1];

    ret = copy_from_user(val, buf, 1);
    if(ret < 0)
    {
        printk(KERN_ERR "%s: copy_from_user failed\n", LED_DEVICE_NAME);
        return -EFAULT;
    }

    if(val[0] == LED_ON || val[0] == LED_OFF)
    {
        gpio_set_value(led_info->led_gpio_num, val[0]);
    }
    else
    {
        printk(KERN_ERR "%s: invalid value\n", LED_DEVICE_NAME);
        return -EINVAL;
    }

    return 1;
}

static inline void led_device_fops_init(struct file_operations *fops)
{
    fops->owner = THIS_MODULE;
    fops->open = led_device_open;
    fops->release = led_device_release;
    fops->read = led_device_read;
    fops->write = led_device_write;
}

static __init int led_device_init(void)
{
    int ret;

    led_info = kzalloc(sizeof(struct led_device_info), GFP_KERNEL);
    if(!led_info)
    {
        printk(KERN_ERR "kzalloc filaed\n");
        return -1;
    }

    // 申请设备号
    ret = alloc_chrdev_region(&led_info->dev, 0, LED_DEVICE_MINOR_CNT, LED_DEVICE_NAME);
    if(ret < 0)
    {
        printk(KERN_ERR "alloc_chrdev_region failed\n");
        goto ALLOC_DEV_ERR;
    }
    led_info->major = MAJOR(led_info->dev);
    led_info->minor = MINOR(led_info->dev);

    // 注册设备驱动
    led_device_fops_init(&led_info->led_device_fops);
    cdev_init(&led_info->cdev, &led_info->led_device_fops);
    ret = cdev_add(&led_info->cdev, led_info->dev, LED_DEVICE_MINOR_CNT);
    if(ret < 0)
    {
        printk(KERN_ERR "cdev_add failed\n");
        goto CDEV_INIT_ERR;
    }

    // 创建类和设备节点
    led_info->class = class_create(THIS_MODULE, LED_DEVICE_NAME);
    if(IS_ERR(led_info->class))
    {
        printk(KERN_ERR "class_create failed\n");
        goto CLASS_CREATE_ERR;
    }
    led_info->device = device_create(led_info->class, NULL, led_info->dev, NULL, LED_DEVICE_NAME);
    if(IS_ERR(led_info->device))
    {
        printk(KERN_ERR "device_create failed\n");
        goto DEVICE_CREATE_ERR;
    }
    return 0;

DEVICE_CREATE_ERR:
    class_destroy(led_info->class);
CLASS_CREATE_ERR:
    cdev_del(&led_info->cdev);
CDEV_INIT_ERR:
    unregister_chrdev_region(led_info->dev, LED_DEVICE_MINOR_CNT);
ALLOC_DEV_ERR:
    kfree(led_info);
    led_info = NULL;

    return ret;
}
static __exit void led_device_exit(void)
{
    if(led_info)
    {
        if(led_info->led_gpio_init_flag)
        {
            gpio_set_value(led_info->led_gpio_num, LED_ON);
            gpio_free(led_info->led_gpio_num);
        }
        device_destroy(led_info->class, led_info->dev);
        class_destroy(led_info->class);
        cdev_del(&led_info->cdev);
        unregister_chrdev_region(led_info->dev, LED_DEVICE_MINOR_CNT);
        kfree(led_info);
        led_info = NULL;
    }
}

module_init(led_device_init);
module_exit(led_device_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("sen");