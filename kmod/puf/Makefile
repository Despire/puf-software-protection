obj-m := dram_puf.o
KDIR  := /lib/modules/$(shell uname -r)/build

all:
	make -C $(KDIR) M=$$PWD
		
clean:
	rm dram_puf.mod dram_puf.mod.c dram_puf.o modules.order dram_puf.mod.o Module.symvers built-in.o .built-in.o.cmd .dram_puf.ko.cmd .dram_puf.mod.o.cmd .dram_puf.o.cmd .dram_puf.mod.cmd .Module.symvers.cmd .dram_puf.o.d .module.order.cmd
