#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# INCLUDES is a list of directories containing header files
#
# APP_TITLE is the name of the app stored in the SMDH file (Optional)
# APP_DESCRIPTION is the description of the app stored in the SMDH file (Optional)
# APP_AUTHOR is the author of the app stored in the SMDH file (Optional)
#---------------------------------------------------------------------------------

TARGET			:=	$(notdir $(CURDIR))
BUILD			:=	build
SOURCES			:=	source
INCLUDES		:=	include

APP_ICON  		:=  $(CTRULIB)/default_icon.png
APP_TITLE		:=	A9K11AIO
APP_DESCRIPTION	:=	ARM9 code execution
APP_AUTHOR		:=	jason0597

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------

ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS	:=	-g -Wall -O2 -mword-relocations -fomit-frame-pointer -ffunction-sections $(ARCH) $(INCLUDE) -DARM11 -D_3DS
ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS	:=  -lctru -lm

#---------------------------------------------------------------------------------
# detected OS, required for deciding which bin2text to use
#---------------------------------------------------------------------------------

ifeq ($(OS),Windows_NT)
BIN2TEXT := bin2text/bin2text_windows.exe 
else
ifeq ($(shell uname -s), Linux)
BIN2TEXT := ./bin2text/bin2text_linux
else
BIN2TEXT := ./bin2text/bin2text_mac 
endif
endif

#---------------------------------------------------------------------------------
# check if bundled arm9.bin exists in arm9 folder
#---------------------------------------------------------------------------------

ifneq ("$(wildcard arm9/arm9.bin)","")
ARM9BIN_EXISTS = 1
CFLAGS += -DARM9BIN_EXISTS=1
else
ARM9BIN_EXISTS = 0
endif

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------

LIBDIRS	:= $(CTRULIB)

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) $(foreach dir,$(DATA),$(CURDIR)/$(dir))	
export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

#---------------------------------------------------------------------------------
# Use CC for linking standard C projects
#---------------------------------------------------------------------------------
export LD	:=	$(CC)
#---------------------------------------------------------------------------------

export OFILES	:=	$(CFILES:.c=.o) $(SFILES:.s=.o)
export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) payload clean all

#---------------------------------------------------------------------------------

all:
	@cd payload && $(MAKE)
	@mkdir -p $(BUILD) 
	@$(BIN2TEXT) payload/arm11.bin payload/arm11bin.h 25 arm11_payload
ifeq ($(ARM9BIN_EXISTS), 1)
	@$(BIN2TEXT) arm9/arm9.bin arm9/arm9bin.h 25 arm9_payload
endif
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------

clean:
	@echo clean ...
	@rm -rf $(BUILD) $(TARGET).3dsx $(TARGET).smdh $(TARGET).elf payload/arm11.bin payload/arm11bin.h arm9/arm9bin.h payload/a.out payload/*.o

#---------------------------------------------------------------------------------
else

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------

ifeq ($(strip $(NO_SMDH)),)
$(OUTPUT).3dsx	:	$(OUTPUT).elf $(OUTPUT).smdh
else
$(OUTPUT).3dsx	:	$(OUTPUT).elf
endif

$(OUTPUT).elf	:	$(OFILES)

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------