ifndef DEVKITPRO
$(error Specify devkitPro install path with $$DEVKITPRO)
endif

DEVKITPATH=$(shell echo "$(DEVKITPRO)" | sed -e 's/^\([a-zA-Z]\):/\/\1/')
PATH := $(DEVKITPATH)/devkitPPC/bin:$(PATH)

export CC      := powerpc-eabi-gcc
export CXX     := powerpc-eabi-g++
export OBJCOPY := powerpc-eabi-objcopy

export DEFINES := $(foreach def, $(USERDEFS), -D$(def))
export DEFINES += -DGEKKO -DNOPAL -DDOL

ifdef MODVERSION
ifneq ($(shell echo "$(MODVERSION)" | grep -P '(^|-)(beta|rc)($$|[\d-])'),)
export BETA := 1
endif
export MODNAME := lab-$(MODVERSION)
else
export MODNAME := lab
endif

ifdef BETA
export DEFINES += -DBETA
endif

export VERSION := 102
export DEFINES += -DMODNAME=\"$(MODNAME)\" -DNTSC102
export MELEELD := $(abspath ssbm-1.03/GALE01r2.ld)
export ISODIR  := $(abspath iso)
export TOOLS   := $(abspath ssbm-1.03/tools)
export OBJDIR  := $(abspath obj)
export DEPDIR  := $(abspath dep)
export SRCDIR  := $(abspath src) $(abspath ssbm-1.03/src/mod/src)

export OUTPUTMAP = $(OBJDIR)/output.map
export LDFLAGS   = -Wl,-Map=$(OUTPUTMAP) -Wl,--gc-sections

export MELEEMAP  = $(MELEELD:.ld=.map)

export CFLAGS    = $(DEFINES) -mogc -mcpu=750 -meabi -mhard-float -Os \
				   -Wall -Wno-switch -Wno-unused-value -Wconversion -Warith-conversion -Wno-multichar \
				   -Wno-pointer-arith \
				   -ffunction-sections -fdata-sections -mno-sdata \
				   -fno-builtin-sqrt -fno-builtin-sqrtf
export ASFLAGS   = $(DEFINES) -Wa,-mregnames -Wa,-mgekko
export CXXFLAGS  = $(CFLAGS) -std=c++2b -fconcepts -fno-rtti -fno-exceptions
export INCLUDE  := $(foreach dir, $(SRCDIR), -I$(dir)) -I$(DEVKITPATH)/libogc/include

.PHONY: lab
lab: $(MELEELD)
	+@cd ssbm-1.03/src/mod && $(MAKE) dol

.PHONY: resources
resources:
	+@cd ssbm-1.03/src/mod && $(MAKE) resources

$(MELEELD): $(MELEEMAP) $(TOOLS)/map_to_linker_script.py
	python $(TOOLS)/map_to_linker_script.py $(MELEEMAP) $(MELEELD)

.PHONY: clean
clean:
	@cd ssbm-1.03/src/mod && $(MAKE) clean