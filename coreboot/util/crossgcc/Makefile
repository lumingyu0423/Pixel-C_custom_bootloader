##
## This file is part of the coreboot project.
##
## This Makefile downloads pre-built Linaro GCC toolchains for
## ARM (elf32-littlearm) and AARCH64 (elf64-littleaarch64).
##

XGCC_DIR := xgcc

# Linaro GCC toolchain download URLs
ARM_EABI_URL := https://releases.linaro.org/components/toolchain/binaries/4.9-2017.01/arm-eabi/gcc-linaro-4.9.4-2017.01-x86_64_arm-eabi.tar.xz
AARCH64_ELF_URL := https://releases.linaro.org/components/toolchain/binaries/4.9-2017.01/aarch64-elf/gcc-linaro-4.9.4-2017.01-x86_64_aarch64-elf.tar.xz

# Linaro GCC toolchain directory names
ARM_EABI_DIR := gcc-linaro-4.9.4-2017.01-x86_64_arm-eabi
AARCH64_ELF_DIR := gcc-linaro-4.9.4-2017.01-x86_64_aarch64-elf

all: build

build: $(XGCC_DIR)/$(ARM_EABI_DIR) $(XGCC_DIR)/$(AARCH64_ELF_DIR)
	@echo "Linaro GCC toolchains are ready."

$(XGCC_DIR)/$(ARM_EABI_DIR):
	@echo "Downloading ARM (arm-eabi) toolchain..."
	mkdir -p $(XGCC_DIR)
	wget -q --show-progress -O $(XGCC_DIR)/$(ARM_EABI_DIR).tar.xz $(ARM_EABI_URL)
	@echo "Extracting ARM toolchain..."
	tar -xf $(XGCC_DIR)/$(ARM_EABI_DIR).tar.xz -C $(XGCC_DIR)
	rm -f $(XGCC_DIR)/$(ARM_EABI_DIR).tar.xz

$(XGCC_DIR)/$(AARCH64_ELF_DIR):
	@echo "Downloading AARCH64 (aarch64-elf) toolchain..."
	mkdir -p $(XGCC_DIR)
	wget -q --show-progress -O $(XGCC_DIR)/$(AARCH64_ELF_DIR).tar.xz $(AARCH64_ELF_URL)
	@echo "Extracting AARCH64 toolchain..."
	tar -xf $(XGCC_DIR)/$(AARCH64_ELF_DIR).tar.xz -C $(XGCC_DIR)
	rm -f $(XGCC_DIR)/$(AARCH64_ELF_DIR).tar.xz

clean:
	rm -rf $(XGCC_DIR)

.PHONY: all build clean
