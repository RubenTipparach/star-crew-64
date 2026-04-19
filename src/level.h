#ifndef LEVEL_H
#define LEVEL_H

#include "game_config.h"

// Each floor tile is a flat textured quad on the XZ plane.
#define TILE_SIZE       20   // world units per tile (character is ~16 tall, so 2m squares)
#define LEVEL_WIDTH     5    // in tiles
#define LEVEL_HEIGHT    5    // in tiles
#define LEVEL_TEX_COUNT 3

typedef enum {
    TILE_NONE    = 0,
    TILE_HALLWAY = 1,
    TILE_ROOM    = 2,
    TILE_AIRLOCK = 3,
} TileType;

typedef struct {
    T3DVertPacked *quad;                               // 2 packed structs = 4 verts, one tile-sized quad
    T3DMat4FP     *tile_matrices;                      // LEVEL_WIDTH * LEVEL_HEIGHT, static
    sprite_t      *textures[LEVEL_TEX_COUNT + 1];      // indexed by TileType (slot 0 unused)
    TileType       tiles[LEVEL_HEIGHT][LEVEL_WIDTH];
} Level;

Level* level_create(void);
void level_draw(Level *level);

// World-space center of the level (useful for camera target).
void level_get_center(Level *level, float *out_x, float *out_z);

#endif // LEVEL_H
