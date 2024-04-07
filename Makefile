BOOTLOADER_DIR = bootloader
BOOTLOADER_BIN = $(BOOTLOADER_DIR)/build/Bootloader.bin
BOOTLOADER_ADDR = 0x8000000

APP_DIR = app
APP_BIN = $(APP_DIR)/build/App.bin
APP_ADDR = 0x8020000

# print color utils
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'


# debug build + opt
######################################
DEBUG = 1
# optimization
OPT = -Og -O0

# Build path
all: build_bootloader build_app

build_bootloader: 
	@echo -e ${BLUE}"Building Bootloader"${NC}
	cd $(BOOTLOADER_DIR)/ && make all

build_app:
	@echo -e ${BLUE}"Building App"${NC}
	cd $(APP_DIR)/ && make all


# clean up
#######################################
clean:
	cd $(BOOTLOADER_DIR)/ && make clean
	cd $(APP_DIR)/ && make clean

# openocd
#######################################
flash: all
	@echo -e ${GREEN}"Flashing Bootloader"${NC}
	openocd --debug=1 -f interface/stlink.cfg -f target/stm32f4x.cfg -c "program $(BOOTLOADER_BIN) verify reset exit $(BOOTLOADER_ADDR)"
	@echo -e ${GREEN}"Flashing App"${NC}
	openocd --debug=1 -f interface/stlink.cfg -f target/stm32f4x.cfg -c "program $(APP_BIN) verify reset exit $(APP_ADDR)"