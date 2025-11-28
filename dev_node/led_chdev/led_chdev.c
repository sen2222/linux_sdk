#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>          
#include <linux/uaccess.h>
#include <linux/printk.h>
#include <linux/io.h>
#include <linux/types.h>


#define LED_CHDEV_NAME      "led_chdev"
#define LED_CHDEV_MAJOR     201

#define LED_OFF      0
#define LED_ON       1


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


static int led_chdev_open(struct inode *inode, struct file *file)
{
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
static void led_chdec_close(struct inode *inode, struct file *file)
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

static int led_chdev_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
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
static size_t led_chdev_read(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    unsigned int val;
    val = readl(gpio1_dr);
    return val & (1 << 3) ? 0 : 1;
}



static struct file_operations led_chdev_ops = { 
    .owner = THIS_MODULE,    
    .open = led_chdev_open,
    .release = led_chdec_close,
    .read = led_chdev_read,
    .write = led_chdev_write,
 };

static int __init led_chdev_init(void)
{
    int ret = 0;
    ret = register_chrdev(LED_CHDEV_MAJOR, LED_CHDEV_NAME, &led_chdev_ops);
    if(ret < 0)
    {
        printk(KERN_ERR "register chrdev failed\n");
        return -EIO;
    }
    return 0;
}
static void __exit led_chdev_exit(void)
{
    unregister_chrdev(LED_CHDEV_MAJOR, LED_CHDEV_NAME);
}

module_init(led_chdev_init);
module_exit(led_chdev_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("sen");