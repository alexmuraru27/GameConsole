BOOTLOADER_DIR = ./bootloader
BOOTLOADER_ELF = $(BOOTLOADER_DIR)/build/Bootloader.elf

APP_DIR = ./app
APP_ELF = $(APP_DIR)/build/Bomberman.elf

SHARED_LIB_DIR = ./shared_lib
SHARED_LIB_ELF = $(SHARED_LIB_DIR)/build/SharedLib.elf

BUILD_DIR = build

# debug build + opt
######################################
DEBUG = 1
# optimization
OPT = -Og -O0

# Build path

all: build_prerequisites

build_prerequisites: $(BUILD_DIR) build_bootloader build_app build_lib

$(BUILD_DIR):
	mkdir $@

build_bootloader: 
	cd $(BOOTLOADER_DIR)/ && make all

build_app:
	cd $(APP_DIR)/ && make all

build_lib:
	cd $(SHARED_LIB_DIR)/ && make all

# clean up
#######################################
clean:
	-rm -fR $(BUILD_DIR)
	cd $(BOOTLOADER_DIR)/ && make clean
	cd $(APP_DIR)/ && make clean
	cd $(SHARED_LIB_DIR)/ && make clean

# openocd
#######################################
flash: all
	openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "program $(BOOTLOADER_ELF) verify reset exit"
