#ifndef LEVEL_H
#define LEVEL_H

#include <stdbool.h>

#include "game_config.h"

// Each floor tile is a flat textured quad on the XZ plane.
#define TILE_SIZE       20   // world units per tile (character is ~16 tall, so 2m squares)
#define LEVEL_TEX_COUNT 3

typedef enum {
    TILE_NONE    = 0,
    TILE_HALLWAY = 1,
    TILE_ROOM    = 2,
    TILE_AIRLOCK = 3,
} TileType;

typedef struct {
    T3DVertPacked *quad;                                  // 2 packed structs = 4 verts, one tile-sized quad
    T3DMat4FP     *tile_matrices;                         // grid_w * grid_h, one per tile (including empty)
    sprite_t      *textures[LEVEL_TEX_COUNT + 1];         // indexed by TileType (slot 0 unused)
    TileType      *tiles;                                 // grid_w * grid_h, row-major (row 0 at -Z)
    int            grid_w;
    int            grid_h;
    // World coords of the "spawn" entity (if one was found in the .lvl file).
    // When has_spawn is false, use level_get_center() instead.
    bool           has_spawn;
    float          spawn_wx;
    float          spawn_wz;
} Level;

// Load and construct a level from a .lvl file in the ROM filesystem (DFS).
// `rom_path` must be a DFS path like "rom:/starting.lvl". Terminates with an
// assertion failure if the file is missing or malformed — .lvl files are
// produced by tools/compile-levels.py at build time, so a failure here
// indicates an asset/build bug, not recoverable runtime state.
Level* level_load(const char *rom_path);

void level_draw(Level *level);

// World-space center of the level (grid is placed symmetrically around (0,0)).
void level_get_center(Level *level, float *out_x, float *out_z);

// Tile lookup in cell coords. Returns TILE_NONE for out-of-bounds cells.
TileType level_tile_at(const Level *level, int col, int row);

#endif // LEVEL_H
