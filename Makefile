#
# libskry: image stacking library
#
#
# This file requires GNU Make (or a compatible). The C compiler has tu support
# dependencies file generation (see C_DEP_OPT).
#
# If Make cannot be used, simply compile all *.c files and link them into a library (e.g.: ar rcs libskry.a *.o)
#

CC = gcc
CFLAGS = -c -O3 -ffast-math -std=c99 -D__USE_MINGW_ANSI_STDIO=1 -Wno-parentheses -Wall -Wextra -Wstrict-prototypes -pedantic -fopenmp -I ./include
C_DEP_OPT = -MM
AR = ar
AR_FLAGS = rcs
OBJ_DIR = ./obj
BIN_DIR = ./bin
SRC_DIR = ./src
MKDIR_P = mkdir -p
REMOVE = rm

ifeq ($(USE_FREEIMAGE),1)
CFLAGS += -DUSE_FREEIMAGE
endif

ifeq ($(USE_CFITSIO),1)
CFLAGS += -DUSE_CFITSIO -I /usr/include/cfitsio
endif

LIB_NAME = libskry.a

OBJECTS = avi.o \
          bmp.o \
          filters.o \
          image.o \
          image_list.o \
          img_align.o \
          imgseq.o \
          init.o \
          logging.o \
          match.o \
          misc.o \
          quality.o \
          ref_pt_align.o \
          ser.o \
          stacking.o \
          tiff.o \
          triangulation.o
          
OBJECTS_PREF = $(addprefix $(OBJ_DIR)/,$(OBJECTS))

all: directories $(BIN_DIR)/$(LIB_NAME)

directories:
	$(MKDIR_P) $(BIN_DIR)
	$(MKDIR_P) $(OBJ_DIR)

clean:
	$(REMOVE) -f $(OBJECTS_PREF)
	$(REMOVE) -f $(OBJECTS_PREF:.o=.d)
	$(REMOVE) -f $(BIN_DIR)/$(LIB_NAME)

$(BIN_DIR)/$(LIB_NAME): $(OBJECTS_PREF)
	$(AR) $(AR_FLAGS) $(BIN_DIR)/$(LIB_NAME) $(OBJECTS_PREF)

# pull in dependency info for existing object files
-include $(OBJECTS_PREF:.o=.d)

$(OBJ_DIR)/avi.o: $(SRC_DIR)/imgseq/avi.c
	$(CC)  $(CFLAGS) $(SRC_DIR)/imgseq/avi.c -o $(OBJ_DIR)/avi.o
	$(CC)  $(C_DEP_OPT) $(CFLAGS) $(SRC_DIR)/imgseq/avi.c > $(OBJ_DIR)/avi.d

$(OBJ_DIR)/bmp.o: $(SRC_DIR)/image/bmp.c
	$(CC)  $(CFLAGS) $(SRC_DIR)/image/bmp.c -o $(OBJ_DIR)/bmp.o
	$(CC)  $(C_DEP_OPT) $(CFLAGS) $(SRC_DIR)/image/bmp.c > $(OBJ_DIR)/bmp.d

$(OBJ_DIR)/filters.o: $(SRC_DIR)/utils/filters.c
	$(CC)  $(CFLAGS) $(SRC_DIR)/utils/filters.c -o $(OBJ_DIR)/filters.o
	$(CC)  $(C_DEP_OPT) $(CFLAGS) $(SRC_DIR)/utils/filters.c > $(OBJ_DIR)/filters.d

$(OBJ_DIR)/image.o: $(SRC_DIR)/image/image.c
	$(CC)  $(CFLAGS) $(SRC_DIR)/image/image.c -o $(OBJ_DIR)/image.o
	$(CC)  $(C_DEP_OPT) $(CFLAGS) $(SRC_DIR)/image/image.c > $(OBJ_DIR)/image.d

$(OBJ_DIR)/image_list.o: $(SRC_DIR)/imgseq/image_list.c
	$(CC)  $(CFLAGS) $(SRC_DIR)/imgseq/image_list.c -o $(OBJ_DIR)/image_list.o
	$(CC)  $(C_DEP_OPT) $(CFLAGS) $(SRC_DIR)/imgseq/image_list.c > $(OBJ_DIR)/image_list.d

$(OBJ_DIR)/img_align.o: $(SRC_DIR)/img_align.c
	$(CC)  $(CFLAGS) $(SRC_DIR)/img_align.c -o $(OBJ_DIR)/img_align.o
	$(CC)  $(C_DEP_OPT) $(CFLAGS) $(SRC_DIR)/img_align.c > $(OBJ_DIR)/img_align.d

