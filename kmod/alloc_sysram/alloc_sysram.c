#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/gfp.h>
#include <linux/mm_types.h>

#define DEVICE_NAME_1 "puf_block_1"
#define DEVICE_NAME_2 "puf_block_2"

#define ROW_SIZE sizeof(unsigned int) * (1<<9) // word_size * page size

typedef unsigned int uint32_t;
typedef int error;

static void *puf_block_1 = 0x0;
static void *puf_block_2 = 0x0;

uint32_t alloc_size_1 = 0x0;
uint32_t alloc_size_2 = 0x0;

module_param(alloc_size_1, uint, S_IRUGO);
module_param(alloc_size_2, uint, S_IRUGO);

static ssize_t block_1_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
    return simple_read_from_buffer(buf, count, ppos, puf_block_1, alloc_size_1);
}

static const struct file_operations puf_block_1_fops = {
    .owner = THIS_MODULE,
    .read = block_1_read,
};

static struct miscdevice puf_block_1_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = DEVICE_NAME_1,
    .fops = &puf_block_1_fops,
    .mode = 0444, // read-only
};

static ssize_t block_2_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
    return simple_read_from_buffer(buf, count, ppos, puf_block_2, alloc_size_2);
}

static const struct file_operations puf_block_2_fops = {
    .owner = THIS_MODULE,
    .read = block_2_read,
};

static struct miscdevice puf_block_2_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = DEVICE_NAME_2,
    .fops = &puf_block_2_fops,
    .mode = 0444, // read-only
};

void free_blocks(void) {
    if (puf_block_1) {
        printk(KERN_INFO "Freeing %zu bytes of physically contiguous memory\n", alloc_size_1);
        free_pages((uint32_t) puf_block_1, get_order(alloc_size_1));
        puf_block_1 = 0x0;
    }

    if (puf_block_2) {
        printk(KERN_INFO "Freeing %zu bytes of physically contiguous memory\n", alloc_size_2);
        free_pages((uint32_t) puf_block_2, get_order(alloc_size_2));
        puf_block_2 = 0x0;
    }
}

static int __init alloc_start(void) {
    error err;

    if (alloc_size_1 != 0) {
        if (alloc_size_1 % ROW_SIZE != 0) {
            printk(KERN_ERR "alloc_size_1 needs to be a multiple of row size: %d\n", ROW_SIZE);
            return -1;
        }

        puf_block_1 = (void *) __get_free_pages(GFP_KERNEL, get_order(alloc_size_1));
        if (!puf_block_1) {
            printk(KERN_ERR "__get_free_pages() for block 1 failed!\n");
            return -ENOMEM;
        }

        printk(KERN_INFO "Allocated %zu bytes of physically contiguous memory\n", alloc_size_1);
        printk(KERN_INFO "Virtual address: %lx\n", (long unsigned int) puf_block_1);
        printk(KERN_INFO "Physical address: %llx\n", (unsigned long long)virt_to_phys((void *)puf_block_1));

        memset(puf_block_1, 0, alloc_size_1);

        err = misc_register(&puf_block_1_dev);
        if (err) {
            pr_err("Failed to register misc device\n");
            free_blocks();
            return err;
        }
    }

    if (alloc_size_2 != 0) {
        if (alloc_size_2 % ROW_SIZE != 0) {
            printk(KERN_ERR "alloc_size_2 needs to be a multiple of row size: %d\n", ROW_SIZE);
            free_blocks();
            return -1;
        }

        puf_block_2 = (void *) __get_free_pages(GFP_KERNEL, get_order(alloc_size_2));
        if (!puf_block_2) {
            free_blocks();
            printk(KERN_ERR "__get_free_pages() for block 2 failed!\n");
            return -ENOMEM;
        }

        printk(KERN_INFO "Allocated %zu bytes of physically contiguous memory\n", alloc_size_2);
        printk(KERN_INFO "Virtual address: %lx\n", (long unsigned int) puf_block_2);
        printk(KERN_INFO "Physical address: %llx\n", (unsigned long long)virt_to_phys((void *)puf_block_2));

        memset(puf_block_2, 0, alloc_size_2);

        err = misc_register(&puf_block_2_dev);
        if (err) {
            pr_err("Failed to register puf block 2");
            free_blocks();
            return err;
        }
    }
    
    return 0;
}

static void __exit alloc_end(void) {
    if (puf_block_1) {
        misc_deregister(&puf_block_1_dev);
    }

    if (puf_block_2) {
        misc_deregister(&puf_block_2_dev);
    }

    free_blocks();
}

module_init(alloc_start);
module_exit(alloc_end);

MODULE_AUTHOR("Matus Mrekaj");
MODULE_DESCRIPTION("Alloc Sys RAM");
MODULE_LICENSE("GPL");
