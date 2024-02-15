unsigned int PID              = 0;
uint8_t      puf_state        = PUF_UNUSED;
uint8_t*     enrollment_data  = 0x0;
uint8_t      enrollment_ptr   = 0x0; // points to elements to enrollment_data

static struct delayed_work work_stop_puf;

void stop_puf(struct work_struct *work) {
    end_puf();
    puf_state = PUF_WAITING_FOR_READ;
    printk(KERN_INFO "puf contents ready to be read");
}

void start_next_timeout() {
    uint16_t timeout = consume_timeout_be(&enrollment_ptr, enrollment_data);
    if (timeout == 0) {
        enrollment_ptr = 0;
        timeout = consume_timeout_be(&enrollment_ptr, enrollment_data);
    }
    puf_state = PUF_DECAYING;
    printk(KERN_INFO "PUF starting to decay\n");
    start_puf();
    schedule_delayed_work(&work_stop_puf, msecs_to_jiffies(timeout * 1000));
}

static int puf_open(struct inode *inode, struct file *file) {
    printk(KERN_INFO "puf opened by process with PID: %d\n", current->pid);
    if (PID == 0) {
        pid = current->pid;
        puf_state = PUF_WAITING_FOR_ENROLLMENT;
        printk(KERN_INFO "PUF waiting for enrollment data\n");
    } else {
        printk(KERN_ERR "%d trying to open PUF already beloning to %d\n", current->pid, PID);
        return -1;
    }
    return 0;
}

static int puf_release(struct inode * inode, struct file *file) {
    if (PID != 0) {
        printk(KERN_INFO "PID %d stopped using PUF\n", PID);
        PID = 0;
    }
    if (enrollment_data != 0x0) {
        printk(KERN_INFO "freeing enrollment_data\n");
        kfree(enrollment_data);
        enrollment_data = 0x0;
        enrollment_ptr = 0x0;
    }
    puf_state = PUF_UNUSED;
    printk(KERN_INFO "PUF is now unused");
    return 0;
}

static ssize_t puf_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
    uint32_t i = 0;
    uint32_t response = 0x0;
    uint32_t block_ptr = 0x0;
    uint32_t block = 0x0;
    uint32_t mask = 0x0;
    uint16_t memory = 0x0;
    uint32_t memory_offset = 0x0;
    uint8_t  bit = 0x0;

    if (puf_state == PUF_WAITING_FOR_READ) {
        for (i = 0; i < 32; i++) {
            block_ptr = consume_block_ptr_be(&enrollment_ptr, enrollment_data);

            block = block_ptr >> 4;
            mask = block_ptr & 0xf;

            memory_offset = block * sizeof(uint16_t);
            if (phys_r16(puf_phys_addr + memory_offset, &memory) != 0) {
                printk(KERN_ERR "failed to read physical RAM, aborting PUF read\n");
                return -EFAULT;
            }

            bit = memory & (1 << mask);
            if (bit != 0x0) {
                response |= 1 << i;
            }
        }

        // TODO: perform ECC on the response (needs parity bits alongside the enrollment).
        if (copy_to_user(buf, &response, sizeof(uint32_t)) != 0) {
            printk(KERN_ERR "failed to give response back to user\n");
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
    // and for each of them 32 pointers each 32bits wide is passed.
    // Example:
    // 10 32-(32bits) integers
    // 20 32-(32bits) integers
    // ...
    if (puf_state == PUF_WAITING_FOR_ENROLLMENT) {
        // we have 2 bytes for the timeout and 4 bytes for the pointers
        // the length of the enrollment devided by 32 should be either 0 or 16.
        if (!(count % (ENROLLMENT_POINTER_BYTES * 8) == 0 || count % (ENROLLMENT_POINTER_BYTES * 8) == 16)) {
            printk(KERN_ERR "invalid enrollment data\n");
            return -EFAULT;
        }
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
