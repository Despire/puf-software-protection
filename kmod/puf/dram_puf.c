#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/gfp.h>
#include <linux/dma-mapping.h>
#include <linux/miscdevice.h>

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

#define ROW_SIZE                2 * (1<<10)                   // bus_width * page_size
#define DEVICE_NAME             "puf"

typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef int error;

/////////////////////////////////////
//        Reed Solomon ECC   .     //
////////////////////////////////////
#define CONFIG_REED_SOLOMON_ENC8
#include "ecc/reed_solomon.c"

/////////////////////////////////////
//        Communication utils.     //
////////////////////////////////////
#include "protocol.c"

/////////////////////////////////////
//          BBB memory access.     //
////////////////////////////////////
#include "memory.c"

/////////////////////////////////////
//          Register Device.       //
////////////////////////////////////
#include "device.c"

/////////////////////////////////////
//          PUF related code.      //
////////////////////////////////////
#include "puf_ops.c"


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

static int __init puf_start(void) {
    INIT_DELAYED_WORK(&work_sdram, sdram_refresh);
    INIT_DELAYED_WORK(&work_temp_poll, temp_polling);
    INIT_DELAYED_WORK(&work_stop_puf, stop_puf);

    init_puf_buffer();
    start_temp_polling();
    return 0;
}

static void __exit puf_end(void) {
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
