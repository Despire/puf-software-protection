obj-m := alloc_sysram.o
KDIR  := /lib/modules/$(shell uname -r)/build

all:
	make -C $(KDIR) M=$$PWD
		
clean:
	rm alloc_sysram.mod.c alloc_sysram.o modules.order alloc_sysram.mod.o Module.symvers built-in.o .alloc_sysram.ko.cmd .alloc_sysram.mod.o.cmd .built-in.o.cmd .alloc_sysram.o.cmd
