#---------------------------------------------------------------------------------
# CubeVerse - Juxtaposition (Pretendo Network's Miiverse) for the Nintendo
# GameCube. Builds cubeverse.dol
# Requires devkitPPC + libogc2 + libfat (devkitPro), stb_image /
# stb_image_write in third_party/stb and mbedTLS 3.6 in third_party/mbedtls
# (the secure connection to Juxt and GitHub). Run ./get-deps.sh once.
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPPC)),)
$(error "DEVKITPPC is not set. Use the GitHub build or the Docker command in README.md")
endif

# libogc2 (the Swiss developers' fork): the official Broadband Adapter and
# WIZnet-based adapters (ETH2GC and similar), SD2SP2 and SD Gecko.
include $(DEVKITPRO)/libogc2/gamecube_rules

MBEDTLS		:=	third_party/mbedtls

ifneq (build,$(notdir $(CURDIR)))
ifneq ($(MAKECMDGOALS),clean)
ifeq ($(wildcard third_party/stb/stb_image_write.h),)
$(error "stb headers not found in third_party/stb. Run ./get-deps.sh first (see README.md)")
endif
ifeq ($(wildcard $(MBEDTLS)/library/ssl_tls.c),)
$(error "mbedTLS not found in $(MBEDTLS). Run ./get-deps.sh first (see README.md)")
endif
endif
endif

TARGET		:=	cubeverse
BUILD		:=	build
SOURCES		:=	source $(MBEDTLS)/library
DATA		:=
INCLUDES	:=	source

CFLAGS	=	-g -O2 -Wall -std=gnu11 -ffunction-sections -fdata-sections $(MACHDEP) $(INCLUDE) \
		-DMBEDTLS_USER_CONFIG_FILE='"gc_mbedtls_config.h"'
CXXFLAGS	=	$(CFLAGS)
LDFLAGS	=	-g $(MACHDEP) -Wl,--gc-sections -Wl,-Map,$(notdir $@).map

# libfat and libbba (the GameCube network stack) must come before libogc
LIBS	:=	-lfat -laesnd -lbba -logc -lm
LIBDIRS	:=	$(PORTLIBS)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export LD	:=	$(CC)
export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SOURCES	:=	$(CFILES:.c=.o)
export OFILES	:=	$(OFILES_BIN) $(OFILES_SOURCES)
export HFILES	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES), -iquote $(CURDIR)/$(dir)) \
			-I$(CURDIR)/source \
			-I$(CURDIR)/$(MBEDTLS)/include \
			-I$(CURDIR)/$(MBEDTLS)/library \
			-I$(CURDIR)/third_party/stb \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD) \
			-I$(LIBOGC_INC)

export LIBPATHS	:=	-L$(LIBOGC_LIB) $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).dol

else

DEPENDS	:=	$(OFILES:.o=.d)

$(OUTPUT).dol: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

$(OFILES_SOURCES) : $(HFILES)

-include $(DEPENDS)

endif
