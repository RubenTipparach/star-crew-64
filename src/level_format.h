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
//   --- v2+ optional trailer ---
//   [u8 num_rooms]                              1 byte + 3-byte pad
//   [LevelRoom * num_rooms]                     16 bytes each
//   [u8 cell_room_id[grid_w * grid_h]]          padded to 4-byte alignment
//
// Tile bytes use the LVL_TILE_* values below. Entity (x, z) are cell
// coordinates in the same grid; multiply by TILE_SIZE at load time to get
// world units. Group names are a short fixed-width string the game can
// switch on (NUL-padded, not necessarily NUL-terminated if full length).
//
// The rooms trailer (v2) preserves the editor's room metadata for
// runtime systems that care about which room contains a given tile —
// fire suppression, in particular. Cells outside any room have
// cell_room_id = 0xFF. Older v1 files have no trailer; loaders treat
// num_rooms as zero in that case.

#include <stdint.h>

#define LVL_MAGIC       0x53544C56u  // 'S','T','L','V'
#define LVL_VERSION     2

// Room metadata length. id (1 byte) + name (15 bytes) = 16 bytes, so
// each room record fits in one cache line and the array stays aligned.
#define LVL_ROOM_NAME_LEN  15
#define LVL_ROOM_NONE      0xFFu

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

// On-disk room record. Distinct from the runtime LevelRoom (in level.h)
// which has a NUL-terminator slot baked into name[].
typedef struct __attribute__((packed)) {
    uint8_t  id;                      // matches the index into the rooms array
    char     name[LVL_ROOM_NAME_LEN]; // NUL-padded short name from the editor
} LevelRoomRecord;

_Static_assert(sizeof(LevelHeader)     == 16, "LevelHeader size drift");
_Static_assert(sizeof(LevelEntity)     == 20, "LevelEntity size drift");
_Static_assert(sizeof(LevelRoomRecord) == 16, "LevelRoomRecord size drift");

#endif // LEVEL_FORMAT_H
