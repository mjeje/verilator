#
# src/sstelement/sstelement.mk
#

SST_CXX=$(shell sst-config --CXX)
SST_CXXFLAGS = $(shell sst-config --ELEMENT_CXXFLAGS)
SST_LDFLAGS = $(shell sst-config --ELEMENT_LDFLAGS)
SST_CPPFLAGS = -I$(BUILD_DIR)

SSTELEMENT_SRC_DIR = $(abspath $(CURDIR)/sstelement)
SSTELEMENT_SRC_SOURCES := $(wildcard $(SSTELEMENT_SRC_DIR)/*.cc)
SSTELEMENT_SOURCES := $(notdir $(SSTELEMENT_SRC_SOURCES))

$(info $$SSTELEMENT_SRC_DIR $(SSTELEMENT_SRC_DIR))
$(info $$SSTELEMENT_SRC_SOURCES $(SSTELEMENT_SRC_SOURCES))
$(info $$SSTELEMENT_SOURCES $(SSTELEMENT_SOURCES))

SSTELEMENT_BUILD_OBJS := $(patsubst %.cc, %.o, $(addprefix $(BUILD_DIR)/, $(SSTELEMENT_SOURCES)))

$(info $$SSTELEMENT_BUILD_OBJS $(SSTELEMENT_BUILD_OBJS))

SSTELEMENT_SRC_HEADERS := $(wildcard $(SSTELEMENT_SRC_DIR)/*.h)
SSTELEMENT_HEADERS := $(notdir $(SSTELEMENT_SRC_HEADERS))

$(info $$SSTELEMENT_SRC_HEADERS $(SSTELEMENT_SRC_HEADERS))
$(info $$SSTELEMENT_HEADERS $(SSTELEMENT_HEADERS))

SSTELEMENT_NAME = basicComponent
SSTELEMENT_LIB = lib$(SSTELEMENT_NAME)

$(info $$SSTELEMENT_NAME $(SSTELEMENT_NAME))
$(info $$SSTELEMENT_LIB $(SSTELEMENT_LIB))

SSTELEMENT_BUILD_LIB = $(BUILD_DIR)/$(SSTELEMENT_LIB)

$(info $$SSTELEMENT_BUILD_LIB $(SSTELEMENT_BUILD_LIB))

SSTELEMENT_INSTALL_HEADERS = $(addprefix $(INSTALL_DIR)/, $(SSTELEMENT_HEADERS))
SSTELEMENT_INSTALL_LIB = $(addprefix $(INSTALL_DIR)/, $(SSTELEMENT_LIB))

$(info $$SSTELEMENT_INSTALL_HEADERS $(SSTELEMENT_INSTALL_HEADERS))
$(info $$SSTELEMENT_INSTALL_LIB $(SSTELEMENT_INSTALL_LIB))