$(OBJ_DIR)/imgseq.o: $(SRC_DIR)/imgseq/imgseq.c
	$(CC)  $(CFLAGS) $(SRC_DIR)/imgseq/imgseq.c -o $(OBJ_DIR)/imgseq.o
	$(CC)  $(C_DEP_OPT) $(CFLAGS) $(SRC_DIR)/imgseq/imgseq.c > $(OBJ_DIR)/imgseq.d

$(OBJ_DIR)/init.o: $(SRC_DIR)/init.c
	$(CC)  $(CFLAGS) $(SRC_DIR)/init.c -o $(OBJ_DIR)/init.o
	$(CC)  $(C_DEP_OPT) $(CFLAGS) $(SRC_DIR)/init.c > $(OBJ_DIR)/init.d

$(OBJ_DIR)/logging.o: $(SRC_DIR)/utils/logging.c
	$(CC)  $(CFLAGS) $(SRC_DIR)/utils/logging.c -o $(OBJ_DIR)/logging.o
	$(CC)  $(C_DEP_OPT) $(CFLAGS) $(SRC_DIR)/utils/logging.c > $(OBJ_DIR)/logging.d

$(OBJ_DIR)/match.o: $(SRC_DIR)/utils/match.c
	$(CC)  $(CFLAGS) $(SRC_DIR)/utils/match.c -o $(OBJ_DIR)/match.o
	$(CC)  $(C_DEP_OPT) $(CFLAGS) $(SRC_DIR)/utils/match.c > $(OBJ_DIR)/match.d

$(OBJ_DIR)/misc.o: $(SRC_DIR)/utils/misc.c
	$(CC)  $(CFLAGS) $(SRC_DIR)/utils/misc.c -o $(OBJ_DIR)/misc.o
	$(CC)  $(C_DEP_OPT) $(CFLAGS) $(SRC_DIR)/utils/misc.c > $(OBJ_DIR)/misc.d

$(OBJ_DIR)/quality.o: $(SRC_DIR)/quality.c
	$(CC)  $(CFLAGS) $(SRC_DIR)/quality.c -o $(OBJ_DIR)/quality.o
	$(CC)  $(C_DEP_OPT) $(CFLAGS) $(SRC_DIR)/quality.c > $(OBJ_DIR)/quality.d

$(OBJ_DIR)/ref_pt_align.o: $(SRC_DIR)/ref_pt_align.c
	$(CC)  $(CFLAGS) $(SRC_DIR)/ref_pt_align.c -o $(OBJ_DIR)/ref_pt_align.o
	$(CC)  $(C_DEP_OPT) $(CFLAGS) $(SRC_DIR)/ref_pt_align.c > $(OBJ_DIR)/ref_pt_align.d

$(OBJ_DIR)/ser.o: $(SRC_DIR)/imgseq/ser.c
	$(CC)  $(CFLAGS) $(SRC_DIR)/imgseq/ser.c -o $(OBJ_DIR)/ser.o
	$(CC)  $(C_DEP_OPT) $(CFLAGS) $(SRC_DIR)/imgseq/ser.c > $(OBJ_DIR)/ser.d
	
$(OBJ_DIR)/stacking.o: $(SRC_DIR)/stacking.c
	$(CC)  $(CFLAGS) $(SRC_DIR)/stacking.c -o $(OBJ_DIR)/stacking.o
	$(CC)  $(C_DEP_OPT) $(CFLAGS) $(SRC_DIR)/stacking.c > $(OBJ_DIR)/stacking.d

$(OBJ_DIR)/tiff.o: $(SRC_DIR)/image/tiff.c
	$(CC)  $(CFLAGS) $(SRC_DIR)/image/tiff.c -o $(OBJ_DIR)/tiff.o
	$(CC)  $(C_DEP_OPT) $(CFLAGS) $(SRC_DIR)/image/tiff.c > $(OBJ_DIR)/tiff.d

$(OBJ_DIR)/triangulation.o: $(SRC_DIR)/utils/triangulation.c
	$(CC)  $(CFLAGS) $(SRC_DIR)/utils/triangulation.c -o $(OBJ_DIR)/triangulation.o
	$(CC)  $(C_DEP_OPT) $(CFLAGS) $(SRC_DIR)/utils/triangulation.c > $(OBJ_DIR)/triangulation.d
