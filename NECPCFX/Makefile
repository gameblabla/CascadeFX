SRCDIR		= ./src
VPATH		= $(SRCDIR)
SRC_C		= $(foreach dir, $(SRCDIR), $(wildcard $(dir)/*.c))
SRC_S		= $(foreach dir, $(SRCDIR), $(wildcard $(dir)/*.s))
OBJ_C		= $(notdir $(patsubst %.c, %.o, $(SRC_C)))
OBJ_S		= $(notdir $(patsubst %.s, %.o, $(SRC_S)))
CD_OBJECTS	   = 
OBJECTS        = $(OBJ_C) $(OBJ_S)
ELF_TARGET     = mytest.elf
BIN_TARGET     = mytest.bin
ADD_FILES      = 
CD_FILES	   = 
CDOUT          = mytest_cd

include example.mk
