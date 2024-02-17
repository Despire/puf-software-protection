unsigned int PID              = 0;
uint8_t      puf_state        = PUF_UNUSED;
uint8_t*     enrollment_data  = 0x0;
uint8_t      enrollment_ptr   = 0x0; // points to elements to enrollment_data

static struct delayed_work work_stop_puf;

void stop_puf(struct work_struct *work) {
    end_puf();
    puf_state = PUF_WAITING_FOR_READ;
    printk(KERN_INFO "decay finished, contents ready to be read\n");
}

void device_cleanup(void) {
    if (cancel_delayed_work_sync(&work_stop_puf)) {
        end_puf();
    }
    printk(KERN_INFO "device: canceled all delayed work\n");

    if (PID != 0) {
        printk(KERN_INFO "PID %d stopped using PUF\n", PID);
        PID = 0;
    }

    if (enrollment_data != 0x0) {
        printk(KERN_INFO "freeing enrollment_data\n");
        kfree(enrollment_data);
        enrollment_data = 0x0;
        enrollment_ptr  = 0x0;
    }
    puf_state = PUF_UNUSED;
    printk(KERN_INFO "device: cleanup ok, PUF is now unused\n");

}

void start_next_timeout(void) {
    uint16_t timeout = consume_timeout_be(&enrollment_ptr, enrollment_data);
    if (timeout == 0) {
        enrollment_ptr = 0;
        timeout = consume_timeout_be(&enrollment_ptr, enrollment_data);
    }
    puf_state = PUF_DECAYING;
    start_puf();
    schedule_delayed_work(&work_stop_puf, msecs_to_jiffies(timeout * 1000));
    printk(KERN_INFO "PUF starting to decay for: %dsec\n", timeout);
}

static int puf_open(struct inode *inode, struct file *file) {
    printk(KERN_INFO "puf opened by process with PID: %d\n", current->pid);
    if (PID == 0) {
        PID = current->pid;
        puf_state = PUF_WAITING_FOR_ENROLLMENT;
        printk(KERN_INFO "PUF waiting for enrollment data\n");
    } else {
        printk(KERN_ERR "%d trying to open PUF already beloning to %d\n", current->pid, PID);
        return -1;
    }
    return 0;
}

static int puf_release(struct inode * inode, struct file *file) {
    device_cleanup();
    return 0;
}

static ssize_t puf_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
    uint32_t    	i                  = 0;
    uint32_t    	response           = 0x0;
    uint32_t    	block_ptr          = 0x0;
    uint32_t    	block              = 0x0;
    uint32_t    	mask               = 0x0;
    uint16_t    	memory             = 0x0;
    uint32_t    	memory_offset      = 0x0;
    uint8_t     	parity             = 0x0;
    uint16_t*   	parity_array       = 0x0;
    uint8_t     	recovered_bits[32] = {0x0};
    struct rs_control*	rs_ctrl            = NULL;

    if (puf_state == PUF_WAITING_FOR_READ) {
        parity = consume_parity_be(&enrollment_ptr, enrollment_data);
        parity_array = (uint16_t *) kzalloc(parity * sizeof(uint16_t), GFP_KERNEL);
	rs_ctrl = init_rs(8, 0x11d, 0, 1, parity);
        for (i = 0; i < 32; i++) {
            block_ptr = consume_block_ptr_be(&enrollment_ptr, enrollment_data);

            block = block_ptr >> 4;
            mask = block_ptr & 0xf;

            memory_offset = block * sizeof(uint16_t);
            if (phys_r16(puf_phys_addr + memory_offset, &memory) != 0) {
                printk(KERN_ERR "failed to read physical RAM, aborting PUF read\n");
                kfree(parity_array);
		free_rs(rs_ctrl);
                start_next_timeout();
                return -EFAULT;
            }

            recovered_bits[i] = memory & (1 << mask);
        }

        if (decode_rs8(rs_ctrl, recovered_bits, parity_array, 32, NULL, 0, NULL, 0, NULL) < 0) {
            printk(KERN_ERR "failed to apply ECC\n");
            kfree(parity_array);
	    free_rs(rs_ctrl);
            start_next_timeout();
            return -EFAULT;
        }

        for (i = 0; i < 32; i++) {
            if (recovered_bits[i] != 0x0) {
                response |= 1 << i;
            }
        }

        if (copy_to_user(buf, &response, sizeof(uint32_t)) != 0) {
            printk(KERN_ERR "failed to give response back to user\n");
            kfree(parity_array);
	    free_rs(rs_ctrl);
            start_next_timeout();
            return -EFAULT;
        }

        start_next_timeout();
        return sizeof(uint32_t);
    }

    return 0;
}

static ssize_t puf_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
    size_t i = 0;
    uint8_t* data = 0;

    data = (uint8_t *) kzalloc(count, GFP_KERNEL);
    if (!data) {
        printk(KERN_ERR "Failed to alloc buffer for user provided data\n");
        return -ENOMEM;
    }

    if (copy_from_user(data, buf, count)) {
        printk(KERN_ERR "Failed to copy provided data from user space into kernel space\n");
        kfree(data);
        return -EFAULT;
    }

    printk(KERN_INFO "Data written to device");
    for (i = 0; i < count; i++) {
        printk(KERN_CONT " %02x", data[i]);
    }
    printk(KERN_CONT "\n");

    // Enrollment consists of multiple decay timeouts
    // of the following format
    // |(16bit)timeout|(8bit)parity-bit-count|(16bit)16 integers|(32bit) 32 integers
    // Example:
    // 10|3|3-(16bits)integers|32-(32bits) integers
    // 20|3|3-(16bits)integers|32-(32bits) integers
    // ...
    if (puf_state == PUF_WAITING_FOR_ENROLLMENT) {
        printk(KERN_INFO "received enrollment data\n");
        enrollment_data = (uint8_t *) kzalloc(count + ENROLLMENT_TIMEOUT_BYTES, GFP_KERNEL); // +extra bytes for signaling a timeout of 0 (i.e EOF).
        if (!enrollment_data) {
            printk(KERN_ERR "Failed to alloc buffer for user provided data\n");
            return -ENOMEM;
        }
        memcpy(enrollment_data, data, count);

        start_next_timeout();
    }

    kfree(data);
    return count;
}

static const struct file_operations puf_fops = {
    .owner   = THIS_MODULE,
    .open    = puf_open,
    .read    = puf_read,
    .write   = puf_write,
    .release = puf_release,
};

static struct miscdevice puf_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = DEVICE_NAME,
    .fops  = &puf_fops,
};
