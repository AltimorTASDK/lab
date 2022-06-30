ifndef DEVKITPRO
$(error Specify devkitPro install path with $$DEVKITPRO)
endif

DEVKITPATH=$(shell echo "$(DEVKITPRO)" | sed -e 's/^\([a-zA-Z]\):/\/\1/')
PATH := $(DEVKITPATH)/devkitPPC/bin:$(PATH)

CC      := powerpc-eabi-gcc
CXX     := powerpc-eabi-g++
OBJCOPY := powerpc-eabi-objcopy

DEFINES := $(foreach def, $(USERDEFS), -D$(def))
DEFINES += -DGEKKO -DNOPAL -DDOL -DUCF_ROTATOR

ifdef MODVERSION
ifneq ($(shell echo "$(MODVERSION)" | grep -P '(^|-)(beta|rc)($$|[\d-])'),)
BETA := 1
endif
MODNAME := lab-$(MODVERSION)
else
MODNAME := lab
endif

LIBDIR  := lib
BASE103 := $(LIBDIR)/ssbm-1.03
BASEMOD := $(LIBDIR)/ssbm-1.03/src/mod

VERSION := 102
DEFINES += -DMODNAME=\"$(MODNAME)\" -DNTSC102
MELEELD := $(BASE103)/GALE01r2.ld
BINDIR  := build/bin
ISODIR  := iso
TOOLS   := $(BASE103)/tools
OBJDIR  := build/obj
DEPDIR  := build/dep
GENDIR  := build/gen
SRCDIR  := src $(GENDIR) $(BASEMOD)/src

OUTPUTMAP   = $(OBJDIR)/output.map
LDFLAGS     = -nolibc -Wl,-Map=$(OUTPUTMAP) -Wl,--gc-sections
STATICLIBS := -lm

MELEEMAP  = $(MELEELD:.ld=.map)

CFLAGS    = $(DEFINES) -mogc -mcpu=750 -meabi -mhard-float -Os \
            -Wall -Wno-switch -Wno-unused-value -Wconversion -Warith-conversion -Wno-multichar \
			-Wno-deprecated-enum-enum-conversion -Wno-pointer-arith \
            -ffunction-sections -fdata-sections -mno-sdata \
            -fno-builtin-sqrt -fno-builtin-sqrtf
ASFLAGS   = $(DEFINES) -Wa,-mregnames -Wa,-mgekko
CXXFLAGS  = $(CFLAGS) -std=c++2b -fconcepts -fno-rtti -fno-exceptions
INCLUDE  := $(foreach dir, $(SRCDIR), -I$(dir)) -I$(DEVKITPATH)/libogc/include -Isrc/libc

ifdef BETA
DEFINES += -DBETA
endif

ifdef DEBUG
CFLAGS  += -g
else
DEFINES += -DNDEBUG
CFLAGS  += -flto
LDFLAGS += -flto
endif

DOLFILE := $(ISODIR)/sys/main.dol
PATCHES := $(BINDIR)/patches.bin
DOLELF  := $(BINDIR)/dol.elf
DOLDATA := build/dol_data.bin
DOLLD   := lab.ld

CFILES   := $(foreach dir, $(SRCDIR), $(shell find $(dir) -type f -name '*.c'   2> /dev/null))
CXXFILES := $(foreach dir, $(SRCDIR), $(shell find $(dir) -type f -name '*.cpp' 2> /dev/null))
SFILES   := $(foreach dir, $(SRCDIR), $(shell find $(dir) -type f -name '*.S'   2> /dev/null))

# Include imgui
IMGUI    := $(LIBDIR)/imgui
INCLUDE  += -I$(IMGUI)
CXXFILES += $(IMGUI)/imgui_draw.cpp $(IMGUI)/imgui_tables.cpp $(IMGUI)/imgui_widgets.cpp $(IMGUI)/imgui.cpp
DEFINES  += -DIMGUI_USER_CONFIG=\"imgui/userconfig.h\"
$(OBJDIR)/$(IMGUI)/%.o: CFLAGS += -Wno-conversion

# Include stb
STB     := $(LIBDIR)/stb
INCLUDE += -I$(STB)

# Include bscanf
BSCANF  := $(LIBDIR)/bscanf
INCLUDE += -I$(BSCANF)
CFILES  += $(BSCANF)/bscanf.c
$(OBJDIR)/$(BSCANF)/%.o: CFLAGS += -Wno-char-subscripts -Wno-unused-but-set-variable

# Include dietlibc
DIET   := $(LIBDIR)/dietlibc
CFILES += $(DIET)/lib/strtol.c $(DIET)/lib/strtod.c \
          $(DIET)/lib/snprintf.c $(DIET)/lib/strlcpy.c $(DIET)/lib/strlcat.c \
		  $(DIET)/lib/isalnum.c $(DIET)/lib/isspace.c \
		  $(DIET)/lib/strstr.c \
		  $(DIET)/lib/qsort.c
