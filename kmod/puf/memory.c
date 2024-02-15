#pragma GCC push_options
#pragma GCC optimize ("O2")
int hw_reg_r32(uint32_t addr, uint32_t *out) {
    uint32_t *virt_addr = (uint32_t *) ioremap(addr, sizeof(uint32_t));
    if (!virt_addr) {
        printk(KERN_ERR "failed to read physical address=0x%08x\n", addr);
        return -1;
    }

    *out = *virt_addr;
    iounmap((void *) virt_addr);

    return 0;
}

int hw_reg_w32(uint32_t addr, uint32_t val) {
    uint32_t *virt_addr = (uint32_t *) ioremap(addr, sizeof(uint32_t));
    if (!virt_addr) {
        printk(KERN_ERR "failed to read physical address=0x%08x\n", addr);
        return -1;
    }

    *virt_addr = val;
    iounmap((void *) virt_addr);

    return 0;
}

int phys_r32(uint32_t addr, uint32_t *out) {
    uint32_t *virt_addr = (uint32_t *) phys_to_virt(addr);
    if (!virt_addr) {
        printk(KERN_ERR "failed to read physical address=0x%08x\n", addr);
        return -1;
    }

    *out = *virt_addr;

    return 0;
}

int phys_w32(uint32_t addr, uint32_t value) {
    uint32_t *virt_addr = (uint32_t *) phys_to_virt(addr);
    if (!virt_addr) {
        printk(KERN_ERR "failed to read physical address=0x%08x\n", addr);
        return -1;
    }

    *virt_addr = value;

    return 0;
}

int phys_r16(uint32_t addr, uint16_t *out) {
    uint16_t *virt_addr = (uint16_t *) phys_to_virt(addr);
    if (!virt_addr) {
        printk(KERN_ERR "failed to read physical address=0x%08x\n", addr);
        return -1;
    }

    *out = *virt_addr;

    return 0;
}

#pragma GCC pop_options
