# star-crew-64 - N64 Game Makefile
# Uses libdragon + tiny3d

BUILD_DIR = build
SRC_DIR = src
ASSETS_DIR = assets

# Path to tiny3d (local copy in this repo)
T3D_INST = $(shell pwd)/tiny3d

# Include libdragon and tiny3d build rules
include $(N64_INST)/include/n64.mk
include $(T3D_INST)/t3d.mk

# Compiler flags
N64_CFLAGS += -std=gnu2x -Os -I$(SRC_DIR)

# Python interpreter — MSYS2 only ships `python`, macOS/Linux usually ship
# `python3`. Used for both level compilation (below) and the STARTING_MAP
# lookup just after this.
PYTHON ?= $(shell command -v python3 2>/dev/null || command -v python 2>/dev/null || echo python3)

# Pick up the starting-map name from levels/levels.json so the game always
# boots whichever map the config file points at. Falls back to "starting" if
# the JSON lookup fails (missing file, bad shape) so the build still goes.
STARTING_MAP := $(shell $(PYTHON) -c "import json; print(json.load(open('levels/levels.json'))['starting_map'])" 2>/dev/null)
ifeq ($(STARTING_MAP),)
STARTING_MAP := starting
endif
N64_CFLAGS += -DSTARTING_LEVEL=\"$(STARTING_MAP)\"

# Project name
PROJECT_NAME = star-crew-64

# Source files
SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Asset conversion - PNG to sprites (top level + textures/ subdir)
assets_png = $(wildcard $(ASSETS_DIR)/*.png) $(wildcard $(ASSETS_DIR)/textures/*.png)
assets_sprites = $(addprefix filesystem/,$(notdir $(assets_png:%.png=%.sprite)))

# Asset conversion - WAV to wav64
assets_wav = $(wildcard $(ASSETS_DIR)/*.wav)
assets_audio = $(addprefix filesystem/,$(notdir $(assets_wav:%.wav=%.wav64)))

# Level compilation (JSON -> .lvl) — levels/levels.json is config, not a map.
LEVEL_CONFIG  = levels/levels.json
LEVEL_SOURCES = $(filter-out $(LEVEL_CONFIG), $(wildcard levels/*.json))
LEVEL_BINS    = $(LEVEL_SOURCES:levels/%.json=filesystem/%.lvl)

# All converted assets
assets_conv = $(assets_sprites) $(assets_audio) $(LEVEL_BINS)

# Main target
all: $(PROJECT_NAME).z64

# Star billboards: convert with RGBA16 instead of CI4. The PNGs from
# gen-textures.py have transparent corners (alpha=0) around a small
# opaque core+halo, but CI4's palette quantization in mksprite was
# baking those transparent pixels as opaque palette entries — so
# alpha-compare passed for every pixel and the stars rendered as solid
# 8×8 squares. RGBA16 stores a real 1-bit alpha per pixel, so the
# transparent corners survive end-to-end. Define this BEFORE the
# generic %.sprite rules so Make picks the more-specific star_% match.
filesystem/star_%.sprite: $(ASSETS_DIR)/textures/star_%.png
	@mkdir -p $(dir $@)
	@echo "    [SPRITE] $@ (RGBA16)"
	$(N64_MKSPRITE) $(MKSPRITE_FLAGS) -f RGBA16 -o filesystem "$<"

# Convert PNG to sprite (top-level)
filesystem/%.sprite: $(ASSETS_DIR)/%.png
	@mkdir -p $(dir $@)
	@echo "    [SPRITE] $@"
	$(N64_MKSPRITE) $(MKSPRITE_FLAGS) -f CI4 -o filesystem "$<"

# Convert PNG to sprite (assets/textures/ subdir — same flags)
filesystem/%.sprite: $(ASSETS_DIR)/textures/%.png
	@mkdir -p $(dir $@)
	@echo "    [SPRITE] $@"
	$(N64_MKSPRITE) $(MKSPRITE_FLAGS) -f CI4 -o filesystem "$<"

# Convert WAV to wav64
filesystem/%.wav64: $(ASSETS_DIR)/%.wav
	@mkdir -p $(dir $@)
	@echo "    [AUDIO] $@"
	$(N64_AUDIOCONV) --wav-compress 1 -o filesystem "$<"

# Compile level editor JSON -> .lvl binary
filesystem/%.lvl: levels/%.json tools/compile-levels.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/compile-levels.py "$<" "$@"

# DFS filesystem depends on converted assets
$(BUILD_DIR)/$(PROJECT_NAME).dfs: $(assets_conv)

# ELF depends on object files
$(BUILD_DIR)/$(PROJECT_NAME).elf: $(OBJS)

# ROM settings - include DFS
$(PROJECT_NAME).z64: N64_ROM_TITLE="star-crew-64"
$(PROJECT_NAME).z64: $(BUILD_DIR)/$(PROJECT_NAME).dfs

# Build object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) -c $(N64_CFLAGS) -o $@ $<

# Clean
clean:
	rm -rf $(BUILD_DIR) filesystem *.z64

# Rebuild
rebuild: clean all

-include $(wildcard $(BUILD_DIR)/*.d)

.PHONY: all clean rebuild
