# Early makefile for Casio Loopy (Kasami, August 2023)
# Based on Wonderful Toolchain package for SuperH

# Normal use: "make clean && make"

# Memory sizes; should use K/M suffix or decimal integer
# Cart battery-backed save RAM, most have at least 8K if any
# "Floopy Drive" flash cart has 128K
SRAMSIZE = 8K
# Allocated stack size, increase if necessary
STACKSIZE = 1K

# Toolchain programs
WONDERFUL_TOOLCHAIN ?= 
TOOLBIN ?= 
#TOOLBIN =
PREFIX ?= sh1-elf-
#PREFIX = sh1-none-elf-
CC  = $(TOOLBIN)$(PREFIX)gcc
CXX = $(TOOLBIN)$(PREFIX)g++
LD  = $(TOOLBIN)$(PREFIX)ld
OBJ = $(TOOLBIN)$(PREFIX)objcopy

# File manipulation progs
MV     = mv
MKDIR  = mkdir -p
RMDIR  = rm -rf
FIXROM = /usr/bin/env python3 ./tools/fixrom.py # Change to "python" if necessary

# File/dir locations
SRCDIR = ./src
INCDIR = ./include/casloopy
OBJDIR = ./obj
ROM    = ./rom.bin

# Basic compile options
OPTIMIZE = -Ofast -flto -Wvla
LIBS =

# Below here probably doesn't need to be touched

LDSCRIPT = ./tools/loopy.ld

# Source/object lists
SRCS_C = src/main.c src/casloopy.c src/common.c src/crt0.c src/font_drawing.c
OBJS_C = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS_C))
SRCS_S = src/header.s src/startup.s
OBJS_S = $(patsubst $(SRCDIR)/%.s,$(OBJDIR)/%.o,$(SRCS_S))

CFLAGS  = $(OPTIMIZE)
CFLAGS += -m1 -mrenesas -DPLATFORM=3 -DHARDWARE_DIV=1 -DBY16_MUL=1
CFLAGS += -ffreestanding
CFLAGS += -falign-functions=4 -ffunction-sections -fdata-sections
CFLAGS += -fomit-frame-pointer -fno-asynchronous-unwind-tables -fno-unwind-tables
CFLAGS += -Wstack-usage=$(shell numfmt --from=iec $(STACKSIZE)) -I$(INCDIR)

CXXFLAGS = -std=c++23 -fno-exceptions -fno-non-call-exceptions -fno-rtti -fno-threadsafe-statics

SIZEDEFS  = -Wl,--defsym=SRAMSIZE=$(SRAMSIZE)
SIZEDEFS += -Wl,--defsym=STACKSIZE=$(STACKSIZE)

LDFLAGS  = -nostartfiles -nolibc -Wl,--gc-sections -Wl,--as-needed -flto -Wl,--no-warn-rwx-segment -Wl,--orphan-handling=error -Wl,--print-memory-usage
LDFLAGS += $(SIZEDEFS) -Wl,-T $(LDSCRIPT) $(LIBS) -s

.PHONY: clean rom

all: rom

rom: $(ROM)

%.elf:
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

%.bin: %.elf
	$(OBJ) -O binary $< $@
	$(FIXROM) $(ROM)

$(ROM:.bin=.elf): $(OBJS_S) $(OBJS_C)

$(OBJDIR)/%.o: $(SRCDIR)/%.s | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)
	$(CXX) $(CFLAGS) $(CXXFLAGS) -c $< -o $@

$(OBJDIR):
	$(MKDIR) $@

clean:
	$(RMDIR) $(OBJDIR)
	$(RM) $(ROM) rom.elf

