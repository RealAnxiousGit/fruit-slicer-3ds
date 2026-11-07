#---------------------------------------------------------------------------------
# Fruit Slicer 3DS
#   make             -> FruitSlicer3DS.3dsx
#   make cia         -> FruitSlicer3DS.cia (needs makerom and bannertool on PATH)
#   make AUTOTEST=1  -> a build that plays itself for emulator test runs
#   make DEV=1       -> developer tools in Options (coins on demand); never for releases
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET		:=	FruitSlicer3DS
BUILD		:=	build
SOURCES		:=	source port
DATA		:=	data
INCLUDES	:=	source port
GRAPHICS	:=	gfx
GFXBUILD	:=	romfs/gfx
ROMFS		:=	romfs

APP_TITLE		:=	Fruit Slicer 3DS
APP_DESCRIPTION	:=	Slice fruit, chain combos, dodge bombs. Free fan game.
APP_AUTHOR		:=	RealAnxiousGit
ICON			:=	meta/icon.png

ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:=	-g -Wall -O2 -mword-relocations -ffunction-sections -fdata-sections $(ARCH)
CFLAGS	+=	$(INCLUDE) -D__3DS__
ifneq ($(strip $(AUTOTEST)),)
CFLAGS	+=	-DAUTOTEST $(TESTDEFS)
endif
ifneq ($(strip $(DEV)),)
CFLAGS	+=	-DDEV_TOOLS
endif

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17
ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map),--gc-sections

LIBS	:= -lcitro2d -lcitro3d -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lctru -lm  # curl and mbedTLS: updates over HTTPS
LIBDIRS	:= $(CTRULIB) $(PORTLIBS)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(GRAPHICS),$(CURDIR)/$(dir))
export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
GFXFILES	:=	$(foreach dir,$(GRAPHICS),$(notdir $(wildcard $(dir)/*.t3s)))

export LD	:=	$(CXX)
export T3XFILES	:=	$(GFXFILES:.t3s=.t3x)
export OFILES_SOURCES	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES	:=	$(OFILES_SOURCES)
export HFILES	:=	$(GFXFILES:.t3s=.h)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)
export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export ROMFS_T3XFILES	:=	$(patsubst %.t3s, $(GFXBUILD)/%.t3x, $(GFXFILES))
export ROMFS_SHADER	:=	$(GFXBUILD)/fruit.shbin
export _3DSXDEPS	:=	$(addprefix $(CURDIR)/,$(ROMFS_T3XFILES) $(ROMFS_SHADER))
export _3DSXFLAGS	+=	--smdh=$(CURDIR)/$(TARGET).smdh --romfs=$(CURDIR)/$(ROMFS)
export APP_ICON		:=	$(TOPDIR)/$(ICON)

.PHONY: all clean cia

all: $(BUILD) $(GFXBUILD) $(ROMFS_T3XFILES) $(ROMFS_SHADER)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

$(GFXBUILD):
	@mkdir -p $@

$(GFXBUILD)/%.t3x: $(GRAPHICS)/%.t3s | $(BUILD) $(GFXBUILD)
	@echo tex3ds $(notdir $<)
	@cd $(GRAPHICS) && tex3ds -i $(notdir $<) -H $(CURDIR)/$(BUILD)/$*.h -d $(CURDIR)/$(BUILD)/$*.d -o $(CURDIR)/$@

$(ROMFS_SHADER): port/fruit.v.pica | $(GFXBUILD)
	@echo picasso $(notdir $<)
	@picasso -o $@ $<

clean:
	@rm -rf $(BUILD) $(GFXBUILD) $(TARGET).3dsx $(TARGET).smdh $(TARGET).elf $(TARGET).cia meta/banner.bnr meta/icon.icn

meta/banner.bnr: meta/banner.png meta/banner.wav
	bannertool makebanner -i meta/banner.png -a meta/banner.wav -o $@

meta/icon.icn: meta/icon.png
	bannertool makesmdh -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p "$(APP_AUTHOR)" -i meta/icon.png -o $@

cia: all meta/banner.bnr meta/icon.icn
	makerom -f cia -o $(TARGET).cia -target t -exefslogo -elf $(TARGET).elf -rsf meta/cia.rsf -icon meta/icon.icn -banner meta/banner.bnr
	@echo built $(TARGET).cia

else

DEPENDS	:=	$(OFILES:.o=.d)

$(OUTPUT).3dsx: $(OUTPUT).elf $(_3DSXDEPS) $(OUTPUT).smdh

$(OFILES): $(HFILES)

$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
