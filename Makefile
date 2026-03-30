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
CFLAGS := -3r -fpi87 -fp3 -s -ox -mf -bt=dos -i=$(WATCOM)/h

# --- Project files ---
SRCDIR  := src
BUILDDIR := build
TARGET  := $(BUILDDIR)/demo.exe

SRCS    := $(wildcard $(SRCDIR)/*.c)
OBJS    := $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.obj,$(SRCS))

# --- Rules ---
.PHONY: all clean run

all: $(TARGET)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.obj: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -fo=$@ $<

$(TARGET): $(OBJS)
	$(WLINK) name $@ system dos32a op stub=$(WATCOM)/binw/dos32a.exe $(patsubst %,file %,$(OBJS))

clean:
	rm -rf $(BUILDDIR)

run: $(TARGET)
	./run.sh
