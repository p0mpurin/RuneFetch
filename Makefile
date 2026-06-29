TARGET := RuneFetch
TITLEID := 0004013000c0fe02
BUILD := build
SOURCES := source
INCLUDES := include

ifeq ($(strip $(DEVKITARM)),)
$(error DEVKITARM is not set)
endif

include $(DEVKITARM)/3ds_rules

ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) -I$(CTRULIB)/include
CFLAGS := -Wall -Wextra -Os -mword-relocations -ffunction-sections -fdata-sections $(ARCH)
CFLAGS += $(INCLUDE)
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++14
ASFLAGS := -g $(ARCH)
LDFLAGS := -specs=3dsx.specs $(ARCH) -Wl,--gc-sections -Wl,-Map,$(notdir $*.map)
LIBS := -lctru -lm
LIBDIRS := $(CTRULIB)

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)
export LD := $(CXX)

CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

export OFILES := $(CFILES:.c=.o) $(CPPFILES:.cpp=.o) $(SFILES:.s=.o)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: all clean install

all: $(BUILD) $(OUTPUT).cxi

$(BUILD):
	@mkdir -p $@

$(OUTPUT).elf: $(OFILES)

$(OUTPUT).cxi: $(OUTPUT).elf RuneFetch.rsf
	makerom -f ncch -rsf RuneFetch.rsf -o $@ -elf $< -target p -ignoresign
	@cp $@ $(CURDIR)/$(TITLEID).cxi

install: $(OUTPUT).cxi
	@mkdir -p /luma/sysmodules
	@cp $(OUTPUT).cxi /luma/sysmodules/$(TITLEID).cxi

clean:
	@rm -rf $(BUILD) $(OUTPUT).elf $(OUTPUT).cxi $(OUTPUT).map $(CURDIR)/$(TITLEID).cxi

else

DEPENDS := $(OFILES:.o=.d)

%.o: %.c
	$(CC) -MMD -MP -MF $(DEPSDIR)/$*.d $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) -MMD -MP -MF $(DEPSDIR)/$*.d $(CXXFLAGS) -c $< -o $@

%.o: %.s
	$(CC) $(ASFLAGS) -c $< -o $@

$(OUTPUT).elf:
	$(CC) $(LDFLAGS) $(OFILES) $(LIBPATHS) $(LIBS) -o $@

-include $(DEPENDS)

endif
