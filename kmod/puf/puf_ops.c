// PUF is in unused state.
// It completed all the decay requests from a program
// and waits for another program.
#define PUF_UNUSED   0x0
// PUF is in a decay state.
// After the the requested decay timeout times outs
// the PUF will be back in IDLE state.
#define PUF_DECAYING 0x1
// PUF was registerd with a certain PID, but
// is waiting for the enrollment data before starting.
#define PUF_WAITING_FOR_ENROLLMENT 0x2
// PUF was successfully generated and is waiting
// to be read the the process.
#define PUF_WAITING_FOR_READ       0x3

static uint32_t puf_size = 0x0;
module_param(puf_size, uint, 0);

static uint32_t puf_phys_addr = 0x0;
module_param(puf_phys_addr, uint, 0);

void*    puf_addr        = 0x0;

uint32_t temp_total      = 0x0;
uint32_t temp_poll_count = 0x0;

static struct delayed_work work_sdram;
static struct delayed_work work_temp_poll;

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
