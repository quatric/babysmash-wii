#---------------------------------------------------------------------------------
# BabySmash for Wii
#   ./scripts/build_controller_portlibs.sh   (once: libwiidrc / libwupc / libsicksaxis)
#   ./scripts/build_grrlib.sh                (once: GRRLIB)
#   make            -> boot.dol and boot-xz.dol (dolxz-compressed)
#   make dist       -> dist/apps/babysmash/ ready to copy to the SD card
#---------------------------------------------------------------------------------
.SUFFIXES:
ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

include $(DEVKITPPC)/wii_rules

TARGET		:=	boot
BUILD		:=	build
SOURCES		:=	src libinput-wii/source
DATA		:=	data
INCLUDES	:=	src

CFLAGS	= -g -O2 -Wall -Wno-unused-function $(MACHDEP) $(INCLUDE) \
          -DHAVE_WIIDRC -DHAVE_WUPC -DHAVE_SICKSAXIS $(EXTRA_CFLAGS)
# Version for the update check: the release tag when CI builds one, else meta.xml
APP_VERSION	?=	$(if $(filter refs/tags/%,$(GITHUB_REF)),$(GITHUB_REF_NAME),$(shell sed -n 's:.*<version>\(.*\)</version>.*:\1:p' $(dir $(abspath $(firstword $(MAKEFILE_LIST))))meta.xml))
CFLAGS	+=	-DAPP_VERSION=\"$(APP_VERSION)\"
CXXFLAGS	=	$(CFLAGS)
LDFLAGS	=	-g $(MACHDEP) -Wl,-Map,$(notdir $@).map -Wl,-wrap,wiiuse_accept

LIBS	:=	-lgrrlib -lwiidrc -lwupc -lsicksaxis -lfreetype -lpng -ljpeg -lbrotlidec -lbrotlicommon -lbz2 -lz \
            -lfat -lwiikeyboard -lwiiuse -lbte -lasnd -logc -lm
LIBS	:=	-lwiiupdate $(LIBS)

LIBDIRS	:=	$(CURDIR)/portlibs/ppc $(DEVKITPRO)/portlibs/ppc $(DEVKITPRO)/portlibs/wii $(CURDIR)/libinput-wii
LIBDIRS	+=	$(CURDIR)/libwiiupdate

# The .dol that goes into the Homebrew Channel package.
ifeq ($(NO_DOLXZ),1)
BOOT_DOL	=	$(TARGET).dol
else
BOOT_DOL	=	$(TARGET)-xz.dol
endif

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
					$(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.bin)))
export LD	:=	$(CC)
export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CFILES:.c=.o)
export OFILES	:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES), -iquote $(CURDIR)/$(dir)) \
					$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
					-I$(CURDIR)/$(BUILD) -I$(LIBOGC_INC)
export LIBPATHS	:=	-L$(LIBOGC_LIB) $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean dist run

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD): libwiiupdate-lib
.PHONY: libwiiupdate-lib
libwiiupdate-lib:
	@$(MAKE) --no-print-directory -C $(CURDIR)/libwiiupdate

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).dol $(OUTPUT)-xz.dol dist

dist: $(BUILD)
	@rm -rf dist && mkdir -p dist/apps/babysmash
	@cp $(BOOT_DOL) dist/apps/babysmash/boot.dol
	@cp meta.xml dist/apps/babysmash/
	@[ ! -f icon.png ] || cp icon.png dist/apps/babysmash/
	@[ ! -d speech ] || cp -R speech dist/apps/babysmash/speech
	@echo "dist/apps/babysmash ready - copy the 'apps' folder to the root of your SD card"

run:
	wiiload $(TARGET).dol

else

DEPENDS	:=	$(OFILES:.o=.d)
#---------------------------------------------------------------------------------
# Self-extracting xz-compressed copy of the .dol (FIX94's dolxz, see
# scripts/dolxz.sh). It is the default target; NO_DOLXZ=1 skips it.
#---------------------------------------------------------------------------------
ifneq ($(NO_DOLXZ),1)
$(OUTPUT)-xz.dol: $(OUTPUT).dol
	@echo compressing ... $(notdir $@)
	@sh $(dir $(OUTPUT))scripts/dolxz.sh $< $@
endif

$(OUTPUT).dol: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

$(OFILES_SRC) : $(HFILES)

%.bin.o %_bin.h : %.bin
	@echo $(notdir $<)
	$(bin2o)

-include $(DEPENDS)

endif
