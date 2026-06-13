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

flash:  $(BUILD_DIR)
	$(MAKE) -C Console flash

flashswo: flash
	./tools/scripts/swo.sh

deploy: $(BUILD_DIR)
	$(MAKE) -C GameXO deploy

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
