# DOS Demo Makefile - uses Open Watcom V2 for cross-compilation to DOS/32A
#
# Usage:
#   make          - build demo.exe
#   make clean    - remove build artifacts
#   make run      - build and launch in DOSBox-X

# --- Toolchain setup ---
WATCOM   := $(CURDIR)/tools/watcom
HOSTBIN  := $(shell uname -s | grep -q Darwin && (uname -m | grep -q arm64 && echo armo64 || echo osx64) || (uname -m | grep -q x86_64 && echo binl64 || echo binl))

CC       := $(WATCOM)/$(HOSTBIN)/wcc386
WLINK    := $(WATCOM)/$(HOSTBIN)/wlink

export WATCOM
export PATH := $(WATCOM)/$(HOSTBIN):$(PATH)

# --- Compiler flags ---
# -3r   = 386, register calling convention
# -fpi87= inline 80x87 FPU instructions
# -fp3  = 387 FPU code generation
# -s    = remove stack overflow checks
# -ox   = maximum optimization
# -mf   = flat memory model
# -bt=dos = target DOS
# -i=   = include paths
LIBXMP_DIR := libs/libxmp-lite
CFLAGS := -3r -fpi87 -fp3 -s -ox -mf -bt=dos -i=$(WATCOM)/h -i=$(LIBXMP_DIR)/include/libxmp-lite -i=$(LIBXMP_DIR)/src -DLIBXMP_CORE_PLAYER -DLIBXMP_NO_PROWIZARD

# --- Project files ---
SRCDIR  := src
BUILDDIR := build
TARGET  := $(BUILDDIR)/demo.exe

SRCS    := $(wildcard $(SRCDIR)/*.c)
OBJS    := $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.obj,$(SRCS))

LIBXMP_SRCS   := $(wildcard $(LIBXMP_DIR)/src/*.c)
LIBXMP_LSRCS  := $(wildcard $(LIBXMP_DIR)/src/loaders/*.c)
LIBXMP_OBJS   := $(patsubst $(LIBXMP_DIR)/src/%.c,$(BUILDDIR)/xmp_%.obj,$(LIBXMP_SRCS))
LIBXMP_LOBJS  := $(patsubst $(LIBXMP_DIR)/src/loaders/%.c,$(BUILDDIR)/xmpl_%.obj,$(LIBXMP_LSRCS))

# --- Rules ---
.PHONY: all clean run

ASSETS := $(wildcard assets/*.bmp) $(wildcard assets/*.xm)
BUILD_ASSETS := $(patsubst assets/%,$(BUILDDIR)/%,$(ASSETS))

all: $(TARGET) $(BUILD_ASSETS) assets/palette.bmp

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.obj: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -fo=$@ $<

$(BUILDDIR)/xmp_%.obj: $(LIBXMP_DIR)/src/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -fo=$@ $<

$(BUILDDIR)/xmpl_%.obj: $(LIBXMP_DIR)/src/loaders/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -fo=$@ $<

$(TARGET): $(OBJS) $(LIBXMP_OBJS) $(LIBXMP_LOBJS)
	$(WLINK) name $@ system dos32a op stub=$(WATCOM)/binw/dos32a.exe $(patsubst %,file %,$(OBJS) $(LIBXMP_OBJS) $(LIBXMP_LOBJS))

assets/palette.bmp: tools/make_palette.py
	python3 tools/make_palette.py

$(BUILDDIR)/%: assets/% | $(BUILDDIR)
	cp $< $@

clean:
	rm -rf $(BUILDDIR)

run: $(TARGET) $(BUILDDIR)/music.xm
	./run.sh
