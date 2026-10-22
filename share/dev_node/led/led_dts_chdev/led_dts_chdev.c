#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>          
#include <linux/uaccess.h>
#include <linux/printk.h>
#include <linux/io.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/of.h>           
#include <linux/of_address.h>   


#define LED_DST_CHDEV_NAME      "led_dts_chdev"
#define LED_DEV_MINOR_CNT       1    
#define LED_ON                  1      
#define LED_OFF                 0
#define LED_DEV_NODE_PATH       "/led"

static void __iomem *ccm_ccgr1          = NULL;
static void __iomem *sw_mux_gpio1_io3   = NULL;
static void __iomem *sw_pad_gpio1_io3   = NULL;
static void __iomem *gpio1_dr           = NULL;
static void __iomem *gpio1_gdir         = NULL;

static struct 
{
    struct cdev cdev;
    dev_t dev;
    struct class *class;
    struct device *device;
    int major;
    int minor;
    struct device_node *node;
} led_dst_chdev_info;



static int led_dst_chdev_open(struct inode *inode, struct file *file)
{
    file->private_data = (void*)&led_dst_chdev_info;
    int ret = 0;
    struct property *prop = NULL;
    u8 *status_str = NULL;
    u32 reg_data[10];
    u32 val = 0;
    // 读取设备树

    /* 寻找设备树节点 */
    led_dst_chdev_info.node = of_find_node_by_path(LED_DEV_NODE_PATH);
    if(led_dst_chdev_info.node == NULL)
    {
        printk("of_find_node_by_path error\n");
        return -EINVAL;
    }
#if 0
    /* 获取 compatible 属性*/
    prop = of_find_property(led_dst_chdev_info.node, "compatible", NULL);
    if(!prop)
    {
        printk("of_find_property error\n");
        return -EINVAL;
    }
    printk("compatible = %s\n", prop->value);
#endif
    /* 获取status 属性 */
    ret = of_property_read_string(led_dst_chdev_info.node, "status", &status_str);
    if(ret < 0)
    {
        printk("of_property_read_string error\n");
        return -EINVAL;
    }
    printk("status = %s\n", status_str);

    /* 获取寄存器地址 */
    ret = of_property_read_u32_array(led_dst_chdev_info.node, "reg", reg_data, 10);
    if(ret < 0)
    {
        printk("of_property_read_u32_array error\n");
        return -EINVAL;
    }
    /* 映射寄存器 */
    ccm_ccgr1           = of_iomap(led_dst_chdev_info.node, 0);
    sw_mux_gpio1_io3    = of_iomap(led_dst_chdev_info.node, 1);
    sw_pad_gpio1_io3    = of_iomap(led_dst_chdev_info.node, 2);
    gpio1_dr            = of_iomap(led_dst_chdev_info.node, 3);
    gpio1_gdir          = of_iomap(led_dst_chdev_info.node, 4);

    val = readl(ccm_ccgr1);
    val &= ~(0x3 << 26);        // 清零
    val |= 0x03 << 26;
    writel(val, ccm_ccgr1);

    writel(0x05, sw_mux_gpio1_io3);     // 设置复用
    writel(0x10B0, sw_pad_gpio1_io3);   // 设置电气属性

    val = readl(gpio1_gdir);            // 设置为输出
    val |= 1 << 3;
    writel(val, gpio1_gdir);

    val = readl(gpio1_dr);
    val |= (1 << 3);                // 高电平关灯
    writel(val, gpio1_dr);     

    return 0;
}

static void led_dst_chdev_release(struct inode *inode, struct file *file)
{
    u32 val;
    struct led_dst_chdev_info *info = (struct led_dst_chdev_info*)file->private_data;
    if(info)
    {
        file->private_data = NULL;
    }
    val = readl(gpio1_dr);
    val |= (1 << 3);
    writel(val, gpio1_dr);

    iounmap(ccm_ccgr1);
    iounmap(sw_mux_gpio1_io3);
    iounmap(sw_pad_gpio1_io3);
    iounmap(gpio1_dr);
    iounmap(gpio1_gdir);
}

