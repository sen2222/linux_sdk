#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>          
#include <linux/kernel.h>    
#include <linux/uaccess.h>
#include <linux/printk.h>

#define CHDEV_MAJOR         200     // 主设备号
#define CHDEV_NAME          "chdev"

static char chndev_buf[100] = {0};



static int chdev_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    int ret;
    ret = copy_from_user(chndev_buf, buf, count);
    if(ret < 0)
    {
        printk(KERN_ERR "copy_from_user error\n");
        return -EFAULT;
    }
    return count;
}

static ssize_t chdev_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    int ret;    
    ret = copy_to_user(buf, chndev_buf, count);
    if(ret < 0)
    {
        printk(KERN_ERR "copy_to_user error\n");
        return -EFAULT;
    }
    return count;
}

static int chdev_open(struct inode *inode, struct file *file)
{
    printk("chdev_open\n");

    return 0;
}

static int chdev_close(struct inode *inode, struct file *file)
{
    printk("chdev_close\n");
    return 0;
}






static struct file_operations chdex_fops = { 
   .owner = THIS_MODULE,    
   .open = chdev_open, 
   .release = chdev_close,
   .read = chdev_read,
   .write = chdev_write,
 };



static int __init chdev_init(void)
{
    printk("chdev_init\n");

    // 注册字符设备
   register_chrdev(CHDEV_MAJOR, CHDEV_NAME, &chdex_fops);


    return 0;
}
static void __exit chdev_exit(void)
{
    printk("chdev_exit\n");

    // 注销字符设备
    unregister_chrdev(CHDEV_MAJOR, CHDEV_NAME);

    return;
}


module_init(chdev_init);
module_exit(chdev_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("sen");
