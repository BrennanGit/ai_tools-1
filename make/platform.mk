TARGET := xcore
BIN_DIR := bin

ifeq ($(TARGET), x86)
	PLATFORM_FLAGS := -g
	PLATFORM_FLAGS += -O3
	PLATFORM_FLAGS += -DTF_LITE_DISABLE_X86_NEON
	PLATFORM_FLAGS += -DENABLE_TRACING

	CC := gcc
	CCFLAGS := $(PLATFORM_FLAGS)

	CXX := g++
	CXXFLAGS := $(PLATFORM_FLAGS)
	CXXFLAGS += -std=c++11 

	LDFLAGS  := -L/usr/local/lib -lstdc++ -lm -pthread

	OBJ_DIR := .build/x86
else # must be xcore
	PLATFORM_FLAGS := -target=XCORE-AI-EXPLORER
	PLATFORM_FLAGS += -mcmodel=large
	PLATFORM_FLAGS += -Os
	PLATFORM_FLAGS += -DXCORE
	PLATFORM_FLAGS += -Wno-xcore-fptrgroup
	PLATFORM_FLAGS += -Wno-unknown-pragmas
	#PLATFORM_FLAGS += -Wno-unknown-attributes
	PLATFORM_FLAGS += -report

	AS := xcc
	ASFLAGS := $(PLATFORM_FLAGS)

	CC := xcc
	CCFLAGS := $(PLATFORM_FLAGS)
	CCFLAGS += -DTF_LITE_STATIC_MEMORY
	CCFLAGS += -DTF_LITE_STRIP_ERROR_STRINGS

	CXX := xcc
	CXXFLAGS := $(PLATFORM_FLAGS)
	CXXFLAGS += -std=c++11
	CXXFLAGS += -DTF_LITE_STATIC_MEMORY
	CXXFLAGS += -DTF_LITE_STRIP_ERROR_STRINGS

	FLASH := xflash

	LDFLAGS := $(PLATFORM_FLAGS) -lquadspi

	OBJ_DIR := .build/xcore
endif