static int led_dst_chdev_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    int ret;
    u32 val;
    const u8 data_buf[count];
    ret = copy_from_user((void*)data_buf, buf, count);
    if(ret < 0)
    {
        printk("kernrl write failed\n");
        return -EFAULT;
    }
    if(data_buf[0] == LED_OFF)
    {
        val = readl(gpio1_dr);
        val |= (1 << 3);                // 高电平关灯
        writel(val, gpio1_dr);        
    }else if(data_buf[0] == LED_ON){
        val = readl(gpio1_dr);
        val &= ~(1 << 3);
        writel(val, gpio1_dr);
    }else{
        printk("invalid command\n");
        return -EINVAL;
    }

    return ret;
}
static size_t led_dst_chdev_read(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    u32 val;
    u8 tmp[1];
    val = readl(gpio1_dr);

    tmp[0] = val & (1 << 3) ? LED_OFF : LED_ON;
    return copy_to_user((void*)buf, tmp, 1);
}


static struct file_operations led_dst_chdev_ops = {
    .owner = THIS_MODULE,
    .open = led_dst_chdev_open,
    .release = led_dst_chdev_release,
    .read = led_dst_chdev_read,
    .write = led_dst_chdev_write,
};

static __init int led_dts_chdev_init(void)
{
    int ret = 0;
    // 申请设备号
    if(led_dst_chdev_info.major)
    {
        led_dst_chdev_info.dev = MKDEV(led_dst_chdev_info.major, led_dst_chdev_info.minor);
        ret = register_chrdev_region(led_dst_chdev_info.dev, LED_DEV_MINOR_CNT, LED_DST_CHDEV_NAME);
        if(ret < 0)
        {
            printk("register_chrdev_region error\n");
            ret = -EINVAL;
            goto REGION_DEV_ERRO;
        }
    }
    else
    {
        ret = alloc_chrdev_region(&led_dst_chdev_info.dev, led_dst_chdev_info.minor, LED_DEV_MINOR_CNT, LED_DST_CHDEV_NAME);
        if(ret < 0)
        {
            printk("alloc_chrdev_region error\n");
            ret = -EINVAL;
            goto REGION_DEV_ERRO;
        }
        led_dst_chdev_info.major = MAJOR(led_dst_chdev_info.dev);
        led_dst_chdev_info.minor = MINOR(led_dst_chdev_info.dev);
    }

    // 注册设备驱动
    led_dst_chdev_info.cdev.owner = THIS_MODULE;
    cdev_init(&led_dst_chdev_info.cdev, &led_dst_chdev_ops);
    ret = cdev_add(&led_dst_chdev_info.cdev, led_dst_chdev_info.dev, LED_DEV_MINOR_CNT);
    if(ret < 0)
    {
        printk("cdev_add error\n");
        ret = -EINVAL;
        goto CDEV_INIT_ERRO;
    }

    // 申请类
    led_dst_chdev_info.class = class_create(THIS_MODULE, LED_DST_CHDEV_NAME);
    if(IS_ERR(led_dst_chdev_info.class))
    {
        printk("class_create error\n");
        ret = -EINVAL;
        goto CLASS_INIT_ERRO;
    }
    led_dst_chdev_info.device = device_create(led_dst_chdev_info.class, NULL, led_dst_chdev_info.dev, NULL, LED_DST_CHDEV_NAME);
    if(IS_ERR(led_dst_chdev_info.device))
    {
        printk("device_create error\n");
        ret = -EINVAL;
        goto DEVICE_CREATE_ERRO;
    }

    return 0;

DEVICE_CREATE_ERRO:
    class_destroy(led_dst_chdev_info.class);
CLASS_INIT_ERRO:
    cdev_del(&led_dst_chdev_info.cdev);
    class_destroy(led_dst_chdev_info.class);
CDEV_INIT_ERRO:
    unregister_chrdev_region(led_dst_chdev_info.dev, LED_DEV_MINOR_CNT);
REGION_DEV_ERRO:
    return ret;
}
static __exit void led_dts_chdev_exit(void)
{
    device_destroy(led_dst_chdev_info.class, led_dst_chdev_info.dev);
    class_destroy(led_dst_chdev_info.class);
    cdev_del(&led_dst_chdev_info.cdev);
    unregister_chrdev_region(led_dst_chdev_info.dev, LED_DEV_MINOR_CNT);
}



module_init(led_dts_chdev_init);
module_exit(led_dts_chdev_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("sen");

