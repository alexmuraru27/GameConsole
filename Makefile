#######################################
# paths
#######################################
# Build path
BUILD_DIR = build
 
######################################
# targets
######################################
all: $(BUILD_DIR)
#   temporary disabled until reworking the memory management and the renderer/assets
	$(MAKE) -C Apps/GameXO all
	$(MAKE) -C Apps/TestRenderer all
	$(MAKE) -C Bootloader all
	$(MAKE) -C Console all
	$(MAKE) -C Shared all
	$(MAKE) -C Esp01s all

# Flash both stages over SWD: the bootloader (sector 0) then the application
# (sectors 1-5). Each programs only its own region. After this the console boots
# the bootloader, which finds no pending update and runs the app.
flash:  $(BUILD_DIR)
	$(MAKE) -C Bootloader flash
	$(MAKE) -C Console flash

flashswo: flash
	./tools/scripts/swo.sh

# Stage everything the console can pull into the update-server content tree: the
# game (+ .pak) under games/, and the console OS image (Firmware/Console.bin) plus
# the ESP-01S firmware (Firmware/ESP01.bin, if built) under the shared Firmware/
# category. The bootloader is never self-updated, so it is not staged.
deploy: $(BUILD_DIR)
	$(MAKE) -C Apps/GameXO deploy
	$(MAKE) -C Apps/TestRenderer deploy
	$(MAKE) -C Console deploy
	$(MAKE) -C Esp01s deploy

# Build the ESP-01S WiFi-module firmware (separate PlatformIO target, see Esp01s/).
esp:
	$(MAKE) -C Esp01s all

$(BUILD_DIR):
	mkdir $@		
#######################################
# clean up
#######################################
clean:
#   temporary disabled until reworking the memory management and the renderer/assets
# 	$(MAKE) -C Console clean
	$(MAKE) -C Apps/GameXO clean
	$(MAKE) -C Apps/TestRenderer clean
	$(MAKE) -C Shared clean
	-rm -fR $(BUILD_DIR)

#######################################
# dependencies
#######################################
-include $(wildcard $(BUILD_DIR)/*.d)

# *** EOF ***
