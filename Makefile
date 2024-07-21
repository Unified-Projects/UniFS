TARGETS = UniFS_Mounter UniFS_Disker

# Root for FUSE includes and libraries for macOS and Linux
FUSE_ROOT_MAC = /usr/local
FUSE_ROOT_LINUX = /usr

# Check the operating system
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S), Darwin)
    LIBFUSE_INCLUDE_DIR = $(FUSE_ROOT_MAC)/include/fuse/
    LIBFUSE_LIBRARY_DIR = $(FUSE_ROOT_MAC)/lib
    LIBS = -lfuse -lpthread -lc++
    CFLAGS_FUSE += -D_DARWIN_USE_64_BIT_INODE
else
    LIBFUSE_INCLUDE_DIR = $(FUSE_ROOT_LINUX)/include/fuse
    LIBFUSE_LIBRARY_DIR = $(FUSE_ROOT_LINUX)/lib
    LIBS = -lfuse -lpthread -lc++
endif

CC ?= gcc

CFLAGS_FUSE = -I$(LIBFUSE_INCLUDE_DIR) -L$(LIBFUSE_LIBRARY_DIR)
CFLAGS_FUSE += -DFUSE_USE_VERSION=26
CFLAGS_FUSE += -D_FILE_OFFSET_BITS=64

CFLAGS_EXTRA = -Wall -std=c++20 -g $(CFLAGS)

# List of source files
SOURCES_Mounter = Mounter.cpp
OBJECTS_Mounter = $(SOURCES_Mounter:.cpp=.o)

SOURCES_Disker = CreateDisk.cpp
OBJECTS_Disker = $(SOURCES_Disker:.cpp=.o)

# Rule for building object files
%.o: %.cpp
	$(CC) $(CFLAGS_FUSE) $(CFLAGS_EXTRA) -c $< -o $@

# Rule for building the target
UniFS_Mounter: $(OBJECTS_Mounter)
	$(CC) $(CFLAGS_FUSE) $(CFLAGS_EXTRA) -o $@ $^ $(LIBS)

UniFS_Disker: $(OBJECTS_Disker)
	$(CC) $(CFLAGS_FUSE) $(CFLAGS_EXTRA) -o $@ $^ $(LIBS)

all: $(TARGETS)

clean:
	rm -f $(TARGETS) $(OBJECTS_Mounter) $(OBJECTS_Disker)
	rm -rf *.dSYM