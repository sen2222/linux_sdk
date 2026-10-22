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


#define BEEP_GPIO_CHDEV_NAME      "beep_gpio_chdev"
#define BEEP_DEV_MINOR_CNT       1    
#define BEEP_ON                  1      
#define BEEP_OFF                 0
#define BEEP_DEV_NODE_PATH       "/gpio_beep"

struct beep_dev
{
    struct cdev cdev;
    dev_t dev;
    struct class *class;
    struct device *device;
    u32 major;
    u32 minor;
    struct device_node *node;
    u32 beep_gpio;
};

static struct beep_dev beep_gpio_chdev_info = {0};
static int beep_gpio_chdev_open(struct inode *inode, struct file *file)
{
    int ret = 0;
    file->private_data = (void*)&beep_gpio_chdev_info;

    beep_gpio_chdev_info.node = of_find_node_by_path(BEEP_DEV_NODE_PATH);
    if(beep_gpio_chdev_info.node == NULL)
    {
        printk(KERN_ERR "%s: can not find node %s\n", BEEP_GPIO_CHDEV_NAME, BEEP_DEV_NODE_PATH);
        return -ENODEV;
    }

    /* 获取LED的GPIO编号 */
    beep_gpio_chdev_info.beep_gpio = of_get_named_gpio(beep_gpio_chdev_info.node, "led-gpio", 0);
    if(beep_gpio_chdev_info.beep_gpio < 0)
    {
        printk(KERN_ERR "%s: can not get led-gpio\n", BEEP_GPIO_CHDEV_NAME);
        ret = -ENODEV;
    }
    /* 申请LED的GPIO */
    ret = gpio_request(beep_gpio_chdev_info.beep_gpio, BEEP_GPIO_CHDEV_NAME);
    if(ret < 0)
    {
        printk(KERN_ERR "%s: can not request led-gpio\n", BEEP_GPIO_CHDEV_NAME);
        return -EACCES;
    }

    ret = gpio_direction_output(beep_gpio_chdev_info.beep_gpio, 1);       // 设置为输出模式，并输出低电平
    if(ret < 0)
    {
        printk(KERN_ERR "%s: can not set direction of led-gpio\n", BEEP_GPIO_CHDEV_NAME);
        gpio_free(beep_gpio_chdev_info.beep_gpio);
        return -EIO;
    }

    return 0;
}

static void beep_gpio_chdev_release(struct inode *inode, struct file *file)
{
    gpio_set_value(beep_gpio_chdev_info.beep_gpio, 1);        // 设置为高电平 (低电平有效)
    gpio_free(beep_gpio_chdev_info.beep_gpio);

    if(file->private_data)
    {
        file->private_data = NULL;
    }
}

static int beep_gpio_chdev_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    u32 data_buf[count];
    struct beep_dev *led_data = (struct beep_dev*)file->private_data;

    ret = copy_from_user(data_buf, buf, count);
    if(ret < 0)
    {
        printk("kernrl write failed\n");
        return -EFAULT;
    }
    if(data_buf[0] == BEEP_ON)
    {
        gpio_set_value(led_data->beep_gpio, 0);        
    }
    else if(data_buf[0] == BEEP_OFF)
    {
        gpio_set_value(led_data->beep_gpio, 1);        
    }
    else
    {
        printk("write error\n");
        return -ENOEXEC;
    }
    return ret;
}
static size_t beep_gpio_chdev_read(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    int ret = 0;
    int val;
    struct beep_dev *led_data = (struct beep_dev*)file->private_data;
    // 读取LED状态
    val = gpio_get_value(led_data->beep_gpio);
    if(val < 0)
    {
        printk("read error\n");
        return -EIO;
    }
    return copy_to_user(buf, &val, 1);
}
static struct file_operations beep_gpio_chdev_fops = {
    .owner = THIS_MODULE,
    .open = beep_gpio_chdev_open,
    .release = beep_gpio_chdev_release,
    .write = beep_gpio_chdev_write,
    .read = beep_gpio_chdev_read,
};

static __init int beep_gpio_chdev_init(void)
{
    int ret = 0;
    // 申请设备号
    if(beep_gpio_chdev_info.major)
    {
        beep_gpio_chdev_info.dev = MKDEV(beep_gpio_chdev_info.major, beep_gpio_chdev_info.minor);
        ret = register_chrdev_region(beep_gpio_chdev_info.dev, BEEP_DEV_MINOR_CNT, BEEP_GPIO_CHDEV_NAME);
        if(ret < 0)
        {
            printk(KERN_ERR "register_chrdev_region error\n");
            ret = -EINVAL;
            goto REGION_DEV_ERRO;
        }
    }
    else
    {
        ret = alloc_chrdev_region(&beep_gpio_chdev_info.dev, beep_gpio_chdev_info.minor, BEEP_DEV_MINOR_CNT, BEEP_GPIO_CHDEV_NAME);
        if(ret < 0)
        {
            printk(KERN_ERR "alloc_chrdev_region error\n");
            ret = -EINVAL;
            goto REGION_DEV_ERRO;
        }
        beep_gpio_chdev_info.major = MAJOR(beep_gpio_chdev_info.dev);
        beep_gpio_chdev_info.minor = MINOR(beep_gpio_chdev_info.dev);
    }

    // 注册设备驱动
    beep_gpio_chdev_info.cdev.owner = THIS_MODULE;
    cdev_init(&beep_gpio_chdev_info.cdev, &beep_gpio_chdev_fops);
    ret = cdev_add(&beep_gpio_chdev_info.cdev, beep_gpio_chdev_info.dev, BEEP_DEV_MINOR_CNT);
    if(ret < 0)
    {
        printk(KERN_ERR "cdev_add error\n");
        ret = -EINVAL;
        goto CDEV_INIT_ERRO;
    }

    // 申请类
    beep_gpio_chdev_info.class = class_create(THIS_MODULE, BEEP_GPIO_CHDEV_NAME);
    if(IS_ERR(beep_gpio_chdev_info.class))
    {
        printk(KERN_ERR "class_create error\n");
        ret = -EINVAL;
        goto CLASS_INIT_ERRO;
    }
    beep_gpio_chdev_info.device = device_create(beep_gpio_chdev_info.class, NULL, beep_gpio_chdev_info.dev, NULL, BEEP_GPIO_CHDEV_NAME);
    if(IS_ERR(beep_gpio_chdev_info.device))
    {
        printk(KERN_ERR "device_create error\n");
        ret = -EINVAL;
        goto DEVICE_CREATE_ERRO;
    }

    return 0;

DEVICE_CREATE_ERRO:
    class_destroy(beep_gpio_chdev_info.class);
CLASS_INIT_ERRO:
    cdev_del(&beep_gpio_chdev_info.cdev);
    class_destroy(beep_gpio_chdev_info.class);
CDEV_INIT_ERRO:
    unregister_chrdev_region(beep_gpio_chdev_info.dev, BEEP_DEV_MINOR_CNT);
REGION_DEV_ERRO:
    return ret;
}

static __exit void beep_gpio_chdev_exit(void)
{
    device_destroy(beep_gpio_chdev_info.class, beep_gpio_chdev_info.dev);
    class_destroy(beep_gpio_chdev_info.class);
    cdev_del(&beep_gpio_chdev_info.cdev);
    unregister_chrdev_region(beep_gpio_chdev_info.dev, BEEP_DEV_MINOR_CNT);
}

module_init(beep_gpio_chdev_init);
module_exit(beep_gpio_chdev_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("sen");
