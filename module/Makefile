obj-m += rgb-led-driver.o rotary-encoder-driver.o

SRC := $(shell pwd)


.PHONY: all clean compile_commands

all: modules

modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) clean


.ONESHELL:

RECIPE := rgb-led-module
KERNEL_SRC := $(BUILDDIR)/tmp/work-shared/beaglebone/kernel-source
KERNEL_WORK_BUILDDIR := $(BUILDDIR)/tmp/work/beaglebone-poky-linux-gnueabi/linux-bb.org/6.12.34+git/build
KERNEL_SRC_BUILDDIR := $(BUILDDIR)/tmp/work-shared/beaglebone/kernel-build-artifacts
CCGEN := $(KERNEL_SRC)/scripts/clang-tools/gen_compile_commands.py
B:=
compile_commands: B = $(shell bitbake-getvar -r $(RECIPE) B | sed -n 's/^B="\(.*\)"$$/\1/p')
compile_commands:
	python3 $(CCGEN) 				\
		-d=$(KERNEL_WORK_BUILDDIR) 	\
		-o=$(SRC)/cc.kernel.json 	\
		$(KERNEL_WORK_BUILDDIR)

	python3 $(CCGEN) 				\
		-d=$(KERNEL_SRC_BUILDDIR) 	\
		-o=$(SRC)/cc.module.json 	\
		$(B)
	
	jq -s 'add' cc.kernel.json cc.module.json > cc.tmp.json
	mv cc.tmp.json compile_commands.json
	rm cc.kernel.json cc.module.json
	