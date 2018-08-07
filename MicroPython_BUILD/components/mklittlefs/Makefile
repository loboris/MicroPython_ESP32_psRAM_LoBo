TARGET = mklfs

ifeq ($(OS),Windows_NT)
	TARGET_OS := WINDOWS
	DIST_SUFFIX := windows
	ARCHIVE_CMD := 7z a
	ARCHIVE_EXTENSION := zip
	TARGET := mklfs.exe
	TARGET_CFLAGS := -mno-ms-bitfields
	TARGET_LDFLAGS := -Wl,-static -static-libgcc
	CC=gcc
	CXX=g++
else
	UNAME_S := $(shell uname -s)
	ifeq ($(UNAME_S),Linux)
		TARGET_OS := LINUX
		UNAME_P := $(shell uname -p)
		ifeq ($(UNAME_P),x86_64)
			DIST_SUFFIX := linux64
		endif
		ifneq ($(filter %86,$(UNAME_P)),)
			DIST_SUFFIX := linux32
		endif
		CC = cc
		AR = ar
	endif
	ifeq ($(UNAME_S),Darwin)
		TARGET_OS := OSX
		DIST_SUFFIX := osx
		CC=clang
		CXX=clang++
		TARGET_CFLAGS   = -mmacosx-version-min=10.7 -arch i386 -arch x86_64
		TARGET_CXXFLAGS = -mmacosx-version-min=10.7 -arch i386 -arch x86_64 -stdlib=libc++
		TARGET_LDFLAGS  = -arch i386 -arch x86_64 -stdlib=libc++
	endif
	ARCHIVE_CMD := tar czf
	ARCHIVE_EXTENSION := tar.gz
	TARGET := mklfs
endif

OS := $(shell uname -s)

SIZE = size

SRC += $(wildcard *.c littlefs/*.c)
OBJ := $(SRC:.c=.o)
DEP := $(SRC:.c=.d)
ASM := $(SRC:.c=.s)

ifdef DEBUG
override CFLAGS += -O0 -g3
else
override CFLAGS += -Os
endif
ifdef WORD
override CFLAGS += -m$(WORD)
endif

override CFLAGS += -I. -Ilittlefs
override CFLAGS += -std=c99 -Wall -pedantic $(TARGET_CFLAGS)
override CFLAGS += -D_FILE_OFFSET_BITS=64
override CFLAGS += -D_XOPEN_SOURCE=700

override LFLAGS += -lfuse

ifeq ($(OS), FreeBSD)
override CFLAGS += -I /usr/local/include
override CFLAGS += -D __BSD_VISIBLE
override LFLAGS += -L /usr/local/lib
endif

all: $(TARGET)

asm: $(ASM)

size: $(OBJ)
	$(SIZE) -t $^

-include $(DEP)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $^ $(LFLAGS) -o $@

%.a: $(OBJ)
	$(AR) rcs $@ $^

%.o: %.c
	$(CC) -c -MMD $(CFLAGS) $< -o $@

%.s: %.c
	$(CC) -S $(CFLAGS) $< -o $@

clean:
	rm -f $(TARGET)
	rm -f $(OBJ)
	rm -f $(DEP)
	rm -f $(ASM)
