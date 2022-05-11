ifndef DEVKITPRO
$(error Specify devkitPro install path with $$DEVKITPRO)
endif

DEVKITPATH=$(shell echo "$(DEVKITPRO)" | sed -e 's/^\([a-zA-Z]\):/\/\1/')
PATH := $(DEVKITPATH)/devkitPPC/bin:$(PATH)

CC      := powerpc-eabi-gcc
CXX     := powerpc-eabi-g++
OBJCOPY := powerpc-eabi-objcopy

DEFINES := $(foreach def, $(USERDEFS), -D$(def))
DEFINES += -DGEKKO -DNOPAL -DDOL -DUCF_TOGGLE

ifdef MODVERSION
ifneq ($(shell echo "$(MODVERSION)" | grep -P '(^|-)(beta|rc)($$|[\d-])'),)
BETA := 1
endif
MODNAME := lab-$(MODVERSION)
else
MODNAME := lab
endif

ifdef BETA
DEFINES += -DBETA
endif

LIBDIR  := lib
BASE103 := $(LIBDIR)/ssbm-1.03
BASEMOD := $(LIBDIR)/ssbm-1.03/src/mod
IMGUI   := $(LIBDIR)/imgui

VERSION := 102
DEFINES += -DMODNAME=\"$(MODNAME)\" -DNTSC102
MELEELD := $(BASE103)/GALE01r2.ld
BINDIR  := build/bin
ISODIR  := iso
TOOLS   := $(BASE103)/tools
OBJDIR  := build/obj/DOL
DEPDIR  := build/dep
GENDIR  := build/gen
SRCDIR  := src $(GENDIR) $(BASEMOD)/src

OUTPUTMAP = $(OBJDIR)/output.map
LDFLAGS   = -Wl,-Map=$(OUTPUTMAP) -Wl,--gc-sections -flto

MELEEMAP  = $(MELEELD:.ld=.map)

CFLAGS    = $(DEFINES) -mogc -mcpu=750 -meabi -mhard-float -Os \
            -Wall -Wno-switch -Wno-unused-value -Wconversion -Warith-conversion -Wno-multichar \
            -Wno-pointer-arith \
            -ffunction-sections -fdata-sections -mno-sdata \
            -fno-builtin-sqrt -fno-builtin-sqrtf -flto
ASFLAGS   = $(DEFINES) -Wa,-mregnames -Wa,-mgekko
CXXFLAGS  = $(CFLAGS) -std=c++2b -fconcepts -fno-rtti -fno-exceptions
INCLUDE  := $(foreach dir, $(SRCDIR), -I$(dir)) -I$(DEVKITPATH)/libogc/include -I$(IMGUI)

DOLFILE := $(ISODIR)/sys/main.dol
PATCHES := $(BINDIR)/patches.bin
DOLELF  := $(BINDIR)/dol.elf
DOLDATA := $(OBJDIR)/dol_data.bin
DOLLD   := $(BASEMOD)/dol.ld

CFILES   := $(foreach dir, $(SRCDIR), $(shell find $(dir) -type f -name '*.c'   2> /dev/null))
CXXFILES := $(foreach dir, $(SRCDIR), $(shell find $(dir) -type f -name '*.cpp' 2> /dev/null))
SFILES   := $(foreach dir, $(SRCDIR), $(shell find $(dir) -type f -name '*.S'   2> /dev/null))

CXXFILES += $(IMGUI)/imgui_draw.cpp $(IMGUI)/imgui_tables.cpp $(IMGUI)/imgui_widgets.cpp $(IMGUI)/imgui.cpp

OBJFILES := \
	$(patsubst %, $(OBJDIR)/%.o, $(CFILES)) \
    $(patsubst %, $(OBJDIR)/%.o, $(CXXFILES)) \
    $(patsubst %, $(OBJDIR)/%.o, $(SFILES))

DEPFILES := $(patsubst $(OBJDIR)/%.o, $(DEPDIR)/%.d, $(OBJFILES))

LINKSCRIPT := mod.ld

