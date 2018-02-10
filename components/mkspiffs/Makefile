BUILD_CONFIG_NAME ?= 

ifeq ($(OS),Windows_NT)
	TARGET_OS := WINDOWS
	DIST_SUFFIX := windows
	ARCHIVE_CMD := 7z a
	ARCHIVE_EXTENSION := zip
	TARGET := mkspiffs.exe
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
	TARGET := mkspiffs
endif

VERSION ?= $(shell git describe --always)

OBJ		:= main.o \
		   spiffs/src/spiffs_cache.o \
		   spiffs/src/spiffs_check.o \
		   spiffs/src/spiffs_gc.o \
		   spiffs/src/spiffs_hydrogen.o \
		   spiffs/src/spiffs_nucleus.o \

INCLUDES := -Itclap -Iinclude -Ispiffs/src -I.

override CFLAGS := -std=gnu99 -Os -Wall $(TARGET_CFLAGS) $(CFLAGS)
override CXXFLAGS := -std=gnu++11 -Os -Wall $(TARGET_CXXFLAGS) $(CXXFLAGS)
override LDFLAGS := $(TARGET_LDFLAGS) $(LDFLAGS)
override CPPFLAGS := $(INCLUDES) -D$(TARGET_OS) -DVERSION=\"$(VERSION)\" -D__NO_INLINE__ $(CPPFLAGS)

DIST_NAME := mkspiffs-$(VERSION)$(BUILD_CONFIG_NAME)-$(DIST_SUFFIX)
DIST_DIR := $(DIST_NAME)
DIST_ARCHIVE := $(DIST_NAME).$(ARCHIVE_EXTENSION)

.PHONY: all clean dist

all: $(TARGET)

dist: test $(DIST_ARCHIVE)

$(DIST_ARCHIVE): $(TARGET) $(DIST_DIR)
	cp $(TARGET) $(DIST_DIR)/
	$(ARCHIVE_CMD) $(DIST_ARCHIVE) $(DIST_DIR)

$(TARGET): $(OBJ)
	$(CXX) $^ -o $@ $(LDFLAGS)
	strip $(TARGET)

$(DIST_DIR):
	@mkdir -p $@

clean:
	@rm -f $(TARGET) $(OBJ)

SPIFFS_TEST_FS_CONFIG := -s 0x100000 -p 512 -b 0x2000

test: $(TARGET)
	mkdir -p spiffs_t
	cp spiffs/src/*.h spiffs_t/
	cp spiffs/src/*.c spiffs_t/
	rm -rf spiffs_t/.git
	rm -f spiffs_t/.DS_Store
	ls -1 spiffs_t > out.list0
	touch spiffs_t/.DS_Store
	mkdir -p spiffs_t/.git
	touch spiffs_t/.git/foo
	./mkspiffs -c spiffs_t $(SPIFFS_TEST_FS_CONFIG) out.spiffs_t | sort | sed s/^\\/// > out.list1
	./mkspiffs -u spiffs_u $(SPIFFS_TEST_FS_CONFIG) out.spiffs_t | sort | sed s/^\\/// > out.list_u
	./mkspiffs -l $(SPIFFS_TEST_FS_CONFIG) out.spiffs_t | cut -f 2 | sort | sed s/^\\/// > out.list2
	diff --strip-trailing-cr out.list0 out.list1
	diff --strip-trailing-cr out.list0 out.list2
	rm -rf spiffs_t/.git
	rm -f spiffs_t/.DS_Store
	diff spiffs_t spiffs_u
	rm -f out.{list0,list1,list2,list_u,spiffs_t}
	rm -R spiffs_u spiffs_t
