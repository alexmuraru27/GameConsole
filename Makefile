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
	$(MAKE) -C GameXO all
	$(MAKE) -C Console all
	$(MAKE) -C Shared all
	$(MAKE) -C Esp01s build

flash:  $(BUILD_DIR)
	$(MAKE) -C Console flash

flashswo: flash
	./tools/scripts/swo.sh

# Stage everything the console can pull into the update-server content tree:
# the game (+ .pak) and the ESP-01S firmware (if it's been built).
deploy: $(BUILD_DIR)
	$(MAKE) -C GameXO deploy
	$(MAKE) -C Esp01s deploy

# Build the ESP-01S WiFi-module firmware (separate PlatformIO target, see Esp01s/).
esp:
	$(MAKE) -C Esp01s build

$(BUILD_DIR):
	mkdir $@		
#######################################
# clean up
#######################################
clean:
#   temporary disabled until reworking the memory management and the renderer/assets
# 	$(MAKE) -C Console clean
	$(MAKE) -C GameXO clean
	$(MAKE) -C Shared clean
	-rm -fR $(BUILD_DIR)

#######################################
# dependencies
#######################################
-include $(wildcard $(BUILD_DIR)/*.d)

# *** EOF ***
