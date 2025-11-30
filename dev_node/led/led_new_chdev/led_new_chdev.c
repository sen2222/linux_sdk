#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>          
#include <linux/uaccess.h>
#include <linux/printk.h>
#include <linux/io.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/device.h>



#define LED_DEV_NAME            "led_new_chdev"
#define LDE_DEC_MINNR_CNT       1                   // 次设备号的个数


#define LED_OFF      0
#define LED_ON       1


/**
 * struct cdev {
    struct kobject kobj;                // 内嵌的kobject（内核对象基础）
    struct module *owner;               // 指向所属模块的指针
    const struct file_operations *ops;  // 文件操作函数集
    struct list_head list;              // 设备链表
    dev_t dev;                          // 设备号（主设备号+次设备号）
    unsigned int count;                 // 设备数量
};
 */


static struct {
   struct cdev cdev;        // 字符设备信息结构体
   dev_t dev;               // 设备号
   struct class *class;     // 设备类
   struct device *device;   // 设备
   int major;               // 主设备号
   int minor;               // 次设备号
}led_new_chdev_info;


/* 寄存器物理地址 */
#define CCM_CCGR1_BASE              (0x020C406C)
#define SW_MUX_GPIO1_IO3_BASE       (0x020E0068)
#define SW_PAD_GPIO1_IO3_BASE       (0x020E02F4)
#define GPIO1_DR_BASE               (0x0209C000)
#define GPIO1_GDIR_BASE             (0x0209C004)

/* 映射后的的虚拟地址指针 */
static void __iomem *ccm_ccgr1          = NULL;
static void __iomem *sw_mux_gpio1_io3   = NULL;
static void __iomem *sw_pad_gpio1_io3   = NULL;
static void __iomem *gpio1_dr           = NULL;
static void __iomem *gpio1_gdir         = NULL;


static int led_new_chdev_open(struct inode *inode, struct file *file)
{

    file->private_data = (void *)&led_new_chdev_info;

    int val;
    // 初始化LED灯
    ccm_ccgr1           = ioremap(CCM_CCGR1_BASE, 4);
    sw_mux_gpio1_io3    = ioremap(SW_MUX_GPIO1_IO3_BASE, 4);
    sw_pad_gpio1_io3    = ioremap(SW_PAD_GPIO1_IO3_BASE, 4);
    gpio1_dr            = ioremap(GPIO1_DR_BASE, 4);
    gpio1_gdir          = ioremap(GPIO1_GDIR_BASE, 4);

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

    printk("led_chdev_open\n");
    return 0;
}
static void led_new_chdec_close(struct inode *inode, struct file *file)
{
    unsigned int val;
    val = readl(gpio1_dr);
    val |= (1 << 3);
    writel(val, gpio1_dr);          // 关闭led

    // 取消映射
    iounmap(ccm_ccgr1);
    iounmap(sw_mux_gpio1_io3);
    iounmap(sw_pad_gpio1_io3);
    iounmap(gpio1_dr);
    iounmap(gpio1_gdir);


    ccm_ccgr1               = NULL;
    sw_mux_gpio1_io3        = NULL;
    sw_pad_gpio1_io3        = NULL;
    gpio1_dr                = NULL;
    gpio1_gdir              = NULL;
}

static int led_new_chdev_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    int ret;
    unsigned int val;
    unsigned char data_buf[count];
    ret = copy_from_user(data_buf, buf, count);
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
static size_t led_new_chdev_read(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    unsigned int val;
    unsigned char tmp[1];
    val = readl(gpio1_dr);

    tmp[0] = val & (1 << 3) ? LED_OFF : LED_ON;
    return copy_to_user(buf, tmp, 1);
}

static struct file_operations led_new_chdev_ops = {
    .owner = THIS_MODULE,
    .open = led_new_chdev_open,
    .release = led_new_chdec_close,
    .read = led_new_chdev_read,
    .write = led_new_chdev_write,
};

static int __init led_new_chdev_init(void)
{
    // 申请设备号
    if (led_new_chdev_info.major) // 这里指原来指定了设备号
    {
        led_new_chdev_info.dev = MKDEV(led_new_chdev_info.major, 0);
        register_chrdev_region(led_new_chdev_info.dev, LDE_DEC_MINNR_CNT, LED_DEV_NAME);
    }
    else
    {
        alloc_chrdev_region(&led_new_chdev_info.dev, 0, LDE_DEC_MINNR_CNT, LED_DEV_NAME); // 申请设备号
        led_new_chdev_info.major = MAJOR(led_new_chdev_info.dev);
        led_new_chdev_info.minor = MINOR(led_new_chdev_info.dev);
    }


    /* 类似于 register_chrdev 操作*/
    // 初始化字符设备结构体
    led_new_chdev_info.cdev.owner = THIS_MODULE;
    cdev_init(&led_new_chdev_info.cdev, &led_new_chdev_ops);

    // 添加一个cdev
    cdev_add(&led_new_chdev_info.cdev, led_new_chdev_info.dev, LDE_DEC_MINNR_CNT);



    /* 替代命令行的 mknod 操作，会自动在 /dev 下创建设备节点 */
    // 创建字符设备类
    led_new_chdev_info.class = class_create(THIS_MODULE, LED_DEV_NAME);    
    if(IS_ERR(led_new_chdev_info.class))
    {
        printk("class_create error\n");
        return -1;
    }
    led_new_chdev_info.device = device_create(led_new_chdev_info.class, NULL, led_new_chdev_info.dev, NULL, LED_DEV_NAME);
    if(IS_ERR(led_new_chdev_info.device))
    {
        printk("device_create error\n");
        return -1;
    }
    return 0;

}
static void __exit led_new_chdev_exit(void)
{
    /* 类似于 unregister_chrdev 操作 */
    cdev_del(&led_new_chdev_info.cdev);  


    unregister_chrdev_region(led_new_chdev_info.dev, LDE_DEC_MINNR_CNT);    //  释放设备号

    /* 删除字符设备类, 同时设备节点也会被删除 */
    device_destroy(led_new_chdev_info.class, led_new_chdev_info.dev);
    class_destroy(led_new_chdev_info.class);
}


module_init(led_new_chdev_init);
module_exit(led_new_chdev_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("sen");
