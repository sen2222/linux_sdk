TARGET = kiva_project
BUILD_DIR = build

CROSS_COMPILE = $(abspath ../../../system/tool/compile-chain/bin/arm-linux-gnueabihf-)

CC = $(CROSS_COMPILE)gcc
CXX = $(CROSS_COMPILE)g++
LD = $(CROSS_COMPILE)ld
AR = $(CROSS_COMPILE)ar
STRIP = $(CROSS_COMPILE)strip

CFLAGS =  -O2 -mcpu=cortex-a7 -mfpu=vfpv3-d16 -mfloat-abi=hard  -std=gnu99 -DLINUX -Wno-unused-function
CXXFLAGS = $(filter-out -std=gnu99,$(CFLAGS)) -std=gnu++11


LDFLAGS = -Wl,-Bdynamic -lpthread -lm -ldl -lrt -lstdc++

SYSTEM_ROOT = system
HW_DRIVE_ROOT = hw_drive
APPLICATION_ROOT = application
SOURCE_LIB_ROOT = lib



# hw_drive模块定义
MODULES_HW_DRIVE = \
    touch \
	camera \


# application模块定义
MODULES_APPLICATION = \
    lvgl_port \
	app_view \
	ds \
  

LIBS =
INCLUDEPATH_ALL = . $(SYSTEM_ROOT) $(HW_DRIVE_ROOT) $(APPLICATION_ROOT)



# LVGL 库
LVGL_LIB_PATH = ${SOURCE_LIB_ROOT}/lvgl
LIBS += -L$(LVGL_LIB_PATH)/lib \
        -llvgl
INCLUDEPATH_ALL += $(LVGL_LIB_PATH)/include
CFLAGS += -DLV_CONF_INCLUDE_SIMPLE -DLV_KCONFIG_IGNORE

# curl
CURL_LIB_PATH = ${SOURCE_LIB_ROOT}/curl
LIBS += -L$(CURL_LIB_PATH)/lib \
        -lcurl \
		-lmbedtls \
		-lmbedx509 \
		-lmbedcrypto \
		-lz
INCLUDEPATH_ALL += $(CURL_LIB_PATH)/include


SOURCES_ALL = main.c $(wildcard $(SYSTEM_ROOT)/*.c $(HW_DRIVE_ROOT)/*.c $(APPLICATION_ROOT)/*.c)
SOURCES_CXX_ALL = $(wildcard $(SYSTEM_ROOT)/*.cpp $(HW_DRIVE_ROOT)/*.cpp $(APPLICATION_ROOT)/*.cpp)

SOURCES_SYSTEM = $(foreach module,$(MODULES_SYSTEM),\
    $(wildcard $(SYSTEM_ROOT)/$(module)/*.c) \
)

SOURCES_HW_DRIVE = $(foreach module,$(MODULES_HW_DRIVE),\
    $(wildcard $(HW_DRIVE_ROOT)/$(module)/*.c) \
)

SOURCES_APPLICATION = $(foreach module,$(MODULES_APPLICATION),\
    $(wildcard $(APPLICATION_ROOT)/$(module)/*.c) \
)

SOURCES_CXX_SYSTEM = $(foreach module,$(MODULES_SYSTEM),\
    $(wildcard $(SYSTEM_ROOT)/$(module)/*.cpp) \
)

SOURCES_CXX_HW_DRIVE = $(foreach module,$(MODULES_HW_DRIVE),\
    $(wildcard $(HW_DRIVE_ROOT)/$(module)/*.cpp) \
)

SOURCES_CXX_APPLICATION = $(foreach module,$(MODULES_APPLICATION),\
    $(wildcard $(APPLICATION_ROOT)/$(module)/*.cpp) \
)

SOURCES_ALL += $(SOURCES_SYSTEM) $(SOURCES_HW_DRIVE) $(SOURCES_APPLICATION)
SOURCES_CXX_ALL += $(SOURCES_CXX_SYSTEM) $(SOURCES_CXX_HW_DRIVE) $(SOURCES_CXX_APPLICATION)

ifneq ($(strip $(SOURCES_CXX_ALL)),)
$(info Found C++ source files, using g++ for linking)
CC_LINK = $(CXX)
else
CC_LINK = $(CC)
endif

INCLUDEPATH_SYSTEM = $(foreach module,$(MODULES_SYSTEM),\
    $(wildcard $(SYSTEM_ROOT)/$(module)) \
    $(SYSTEM_ROOT)/$(module) \
)

INCLUDEPATH_HW_DRIVE = $(foreach module,$(MODULES_HW_DRIVE),\
    $(wildcard $(HW_DRIVE_ROOT)/$(module)) \
    $(HW_DRIVE_ROOT)/$(module) \
)

INCLUDEPATH_APPLICATION = $(foreach module,$(MODULES_APPLICATION),\
    $(wildcard $(APPLICATION_ROOT)/$(module)) \
    $(APPLICATION_ROOT)/$(module) \
)

INCLUDEPATH_ALL += $(INCLUDEPATH_SYSTEM) $(INCLUDEPATH_HW_DRIVE) $(INCLUDEPATH_APPLICATION)
INCLUDEPATH_ALL := $(sort $(INCLUDEPATH_ALL))

OBJECTS_C = $(patsubst %.c,$(BUILD_DIR)/%.o,$(filter %.c,$(SOURCES_ALL)))
OBJECTS_CXX = $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(filter %.cpp,$(SOURCES_CXX_ALL)))
OBJECTS = $(OBJECTS_C) $(OBJECTS_CXX)
INCLUDES = $(addprefix -I,$(INCLUDEPATH_ALL))

$(info ========================================)
$(info Target: $(TARGET))
$(info Architecture: ARM (Cortex-A7))
$(info C source files: $(words $(SOURCES_ALL)))
$(info C++ source files: $(words $(SOURCES_CXX_ALL)))
$(info ========================================)

all: prepare $(TARGET)
	@echo "Build completed: $(TARGET)"
	@$(STRIP) $(TARGET) 2>/dev/null || true
	@echo "Stripped binary"

prepare:
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(dir $(OBJECTS))

$(TARGET): $(OBJECTS) $(LVGL_LIB_PATH)/lib/liblvgl.a
	$(CC_LINK) -o $@ $(OBJECTS) $(LIBS) $(LDFLAGS)
	@echo "  LD    $@"

$(BUILD_DIR)/%.o: %.c $(LVGL_LIB_PATH)/include/lvgl/config/lv_conf.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@
	@echo "  CC    $<"

$(BUILD_DIR)/%.o: %.cpp $(LVGL_LIB_PATH)/include/lvgl/config/lv_conf.h
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@
	@echo "  CXX   $<"

clean:
	@rm -rf $(BUILD_DIR) $(TARGET)
	@echo "Cleaned."
