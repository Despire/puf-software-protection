#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/gfp.h>
#include <linux/dma-mapping.h>

#define CONFIG_REED_SOLOMON_ENC8
#include "ecc/reed_solomon.c"

#define CNTRL_MOD_REG               0x44E10000                // Control Module registers offset.
#define BANDGAP_CTRL                CNTRL_MOD_REG + 0x0448    // Bandgap control register for reading temperature.
#define BANDGAP_CTRL_DTEMP_OFF      8                         // Offset for temperature reading.
#define BANDGAP_CTRL_DTEMP_MASK     0x0000FF00                // Bits used for temperature.
#define BANDGAP_CTRL_TMPOFF         BIT(5)
#define BANDGAP_CTRL_SOC            BIT(4)                    
#define BANDGAP_CTRL_CLRZ           BIT(3)
#define BANDGAP_CTRL_CONTCONV       BIT(2)

#define TEMP_POLL_PERIOD        2                             // Number of seconds between individual temperature reads.

#define EMIF0_REG               0x4C000000                    // EMIF0 register base.
#define SDRAM_REF_CTRL          EMIF0_REG + 0x10              // Offset for the register of the SDRAM REFRESH CONTROL register.
#define SDRAM_REF_CTRL_SDHW     EMIF0_REG + 0x14              // Offset for shadow register of the SDRAM REFRESH CONTROL register.

#define DDR_END                 0x9fdfffff                    // SDRAM end for CPU.
#define DDR_BASE                0x80000000                    // SDRAM base for CPU.

#define REFRESH_PERIOD          55                            // Refresh period for custom SDRAM refresh.
#define DISABLE_REFRESH         BIT(31)                       // Bit to flip to disable/enable SDRAM refresh.

#define ROW_SIZE                sizeof(unsigned int) * (1<<9) // word_size * page size
#define DEVICE_NAME             "puf"

typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef int error;

static uint32_t puf_size = 0x0;
module_param(puf_size, uint, 0);

uint32_t puf_phys_addr   = 0x0;
void*    puf_addr        = 0x0;

uint32_t temp_total      = 0x0;
uint32_t temp_poll_count = 0x0;

static struct delayed_work work_sdram;
static struct delayed_work work_temp_poll;

/////////////////////////////////////
//          Register Device.       //
////////////////////////////////////
#include "device.c"

/////////////////////////////////////
//          PUF related code.      //
////////////////////////////////////
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

#pragma GCC push_options
#pragma GCC optimize ("O2")
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
#pragma GCC pop_options

void temp_polling(struct work_struct *work) {
    uint32_t tctrl, temp;
    struct delayed_work *dwork;

    dwork = to_delayed_work(work);
    schedule_delayed_work(dwork, msecs_to_jiffies(TEMP_POLL_PERIOD * 1000));

    hw_reg_r32(BANDGAP_CTRL, &tctrl);
    temp = tctrl & BANDGAP_CTRL_DTEMP_MASK;
    temp >>= BANDGAP_CTRL_DTEMP_OFF;

    temp_total      += temp;
    temp_poll_count += 1;
}

void sdram_refresh(struct work_struct *work) {
    uint32_t discard, offset;
    uint32_t puf_beg, puf_end;

    struct delayed_work *dwork;

    dwork = to_delayed_work(work);
    schedule_delayed_work(dwork, msecs_to_jiffies(REFRESH_PERIOD));

    puf_beg = puf_phys_addr;
    puf_end = puf_phys_addr + puf_size;

    for (offset = DDR_BASE; offset <= DDR_END; offset += ROW_SIZE) {
        if (offset >= puf_beg && offset < puf_end) {
            offset = puf_end - ROW_SIZE;
            continue;
        }
        phys_r32(offset, &discard);
    }
}

void disable_sdram_refresh(void) {
    uint32_t rctrl, rctrl_sdhw;
    hw_reg_r32(SDRAM_REF_CTRL, &rctrl);
    hw_reg_r32(SDRAM_REF_CTRL_SDHW, &rctrl_sdhw);

    printk(KERN_INFO "DISABLE, RCTRL=0x%08x\n", rctrl);
    printk(KERN_INFO "DISABLE, RCTRL_SDHW=0x%08x\n", rctrl_sdhw);

    rctrl |= DISABLE_REFRESH;
    rctrl_sdhw |= DISABLE_REFRESH;

    printk(KERN_INFO "DISABLE, RCTRL=0x%08x\n", rctrl);
    printk(KERN_INFO "DISABLE, RCTRL_SDHW=0x%08x\n", rctrl_sdhw);

    hw_reg_w32(SDRAM_REF_CTRL, rctrl);
    hw_reg_w32(SDRAM_REF_CTRL_SDHW, rctrl_sdhw);
}

