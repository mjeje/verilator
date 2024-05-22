LOCAL := $(abspath $(CURDIR)/BasicVerilogCounter)
BasicVerilogCounter_SSTELEMENT_BUILD_OBJS := $(patsubst %.cc, %.o, $(addprefix $(BUILD_DIR)/, $(notdir $(wildcard $(LOCAL)/*.cc))))
SSTELEMENT_BUILD_OBJS += $(BasicVerilogCounter_SSTELEMENT_BUILD_OBJS)
VERILOG_BUILD_OBJS += $(BUILD_DIR)/Counter.o

$(BUILD_DIR)/%.o: $(LOCAL)/%.v
	mkdir -p $(dir $@)
	$(VERILOG_BUILD_COMMAND)
	
$(BUILD_DIR)/%.o: $(LOCAL)/%.sv
	mkdir -p $(dir $@)
	$(VERILOG_BUILD_COMMAND)

$(BUILD_DIR)/%.o: $(LOCAL)/%.cc 
	mkdir -p $(dir $@)
	$(BUILD_COMMAND) -c $< -o $@