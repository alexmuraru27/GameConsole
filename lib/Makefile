BUILD_DIR = build

$(BUILD_DIR):
	mkdir $@		

# clean up
#######################################
clean:
	-rm -fR $(BUILD_DIR)

# dependencies
#######################################
-include $(wildcard $(BUILD_DIR)/*.d)
