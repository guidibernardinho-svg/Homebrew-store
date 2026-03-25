#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=/opt/devkitpro")
endif

TOPDIR  ?= $(CURDIR)
include $(DEVKITPRO)/rules/3ds.mk

#---------------------------------------------------------------------------------
# TARGET: name of the output file (no extension)
# BUILD:  output directory for object files
# SOURCES: list of directories containing source files
# INCLUDES: extra include dirs
# ROMFS: romfs directory for the app (optional)
#---------------------------------------------------------------------------------
TARGET   := homebrew_shop
BUILD    := build
SOURCES  := source
INCLUDES := include
ROMFS    := romfs

APP_TITLE    := Homebrew Shop
APP_DESCRIPTION := Browse & download 3DS homebrew
APP_AUTHOR   := YourName
APP_PRODUCT_CODE := CTR-H-HBSH
APP_UNIQUE_ID    := 0x16200

#---------------------------------------------------------------------------------
# Compiler flags
#---------------------------------------------------------------------------------
ARCH   := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS  := -g -Wall -O2 -mword-relocations -fomit-frame-pointer \
           -ffunction-sections $(ARCH) $(INCLUDE) -D__3DS__

CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17

ASFLAGS := -g $(ARCH)
LDFLAGS  = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

#---------------------------------------------------------------------------------
# Libraries: curl, jansson, mbedtls bundled via portlibs
#---------------------------------------------------------------------------------
LIBS    := -lcurl -ljansson -lmbedtls -lmbedx509 -lmbedcrypto \
           -lcitro3d -lctru -lm

LIBDIRS := $(CTRULIB) $(PORTLIBS)

#---------------------------------------------------------------------------------
# Automatic file detection
#---------------------------------------------------------------------------------
export OUTPUT  := $(CURDIR)/$(TARGET)
export TOPDIR
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

export OFILES   := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export APP_ICON := $(TOPDIR)/icon.png

ifeq ($(strip $(ROMFS)),)
    export _3DSXFLAGS :=
else
    export _3DSXFLAGS := --romfs=$(CURDIR)/$(ROMFS)
endif

#---------------------------------------------------------------------------------
.PHONY: all clean

all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(TARGET).smdh $(TARGET).cia $(TARGET).elf
