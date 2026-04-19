#ifndef LEVEL_FORMAT_H
#define LEVEL_FORMAT_H

// Binary format written by tools/compile-levels.py and (eventually) read by
// the runtime level loader. All multi-byte fields are big-endian to match the
// N64 CPU byte order — the file is mmap-able from DFS directly into these
// structs, no byteswapping needed.
//
// Layout:
//   [LevelHeader                            ]  16 bytes
//   [tile grid: grid_w * grid_h bytes       ]  padded to 4-byte alignment
//   [entities: num_entities * LevelEntity   ]  20 bytes each
//
// Tile bytes use the LVL_TILE_* values below. Entity (x, z) are cell
// coordinates in the same grid; multiply by TILE_SIZE at load time to get
// world units. Group names are a short fixed-width string the game can
// switch on (NUL-padded, not necessarily NUL-terminated if full length).

#include <stdint.h>

#define LVL_MAGIC       0x53544C56u  // 'S','T','L','V'
#define LVL_VERSION     1

// Tile values. Match TileType in level.h where possible so the runtime can
// memcpy from the DFS buffer straight into Level.tiles.
#define LVL_TILE_EMPTY    0
#define LVL_TILE_HALLWAY  1
#define LVL_TILE_ROOM     2
#define LVL_TILE_AIRLOCK  3

#define LVL_GROUP_LEN   16

typedef struct __attribute__((packed)) {
    uint32_t magic;          // LVL_MAGIC
    uint16_t version;        // LVL_VERSION
    uint16_t flags;          // reserved, 0
    uint16_t grid_w;
    uint16_t grid_h;
    uint16_t num_entities;
    uint16_t reserved;
} LevelHeader;

typedef struct __attribute__((packed)) {
    int16_t  x;                       // cell coord along +X
    int16_t  z;                       // cell coord along +Z
    char     group[LVL_GROUP_LEN];    // NUL-padded short name
} LevelEntity;

_Static_assert(sizeof(LevelHeader) == 16, "LevelHeader size drift");
_Static_assert(sizeof(LevelEntity) == 20, "LevelEntity size drift");

#endif // LEVEL_FORMAT_H
