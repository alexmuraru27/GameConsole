BOOTLOADER_DIR = bootloader
BOOTLOADER_BIN = $(BOOTLOADER_DIR)/build/Bootloader.bin
BOOTLOADER_ADDR = 0x8000000

SHARED_LIB_DIR = shared_lib
SHARED_LIB_BIN = $(SHARED_LIB_DIR)/build/SharedLib.bin
SHARED_LIB_ADDR = 0x8010000

APP_DIR = app
APP_BIN = $(APP_DIR)/build/App.bin
APP_ADDR = 0x8020000


# debug build + opt
######################################
DEBUG = 1
# optimization
OPT = -Og -O0

# Build path
all: build_prerequisites

build_prerequisites: build_bootloader build_app build_lib

build_bootloader: 
	cd $(BOOTLOADER_DIR)/ && make all

build_app:
	cd $(APP_DIR)/ && make all

build_lib:
	cd $(SHARED_LIB_DIR)/ && make all

# clean up
#######################################
clean:
	cd $(BOOTLOADER_DIR)/ && make clean
	cd $(APP_DIR)/ && make clean
	cd $(SHARED_LIB_DIR)/ && make clean

# openocd
#######################################
flash: all
	openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "program $(BOOTLOADER_BIN) verify reset exit $(BOOTLOADER_ADDR)"
	openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "program $(APP_BIN) verify reset exit $(APP_ADDR)"