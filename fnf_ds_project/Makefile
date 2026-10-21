.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Установи devkitPro и devkitARM, задай DEVKITARM")
endif

include $(DEVKITARM)/ds_rules

TARGET   := fnf_ds
BUILD    := build
SOURCES  := .
INCLUDES :=

ARCH     := -mthumb -mthumb-interwork
CFLAGS   := -g -Wall -O2 -march=armv5te -mtune=arm946e-s -fomit-frame-pointer \
            -ffast-math $(ARCH) -DARM9
CFLAGS   += $(INCLUDE)
LDFLAGS   = -specs=ds_arm9.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS     := -lnds9
LIBDIRS  := $(LIBNDS)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT  := $(CURDIR)/$(TARGET)
export VPATH   := $(CURDIR)
export DEPSDIR := $(CURDIR)/$(BUILD)
export CC      := $(PREFIX)gcc
export LD      := $(CC)
export OFILES  := $(addsuffix .o,$(basename $(notdir $(wildcard *.c))))
export INCLUDE := $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).nds

else

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).nds: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
