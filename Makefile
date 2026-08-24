obj-m += rgb-led-driver.o

SRC := $(shell pwd)

.PHONY: all clean compile_commands

all: modules compile_commands

modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) clean

KBUILD_OUTPUT ?= $(BUILDDIR)/tmp/work-shared/beaglebone/kernel-build-artifacts
compile_commands: all
	python3 $(KERNEL_SRC)/scripts/clang-tools/gen_compile_commands.py -d $(PWD)
	sed -i 's#-I\./#-I$(KBUILD_OUTPUT)/#g' compile_commands.json