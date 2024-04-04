BOOTLOADER_DIR = ./bootloader
BOOTLOADER_ELF = $(BOOTLOADER_DIR)/build/Bootloader.elf

BUILD_DIR = build

# debug build + opt
######################################
DEBUG = 1
# optimization
OPT = -Og -O0

# Build path

$(BUILD_DIR):
	mkdir $@

# clean up
#######################################
clean:
	-rm -fR $(BUILD_DIR)
	cd $(BOOTLOADER_DIR)/ && make clean
	cd ./app/ && make clean
	cd ./lib/ && make clean

# openocd
#######################################
flash: all
	openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "program $(BOOTLOADER_ELF) verify reset exit"

# dependencies
#######################################
-include $(wildcard $(BUILD_DIR)/*.d)
