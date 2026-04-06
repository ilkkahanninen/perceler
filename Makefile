# Perceler Makefile - uses Open Watcom V2 for cross-compilation to DOS/32A
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
LIBXMP_DIR := libs/libxmp-lite
# --- Project files ---
SRCDIR  := src
BUILDDIR := build

CFLAGS := -5 -fpi87 -fp3 -s -otexan -zp4 -oa -mf -bt=dos -i=$(WATCOM)/h -i=$(LIBXMP_DIR)/include/libxmp-lite -i=$(LIBXMP_DIR)/src -i=$(SRCDIR) -i=$(SRCDIR)/engine -DLIBXMP_CORE_PLAYER -DLIBXMP_NO_PROWIZARD
TARGET  := $(BUILDDIR)/demo.exe

MAIN_SRCS    := $(wildcard $(SRCDIR)/*.c)
ENGINE_SRCS  := $(wildcard $(SRCDIR)/engine/*.c)
SCENE_SRCS   := $(wildcard $(SRCDIR)/scenes/*.c)
UTILS_SRCS   := $(wildcard $(SRCDIR)/scenes/utils/*.c)
SRCS         := $(MAIN_SRCS) $(ENGINE_SRCS) $(SCENE_SRCS) $(UTILS_SRCS)

MAIN_OBJS    := $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.obj,$(MAIN_SRCS))
ENGINE_OBJS  := $(patsubst $(SRCDIR)/engine/%.c,$(BUILDDIR)/eng_%.obj,$(ENGINE_SRCS))
SCENE_OBJS   := $(patsubst $(SRCDIR)/scenes/%.c,$(BUILDDIR)/scn_%.obj,$(SCENE_SRCS))
UTILS_OBJS   := $(patsubst $(SRCDIR)/scenes/utils/%.c,$(BUILDDIR)/utl_%.obj,$(UTILS_SRCS))
OBJS         := $(MAIN_OBJS) $(ENGINE_OBJS) $(SCENE_OBJS) $(UTILS_OBJS)

LIBXMP_SRCS   := $(wildcard $(LIBXMP_DIR)/src/*.c)
LIBXMP_LSRCS  := $(wildcard $(LIBXMP_DIR)/src/loaders/*.c)
LIBXMP_OBJS   := $(patsubst $(LIBXMP_DIR)/src/%.c,$(BUILDDIR)/xmp_%.obj,$(LIBXMP_SRCS))
LIBXMP_LOBJS  := $(patsubst $(LIBXMP_DIR)/src/loaders/%.c,$(BUILDDIR)/xmpl_%.obj,$(LIBXMP_LSRCS))

# --- PNG to BMP conversion ---
ASSET_SRCDIR  := asset-sources
PNG_SOURCES   := $(wildcard $(ASSET_SRCDIR)/*.png)
PNG_ASSETS    := $(patsubst $(ASSET_SRCDIR)/%.png,assets/%.bmp,$(PNG_SOURCES))
PALETTE_BMP   := $(ASSET_SRCDIR)/palette.bmp

# --- OBJ to MDL conversion ---
OBJ_SOURCES   := $(wildcard $(ASSET_SRCDIR)/*.obj)
OBJ_ASSETS    := $(patsubst $(ASSET_SRCDIR)/%.obj,assets/%.mdl,$(OBJ_SOURCES))

# --- Asset packing ---
ASSET_FILES := $(sort $(wildcard assets/*) $(PNG_ASSETS) $(OBJ_ASSETS))
ASSET_DAT   := $(BUILDDIR)/demo.dat
ASSET_HDR   := $(SRCDIR)/assets.h

# --- Rules ---
.PHONY: all clean run assets

all: $(TARGET) $(ASSET_DAT)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# Pack assets and generate header (must run before compiling sources)
$(ASSET_DAT) $(ASSET_HDR): $(ASSET_FILES) $(PNG_ASSETS) tools/pack_assets.py | $(BUILDDIR)
	python3 tools/pack_assets.py $(BUILDDIR) $(SRCDIR) $(ASSET_FILES)

assets/%.bmp: $(ASSET_SRCDIR)/%.png $(PALETTE_BMP) tools/png2bmp.py
	python3 tools/png2bmp.py $< $@ -p $(PALETTE_BMP)

assets/%.mdl: $(ASSET_SRCDIR)/%.obj tools/obj2model.py
	python3 tools/obj2model.py $< $@

assets: $(ASSET_DAT)

# All object files depend on the generated asset header
$(OBJS): $(ASSET_HDR)

$(BUILDDIR)/%.obj: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -fo=$@ $<

$(BUILDDIR)/eng_%.obj: $(SRCDIR)/engine/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -fo=$@ $<

$(BUILDDIR)/scn_%.obj: $(SRCDIR)/scenes/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -fo=$@ $<

$(BUILDDIR)/utl_%.obj: $(SRCDIR)/scenes/utils/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -fo=$@ $<

$(BUILDDIR)/xmp_%.obj: $(LIBXMP_DIR)/src/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -fo=$@ $<

$(BUILDDIR)/xmpl_%.obj: $(LIBXMP_DIR)/src/loaders/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -fo=$@ $<

$(TARGET): $(OBJS) $(LIBXMP_OBJS) $(LIBXMP_LOBJS)
	$(WLINK) name $@ system dos32a op stub=$(WATCOM)/binw/dos32a.exe $(patsubst %,file %,$(OBJS) $(LIBXMP_OBJS) $(LIBXMP_LOBJS))

RELEASEDIR ?= release

release: all
	mkdir -p $(RELEASEDIR)
	cp $(BUILDDIR)/demo.exe $(RELEASEDIR)/
	cp $(BUILDDIR)/demo.dat $(RELEASEDIR)/

WATCH_DIRS := $(SRCDIR) assets $(ASSET_SRCDIR)
WATCH_STAMP := $(BUILDDIR)/.watch_stamp

watch: | $(BUILDDIR)
	@echo "Watching for changes..."
	@$(MAKE) --no-print-directory; touch $(WATCH_STAMP); \
	while true; do \
		sleep 1; \
		if [ -n "$$(find $(WATCH_DIRS) Makefile -type f -newer $(WATCH_STAMP) 2>/dev/null | head -1)" ]; then \
			echo ""; \
			echo "Change detected, rebuilding..."; \
			$(MAKE) --no-print-directory; \
			touch $(WATCH_STAMP); \
		fi; \
	done

clean:
	rm -rf $(BUILDDIR) $(ASSET_HDR) *.err

DEMO_ARGS ?=

run: all
ifdef DEMO_ARGS
	@sed 's/^demo\.exe$$/demo.exe $(DEMO_ARGS)/' dosbox-x.conf > $(BUILDDIR)/dosbox-x.conf
	dosbox-x -conf $(BUILDDIR)/dosbox-x.conf
else
	dosbox-x -conf dosbox-x.conf
endif
