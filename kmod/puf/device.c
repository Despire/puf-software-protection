#include <linux/miscdevice.h>

static int puf_open(struct inode *inode, struct file *file) {
    // TODO: add initialization code. (possibly add check for PID)
    printk(KERN_INFO "puf opened by process with PID: %d\n", current->pid);
    return 0;
}

static ssize_t puf_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
    return simple_read_from_buffer(buf, count, ppos, puf_addr, puf_size);
}

static ssize_t puf_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
    size_t i = 0;
    uint8_t* data = 0;

    data = (uint8_t *) kzalloc(count, GFP_KERNEL);
    if (!data) {
        printk(KERN_ERR "Failed to alloc buffer for user provided data");
        return -ENOMEM;
    }

    if (copy_from_user(data, buf, count)) {
        printk(KERN_ERR "Failed to copy provided data from user space into kernel space");
        kfree(data);
        return -EFAULT;
    }

    printk(KERN_INFO "Data written to device");
    for (i = 0; i < count; i++) {
        printk(KERN_CONT " %02x", data[i]);
    }
    printk(KERN_CONT "\n");

    kfree(data);
    return count;
}

static const struct file_operations puf_fops = {
    .owner = THIS_MODULE,
    .open  = puf_open,
    .read  = puf_read,
    .write = puf_write,
};

static struct miscdevice puf_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = DEVICE_NAME,
    .fops  = &puf_fops,
};
