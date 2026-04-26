#ifndef LEVEL_H
#define LEVEL_H

#include <stdbool.h>

#include "game_config.h"

// Each floor tile is a flat textured quad on the XZ plane.
#define TILE_SIZE       20   // world units per tile (character is ~16 tall, so 2m squares)
#define WALL_HEIGHT     30   // vertical wall quad height in world units
#define LEVEL_TEX_COUNT 3

typedef enum {
    TILE_NONE    = 0,
    TILE_HALLWAY = 1,
    TILE_ROOM    = 2,
    TILE_AIRLOCK = 3,
} TileType;

// A single wall quad; there's one per (cell, side) pair where the cell is
// walkable and the neighbour in that direction is TILE_NONE. We only emit
// walls on the -X and -Z sides of each cell so the 3/4 camera (looking down
// the +X+Z diagonal) sees the interior through the open +X / +Z sides.
typedef struct {
    T3DMat4FP matrix;        // world transform for this wall quad
    uint8_t   tile_type;     // the cell the wall belongs to — picks the texture
} Wall;

typedef struct {
    T3DVertPacked *quad;                                  // 2 packed structs = 4 verts, one tile-sized quad
    T3DVertPacked *wall_quad;                             // 2 packed structs = 4 verts, one wall-sized vertical quad
    T3DMat4FP     *tile_matrices;                         // grid_w * grid_h, one per tile (including empty)
    sprite_t      *textures[LEVEL_TEX_COUNT + 1];         // floor textures, indexed by TileType (slot 0 unused)
    sprite_t      *wall_textures[LEVEL_TEX_COUNT + 1];    // wall textures (airlock falls back to hallway)
    TileType      *tiles;                                 // grid_w * grid_h, row-major (row 0 at -Z)
    Wall          *walls;                                 // sorted by tile_type to group sprite uploads
    int            num_walls;
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

// Returns true if the cell containing the world-space point (wx, wz) is a
// walkable (non-empty) tile. Used for character-vs-map collision in main.c.
bool level_is_walkable(const Level *level, float wx, float wz);

#endif // LEVEL_H
