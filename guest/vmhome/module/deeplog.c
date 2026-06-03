#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/cred.h>
#include <linux/fs.h>

extern void deeplog_start_logging(void);
extern void deeplog_stop_logging(void);
extern void deeplog_clean_log(void);
extern bool DEEPLOG_FUNCTION_MAP[335][5271];
extern int number_of_functions;
extern int number_of_syscalls;

#define DEEPLOG_START _IO('p', 1)
#define DEEPLOG_EXTRACT _IO('p', 2)
#define DEEPLOG_STOP _IO('p', 3)
#define DEEPLOG_CLEAN _IO('p', 4)
#define DEEPLOG_SET _IO('p', 5)

MODULE_LICENSE("GPL");

static int device_open(struct inode *inode, struct file *filp)
{
        printk(KERN_ALERT "Device opened.\n");
        return 0;
}

static int device_release(struct inode *inode, struct file *filp)
{
        printk(KERN_ALERT "Device closed.\n");
        return 0;
}

static ssize_t device_read(struct file *filp, char *buffer, size_t length, loff_t *offset)
{
        return -EINVAL;
}

static ssize_t device_write(struct file *filp, const char *buf, size_t len, loff_t *off)
{
        return -EINVAL;
}

int syscall_index = 0;
static long device_ioctl(struct file *filp, unsigned int ioctl_num, unsigned long ioctl_param)
{
        printk(KERN_ALERT "Got ioctl argument %#x!", ioctl_num);
        switch (ioctl_num) {
                case DEEPLOG_START:
                        pr_info("Starting log\n");
                        deeplog_start_logging();
                        break;
                case DEEPLOG_EXTRACT:
                        pr_info("Extracting log data\n");
                        pr_alert("Write %ld bytes to userspace!\n", copy_to_user((bool *)ioctl_param, &DEEPLOG_FUNCTION_MAP[syscall_index], sizeof(bool) * 5272));
                        break;
                case DEEPLOG_STOP:
                        pr_info("Stopping log\n");
                        deeplog_stop_logging();
                        break;
                case DEEPLOG_CLEAN:
                        pr_info("Cleaning up map\n");
                        deeplog_clean_log();
                        break;
                case DEEPLOG_SET:
                        pr_info("setting index to: %d\n", (int)ioctl_param);
                        syscall_index = (int)ioctl_param;
                        break;
                default:
                        return 1;
        }
        return 0;
}

static struct proc_ops fops = {
        .proc_read = device_read,
        .proc_write = device_write,
        .proc_ioctl = device_ioctl,
        .proc_open = device_open,
        .proc_release = device_release
};

struct proc_dir_entry *proc_entry = NULL;

int init_module(void)
{
        printk(KERN_ALERT "ioctl address: %#lx\b", (unsigned long)device_ioctl);
        printk(KERN_ALERT "DEEPLOG_START value: %#x\b", DEEPLOG_START);
        proc_entry = proc_create("deeplog-ioctl", 0666, NULL, &fops);
        return 0;
}

void cleanup_module(void)
{
        if (proc_entry) proc_remove(proc_entry);
}