.PHONY: lab
lab: $(DOLFILE)

$(DOLFILE): $(DOLELF) $(PATCHES) $(TOOLS)/patch_dol.py
	@[ -d $(@D) ] || mkdir -p $(@D)
	@[ $(DOLSRC) ] || ( echo "\$$DOLSRC must be set to the path of an NTSC 1.02 main.dol file." >& 2; exit 1 )
	$(OBJCOPY) -O binary -R .patches $< $@
	python $(TOOLS)/patch_dol.py $@ $@ $(OUTPUTMAP) $(PATCHES)

$(PATCHES): $(DOLELF)
	@[ -d $(@D) ] || mkdir -p $(@D)
	$(OBJCOPY) -O binary -j .patches $< $@

$(DOLELF): $(OBJFILES) $(DOLDATA) $(DOLLD) $(MELEELD) $(BASEMOD)/../common.ld | clean_unused
	@[ -d $(@D) ] || mkdir -p $(@D)
	$(CC) $(LDFLAGS) -T$(DOLLD) -T$(MELEELD) $(OBJFILES) -o $@

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
RESOURCES        := $(foreach dir, $(RESOURCE_DIR_IN), $(shell find $(dir) -type f))
RESOURCES        := $(filter-out %.docx, $(filter-out %.psd, $(RESOURCES)))
TEXTURES         := $(filter     %.png,  $(RESOURCES))
RESOURCES        := $(filter-out %.png,  $(RESOURCES))

define get_resource_out
$(RESOURCE_DIR_OUT)/$(shell echo $1 | sed -r "s/(^|.*[/\\])resources[/\\](.*)/\\2/")
endef

define get_texture_out
$(shell echo $(call get_resource_out, $1) | sed -r "s/((.*)(\\.[^./\]*)|(.*))\\.png$$/\\2\\4.tex/")
endef

define make_resource_rule
_OUT := $(call get_resource_out, $1)
RESOURCES_OUT += $(_OUT)

$(_OUT): $1
	@mkdir -p $$(dir $$@)
	cp $$< $$@
endef

define make_texture_rule
_OUT := $(call get_texture_out, $1)
RESOURCES_OUT += $(_OUT)

$(_OUT): $1 $(TOOLS)/compress_resource.py $(TOOLS)/encode_texture.py
	python $(TOOLS)/encode_texture.py    $$< $$@
	python $(TOOLS)/compress_resource.py $$@ $$@
endef

$(foreach resource, $(RESOURCES), $(eval $(call make_resource_rule, $(resource))))
$(foreach texture,  $(TEXTURES),  $(eval $(call make_texture_rule,  $(texture))))

RESOURCE_HEADERS := $(RESOURCES_OUT:%=%.h)

$(RESOURCE_DIR_OUT)/%.h: $(RESOURCE_DIR_OUT)/% $(TOOLS)/bin_to_header.py
	python $(TOOLS)/bin_to_header.py $< $@

.PHONY: resources
resources: $(RESOURCE_HEADERS)

.PHONY: clean
clean:
	rm -rf $(BINDIR) $(OBJDIR) $(DEPDIR) $(RESOURCE_DIR_OUT)

# Remove unused obj/dep/resource files
.PHONY: clean_unused
clean_unused:
	$(foreach file, $(shell find $(OBJDIR) -type f 2> /dev/null), \
		$(if $(filter $(file), $(OBJFILES) $(OUTPUTMAP) $(DOLDATA)),, \
		rm $(file);))
	$(foreach file, $(shell find $(DEPDIR) -type f 2> /dev/null), \
		$(if $(filter $(file), $(DEPFILES)),, \
		rm $(file);))
	$(foreach file, $(shell find $(RESOURCE_DIR_OUT) -type f 2> /dev/null), \
		$(if $(filter $(file), $(RESOURCES_OUT) $(RESOURCE_HEADERS)),, \
		rm $(file);))
	echo "RESOURCES_OUT: $(RESOURCES_OUT)"

-include $(DEPFILES)