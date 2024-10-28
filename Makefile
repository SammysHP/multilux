
# can use a local build of hidapi with
# HIDAPI_DIR=~/hidapi make
HIDAPI_DIR ?= ""

UNAME := $(shell uname -s)
ARCH := $(shell uname -m)

ifeq "$(OS)" "Windows_NT"
	OS=windows
endif
ifeq "$(UNAME)" "Darwin"
	OS=macos
endif
ifeq "$(UNAME)" "Linux"
	OS=linux
endif
ifeq "$(UNAME)" "FreeBSD"
	OS=freebsd
endif

# Windows
ifeq "$(OS)" "windows"

ifeq (default,$(origin CC))
  CC = gcc
endif

LIBS += -lsetupapi -Wl,--enable-auto-import -static-libgcc -static-libstdc++ -lm
OBJS = $(HIDAPI_DIR)/windows/hid.o
EXE=.exe
endif

# Mac
ifeq "$(OS)" "macos"
CFLAGS+=-arch x86_64 -arch arm64
LIBS=-framework IOKit -framework CoreFoundation -framework AppKit -lm
OBJS=$(HIDAPI_DIR)/mac/hid.o
EXE=
endif

# Linux (hidraw)
ifeq "$(OS)" "linux"
PKGS = libudev libmosquitto

ifneq ($(wildcard $(HIDAPI_DIR)),)
OBJS = $(HIDAPI_DIR)/linux/hid.o
else
PKGS += hidapi-hidraw hidapi-libusb
endif

CFLAGS += $(shell pkg-config --cflags $(PKGS))
LIBS = $(shell pkg-config --libs $(PKGS)) -lm
EXE=
endif


# FreeBSD
ifeq "$(OS)" "freebsd"
CFLAGS += -I/usr/local/include
OBJS = $(HIDAPI_DIR)/libusb/hid.o
LIBS += -L/usr/local/lib -lusb -liconv -pthread -lm
EXE=
endif

# shared

CFLAGS += -I $(HIDAPI_DIR)/hidapi
OBJS += cp2112.o

all: multilux mlx90614_read

$(OBJS) multilux.o mlx90614_read.o: %.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

multilux: multilux.o $(OBJS)
	$(CC) $(CFLAGS) multilux.o $(OBJS) -o multilux$(EXE) $(LIBS)

mlx90614_read: mlx90614_read.o $(OBJS)
	$(CC) $(CFLAGS) mlx90614_read.o $(OBJS) -o mlx90614_read$(EXE) $(LIBS)

clean:
	rm -f $(OBJS) multilux.o mlx90614_read.o
	rm -f multilux$(EXE)
	rm -f mlx90614_read$(EXE)