void enable_sdram_refresh(void) {
    uint32_t rctrl, rctrl_sdhw;
    hw_reg_r32(SDRAM_REF_CTRL, &rctrl);
    hw_reg_r32(SDRAM_REF_CTRL_SDHW, &rctrl_sdhw);

    printk(KERN_INFO "ENABLE, RCTRL=0x%08x\n", rctrl);
    printk(KERN_INFO "ENABLE, RCTRL_SDHW=0x%08x\n", rctrl_sdhw);

    rctrl &= ~DISABLE_REFRESH;
    rctrl_sdhw &= ~DISABLE_REFRESH;

    printk(KERN_INFO "ENABLE, RCTRL=0x%08x\n", rctrl);
    printk(KERN_INFO "ENABLE, RCTRL_SDHW=0x%08x\n", rctrl_sdhw);

    hw_reg_w32(SDRAM_REF_CTRL, rctrl);
    hw_reg_w32(SDRAM_REF_CTRL_SDHW, rctrl_sdhw);
}

void start_puf(void) {
    disable_sdram_refresh();
    schedule_delayed_work(&work_sdram, msecs_to_jiffies(REFRESH_PERIOD));
}

void start_temp_polling(void) {
    uint32_t tctrl;

    tctrl = BANDGAP_CTRL_SOC | BANDGAP_CTRL_CLRZ | BANDGAP_CTRL_CONTCONV;
    hw_reg_w32(BANDGAP_CTRL, tctrl);

    schedule_delayed_work(&work_temp_poll, msecs_to_jiffies(TEMP_POLL_PERIOD * 1000));
}

void deinit_puf_buffer(void) {
    if (puf_addr) {
        printk(KERN_INFO "Freeing %zu bytes of physically contiguous memory\n", puf_size);
        free_pages((uint32_t) puf_addr, get_order(puf_size));
        puf_addr = 0x0;
    }
}

int init_puf_buffer(void) {
    error err;

    if (puf_size % ROW_SIZE != 0) {
        printk(KERN_ERR "puf_size needs to be multiple of row size (%d)\n", ROW_SIZE);
        return -ENOMEM;
    }

    puf_addr = (void *) __get_free_pages(GFP_KERNEL, get_order(puf_size));
    if (!puf_addr) {
        printk(KERN_ERR "__get_free_pages() for puf buffer failed!\n");
        return -ENOMEM;
    }

    printk(KERN_INFO "Allocated %zu bytes of physically contiguous memory\n", puf_size);

    memset(puf_addr, 0, puf_size);

    err = misc_register(&puf_device);
    if (err) {
        pr_err("Failed to register puf device\n");
        deinit_puf_buffer();
        return err;
    }

    return 0;
}

void stop_temp_polling(void) {
    uint32_t tctrl;

    cancel_delayed_work_sync(&work_temp_poll);

    tctrl = BANDGAP_CTRL_TMPOFF;
    hw_reg_w32(BANDGAP_CTRL, tctrl);

    printk(KERN_INFO "Temp Total=%d\n", temp_total);
    printk(KERN_INFO "Poll count=%d\n", temp_poll_count);
}

void end_puf(void) {
    enable_sdram_refresh();
    cancel_delayed_work_sync(&work_sdram);
}

static int __init puf_start(void) {
    INIT_DELAYED_WORK(&work_sdram, sdram_refresh);
    INIT_DELAYED_WORK(&work_temp_poll, temp_polling);

    init_puf_buffer();
    start_temp_polling();
    start_puf();

    return 0;
}

static void __exit puf_end(void) {
    end_puf();
    stop_temp_polling();

    if (puf_addr) {
        misc_deregister(&puf_device);
    }
    deinit_puf_buffer();
}

module_init(puf_start);
module_exit(puf_end);

MODULE_AUTHOR("Matus Mrekaj");
MODULE_DESCRIPTION("DRAM Physically Unclonable Function");
MODULE_LICENSE("GPL");
