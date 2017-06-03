#
# libskry: image stacking library
#
#
# This file requires GNU Make (or a compatible). The C compiler has tu support
# dependencies file generation (see C_DEP_GEN_OPT and C_DEP_TARGET_OPT).
#
# If Make cannot be used, simply compile all *.c files and link them into a library (e.g.: ar rcs libskry.a *.o)
#

#--------- Configuration -------------------

# If set to 0, there is only limited AVI support (no extended/ODML headers);
# libav is usually available as package `ffmpeg-devel`
USE_LIBAV = 1

# Needed if USE_LIBAV = 1
LIBAV_INCLUDE_PATH = /usr/include/ffmpeg

#-------------------------------------------


CC = gcc
CFLAGS = -c -O3 -ffast-math -std=c99 -I ./include \
         -D__USE_MINGW_ANSI_STDIO=1 \
         -Wno-parentheses -Wall -Wextra -Wstrict-prototypes -pedantic \
         -Wno-missing-field-initializers \
         -fopenmp

# Cmdline option for CC to generate dependencies list to std. output
C_DEP_GEN_OPT = -MM

# Cmdline option for CC to specify target name of generated dependencies
C_DEP_TARGET_OPT = -MT

AR = ar
AR_FLAGS = rcs
OBJ_DIR = ./obj
BIN_DIR = ./bin
SRC_DIR = ./src

# Directory creation command, not failing if directory exists
MKDIR_P = mkdir -p

REMOVE = rm

ifeq ($(USE_FREEIMAGE),1)
CFLAGS += -DUSE_FREEIMAGE
endif

ifeq ($(USE_CFITSIO),1)
CFLAGS += -DUSE_CFITSIO -I /usr/include/cfitsio
endif

LIB_NAME = libskry.a

SRC_FILES = img_align.c \
            init.c \
            quality.c \
            ref_pt_align.c \
            stacking.c \
            image/bmp.c \
            image/image.c \
            image/tiff.c \
            imgseq/image_list.c \
            imgseq/imgseq.c \
            imgseq/ser.c \
            utils/demosaic.c \
            utils/filters.c \
            utils/img_pool.c \
            utils/list.c \
            utils/logging.c \
            utils/match.c \
            utils/misc.c \
            utils/triangulation.c

ifeq ($(USE_LIBAV),1)
CFLAGS += -DUSE_LIBAV -I $(LIBAV_INCLUDE_PATH)
SRC_FILES += imgseq/vid_libav.c
else
SRC_FILES += imgseq/avi.c
endif


#
# Converts the specified path $(1) to the form:
#   $(OBJ_DIR)/<filename>.o
#
define make_object_name_from_src_file_name =
$(addprefix $(OBJ_DIR)/, \
    $(patsubst %.c, %.o, \
        $(notdir $(1))))
endef
            
OBJECTS = \
$(foreach srcfile, $(SRC_FILES), \
    $(call make_object_name_from_src_file_name, $(srcfile)))

all: directories $(BIN_DIR)/$(LIB_NAME)

directories:
	$(MKDIR_P) $(BIN_DIR)
	$(MKDIR_P) $(OBJ_DIR)

clean:
	$(REMOVE) -f $(OBJECTS)
	$(REMOVE) -f $(OBJECTS:.o=.d)
	$(REMOVE) -f $(BIN_DIR)/$(LIB_NAME)

$(BIN_DIR)/$(LIB_NAME): $(OBJECTS)
	$(AR) $(AR_FLAGS) $(BIN_DIR)/$(LIB_NAME) $(OBJECTS)

# Pull in dependency info for existing object files
-include $(OBJECTS:.o=.d)

#
# Creates a rule for building a .c file
#
# Parameters:
#   $(1) - full path to .o file
#   $(2) - full path to .c file
#   $(3) - additional compiler flags (may be empty)
#
define C_file_rule_template =
$(1): $(2)
	$(CC) $(CFLAGS) -c $(2) $(3) -o $(1)
	$(CC) $(CFLAGS) $(2) $(3) $(C_DEP_GEN_OPT) $(C_DEP_TARGET_OPT) $(1) > $(patsubst %.o, %.d, $(1))
endef

# Create build rules for all $(SRC_FILES)

$(foreach srcfile, $(SRC_FILES), \
  $(eval \
    $(call C_file_rule_template, \
      $(call make_object_name_from_src_file_name, $(srcfile)), \
      $(SRC_DIR)/$(srcfile), \
      $(if $(findstring $(notdir $(srcfile)), demosaic.c), -Wno-unused-variable, ))))