$(OBJDIR)/$(DIET)/%.o: INCLUDE += -I$(DIET) -I$(DIET)/include
$(OBJDIR)/$(DIET)/%.o: CFLAGS += -Wno-char-subscripts -Wno-sign-conversion -Wno-attributes

OBJFILES := \
	$(patsubst %, $(OBJDIR)/%.o, $(CFILES)) \
    $(patsubst %, $(OBJDIR)/%.o, $(CXXFILES)) \
    $(patsubst %, $(OBJDIR)/%.o, $(SFILES))

DEPFILES := $(patsubst $(OBJDIR)/%.o, $(DEPDIR)/%.d, $(OBJFILES))

.PHONY: lab
lab: $(DOLFILE) | clean-unused

$(DOLFILE): $(DOLELF) $(PATCHES) $(TOOLS)/patch_dol.py
	@[ -d $(@D) ] || mkdir -p $(@D)
	@[ $(DOLSRC) ] || ( echo "\$$DOLSRC must be set to the path of an NTSC 1.02 main.dol file." >& 2; exit 1 )
	$(OBJCOPY) -O binary -R .patches --strip-debug $< $@
	python $(TOOLS)/patch_dol.py $@ $@ $(OUTPUTMAP) $(PATCHES)

$(PATCHES): $(DOLELF)
	@[ -d $(@D) ] || mkdir -p $(@D)
	$(OBJCOPY) -O binary -j .patches $< $@

$(DOLELF): $(OBJFILES) $(DOLDATA) $(DOLLD) $(MELEELD) $(BASEMOD)/../common.ld
	@[ -d $(@D) ] || mkdir -p $(@D)
	$(CC) $(LDFLAGS) -T$(DOLLD) -T$(MELEELD) $(OBJFILES) $(STATICLIBS) -o $@

$(DOLDATA): $(DOLSRC)
	@[ -d $(@D) ] || mkdir -p $(@D)
# skip dol header
	dd if=$(DOLSRC) of=$(DOLDATA) bs=256 skip=1

$(MELEELD): $(MELEEMAP) $(TOOLS)/map_to_linker_script.py
	python $(TOOLS)/map_to_linker_script.py $(MELEEMAP) $(MELEELD)

$(OBJDIR)/%.c.o: %.c | resources
	@[ -d $(@D) ] || mkdir -p $(@D)
	@[ -d $(subst $(OBJDIR), $(DEPDIR), $(@D)) ] || mkdir -p $(subst $(OBJDIR), $(DEPDIR), $(@D))
	$(CC) -MMD -MP -MF $(patsubst $(OBJDIR)/%.o, $(DEPDIR)/%.d, $@) $(CFLAGS) $(INCLUDE) -c $< -o $@

$(OBJDIR)/%.cpp.o: %.cpp | resources
	@[ -d $(@D) ] || mkdir -p $(@D)
	@[ -d $(subst $(OBJDIR), $(DEPDIR), $(@D)) ] || mkdir -p $(subst $(OBJDIR), $(DEPDIR), $(@D))
	$(CXX) -MMD -MP -MF $(patsubst $(OBJDIR)/%.o, $(DEPDIR)/%.d, $@) $(CXXFLAGS) $(INCLUDE) -c $< -o $@

$(OBJDIR)/%.S.o: %.S
	@[ -d $(@D) ] || mkdir -p $(@D)
	@[ -d $(subst $(OBJDIR), $(DEPDIR), $(@D)) ] || mkdir -p $(subst $(OBJDIR), $(DEPDIR), $(@D))
	$(CC) $(ASFLAGS) -c $< -o $@

RESOURCE_DIR_IN  := resources $(BASEMOD)/resources
RESOURCE_DIR_OUT := $(GENDIR)/resources
-include $(BASEMOD)/resources.mk

.PHONY: clean
clean:
	rm -rf build

# Remove unused obj/dep/gen files
.PHONY: clean-unused
clean-unused:
	$(foreach file, $(shell find $(OBJDIR) -type f 2> /dev/null), \
		$(if $(filter $(file), $(OBJFILES) $(OUTPUTMAP) $(DOLDATA)),, \
		rm $(file);))
	$(foreach file, $(shell find $(DEPDIR) -type f 2> /dev/null), \
		$(if $(filter $(file), $(DEPFILES)),, \
		rm $(file);))
	$(foreach file, $(shell find $(GENDIR) -type f 2> /dev/null), \
		$(if $(filter $(file), $(RESOURCES_OUT) $(RESOURCE_HEADERS)),, \
		rm $(file);))

-include $(DEPFILES)