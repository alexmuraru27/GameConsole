#######################################
# paths
#######################################
# Build path
BUILD_DIR = build
 
######################################
# targets
######################################
all: $(BUILD_DIR)
	$(MAKE) -C Console all
	$(MAKE) -C GameXO all
	$(MAKE) -C Shared all

flash:  $(BUILD_DIR)
	$(MAKE) -C Console flash
	$(MAKE) -C GameXO flash

$(BUILD_DIR):
	mkdir $@		
#######################################
# clean up
#######################################
clean:
	$(MAKE) -C Console clean
	$(MAKE) -C GameXO clean
	$(MAKE) -C Shared clean
	-rm -fR $(BUILD_DIR)
  
#######################################
# dependencies
#######################################
-include $(wildcard $(BUILD_DIR)/*.d)

# *** EOF ***
