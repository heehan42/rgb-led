obj-m += rgb-led-driver.o

SRC := $(shell pwd)

.PHONY: all clean compile_commands

all: modules

modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) clean



CCGEN = $(KERNEL_SRC)/scripts/clang-tools/gen_compile_commands.py
KERNEL_WORK_BUILDDIR = $(BUILDDIR)/tmp/work/beaglebone-poky-linux-gnueabi/linux-bb.org/6.12.34+git/build
KERNEL_SRC_BUILDDIR = $(BUILDDIR)/tmp/work-shared/beaglebone/kernel-build-artifacts
compile_commands:
	python3 $(CCGEN)								\
			-d=$(KERNEL_WORK_BUILDDIR)				\
			-o=$(SRC)/cc.kernel.json				\
			$(KERNEL_WORK_BUILDDIR)

	python3 $(CCGEN)								\
			-d=$(KERNEL_SRC_BUILDDIR)				\
			-o=$(SRC)/cc.module.json				\
			$(SRC)
	
	jq -s 'add' cc.kernel.json cc.module.json > cc.tmp.json
	mv cc.tmp.json compile_commands.json
	rm cc.kernel.json cc.module.json
	