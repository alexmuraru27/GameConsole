######################################
# REPO ROOT
######################################
# Path from a sub-makefile back to the repo root. Makefiles one level down
# (Console/, Bootloader/, Esp01s/) get the default `..`; deeper ones (the apps
# under Apps/<name>/) set REPO_ROOT = ../.. before including this file. Every
# shared path below is expressed against it, so the tree can be re-nested without
# editing each Makefile.
REPO_ROOT ?= ..

######################################
# DEPLOY
######################################
# Update-server content tree (the console will pull these over WiFi), resolved
# from the repo root so it works at any sub-makefile depth.
UPDATE_SERVER_CONTENT = $(REPO_ROOT)/tools/update_server/content

######################################
# OPENOCD
######################################
OPENOCD = "openocd"
OPENOCD_CFG = -f interface/stlink.cfg -f target/stm32f4x.cfg

######################################
# building variables
######################################
# debug build?
DEBUG = 1
# optimization
OPT = -Og

#######################################
# binaries
#######################################
PREFIX = arm-none-eabi-
# The gcc compiler bin path can be either defined in make command via GCC_PATH variable (> make GCC_PATH=xxx)
# either it can be added to the PATH environment variable.
ifdef GCC_PATH
CC = $(GCC_PATH)/$(PREFIX)gcc
AS = $(GCC_PATH)/$(PREFIX)gcc -x assembler-with-cpp
CP = $(GCC_PATH)/$(PREFIX)objcopy
SZ = $(GCC_PATH)/$(PREFIX)size
else
CC = $(PREFIX)gcc
AS = $(PREFIX)gcc -x assembler-with-cpp
CP = $(PREFIX)objcopy
SZ = $(PREFIX)size
endif
HEX = $(CP) -O ihex
BIN = $(CP) -O binary -S


#######################################
# CFLAGS
#######################################
# cpu
CPU = -mcpu=cortex-m4
# fpu
FPU = -mfpu=fpv4-sp-d16
# float-abi
FLOAT-ABI = -mfloat-abi=hard
# mcu
MCU = $(CPU) -mthumb $(FPU) $(FLOAT-ABI)