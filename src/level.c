#include <stdlib.h>
#include <string.h>

#include "level.h"
#include "level_format.h"

static Level level_instance = {0};

// Build a single TILE_SIZE × TILE_SIZE quad lying flat on XZ at the origin.
// The tile's own extent is placed relative to (0,0,0); per-tile positions
// are applied via tile_matrices.
static void build_quad(T3DVertPacked *out)
{
    const int16_t S = TILE_SIZE;
    uint16_t nUp = t3d_vert_pack_normal(&(T3DVec3){{0, 1, 0}});
    uint32_t white = 0xFFFFFFFF;

    // 4 verts of the quad, CCW viewed from above (+Y looking down):
    //   v0 (0,0,0)  v1 (S,0,0)  v2 (S,0,S)  v3 (0,0,S)
    int16_t u0 = UV(0), v0 = UV(0);
    int16_t u1 = UV(32), v1 = UV(32);

    out[0] = (T3DVertPacked){
        .posA = {0, 0, 0}, .rgbaA = white, .normA = nUp, .stA = {u0, v0},
        .posB = {S, 0, 0}, .rgbaB = white, .normB = nUp, .stB = {u1, v0},
    };
    out[1] = (T3DVertPacked){
        .posA = {S, 0, S}, .rgbaA = white, .normA = nUp, .stA = {u1, v1},
        .posB = {0, 0, S}, .rgbaB = white, .normB = nUp, .stB = {u0, v1},
    };
}

// Convert a cell coord (col along +X, row along +Z) to the world-space center
// of that tile. The grid is placed symmetrically around the origin.
static inline void cell_to_world(int grid_w, int grid_h, int col, int row,
                                 float *out_x, float *out_z)
{
    *out_x = (col - (grid_w - 1) * 0.5f) * (float)TILE_SIZE;
    *out_z = (row - (grid_h - 1) * 0.5f) * (float)TILE_SIZE;
}

// Read the full contents of a DFS file into a freshly malloc'd buffer. Caller
// owns the buffer and must free() it. Returns NULL on failure.
static uint8_t* dfs_read_all(const char *rom_path, int *out_size)
{
    int fd = dfs_open(rom_path);
    assertf(fd >= 0, "level_load: dfs_open('%s') failed (%d)", rom_path, fd);
    int size = dfs_size(fd);
    assertf(size >= (int)sizeof(LevelHeader),
            "level_load: '%s' is %d bytes, too small for a header", rom_path, size);
    uint8_t *buf = malloc((size_t)size);
    assertf(buf != NULL, "level_load: out of memory reading '%s' (%d bytes)", rom_path, size);
    int n = dfs_read(buf, 1, size, fd);
    dfs_close(fd);
    assertf(n == size, "level_load: short read on '%s' (%d of %d)", rom_path, n, size);
    *out_size = size;
    return buf;
}

Level* level_load(const char *rom_path)
{
    Level *lv = &level_instance;

    int size = 0;
    uint8_t *buf = dfs_read_all(rom_path, &size);

    // Header is big-endian on disk; N64 is MIPS big-endian so direct struct
    // reads match the file layout. (See comment in level_format.h.)
    const LevelHeader *hdr = (const LevelHeader*)buf;
    assertf(hdr->magic == LVL_MAGIC,
            "level_load: bad magic in '%s' (got 0x%08lx, want 0x%08x)",
            rom_path, (unsigned long)hdr->magic, LVL_MAGIC);
    assertf(hdr->version == LVL_VERSION,
            "level_load: '%s' is version %u, runtime expects %u",
            rom_path, hdr->version, LVL_VERSION);

    lv->grid_w = hdr->grid_w;
    lv->grid_h = hdr->grid_h;
    int num_entities = hdr->num_entities;
    int tile_count = lv->grid_w * lv->grid_h;
    assertf(tile_count > 0, "level_load: '%s' has zero-size grid", rom_path);

    // Byte layout: header (16) | tiles (grid_w*grid_h, padded to 4) | entities (20 each)
    int tile_offset = (int)sizeof(LevelHeader);
    int tile_padded = (tile_count + 3) & ~3;
    int ent_offset  = tile_offset + tile_padded;
    assertf(ent_offset + num_entities * (int)sizeof(LevelEntity) <= size,
            "level_load: '%s' truncated (need %d bytes, have %d)",
            rom_path, ent_offset + num_entities * (int)sizeof(LevelEntity), size);

    // Copy tiles out of the I/O buffer into our own allocation. The on-disk
    // tile values are a 1:1 match for TileType, so a memcpy suffices.
    lv->tiles = malloc(sizeof(TileType) * tile_count);
    assertf(lv->tiles != NULL, "level_load: out of memory for %d tiles", tile_count);
    for (int i = 0; i < tile_count; i++) {
        lv->tiles[i] = (TileType)buf[tile_offset + i];
    }

    // Scan entities for a "spawn" marker. First match wins.
    lv->has_spawn = false;
    lv->spawn_wx = 0.0f;
    lv->spawn_wz = 0.0f;
    for (int i = 0; i < num_entities; i++) {
        const LevelEntity *e = (const LevelEntity*)(buf + ent_offset + i * sizeof(LevelEntity));
        // group is NUL-padded but may fill the full field; strncmp bounds it safely.
        if (strncmp(e->group, "spawn", LVL_GROUP_LEN) == 0) {
            cell_to_world(lv->grid_w, lv->grid_h, e->x, e->z,
                          &lv->spawn_wx, &lv->spawn_wz);
            lv->has_spawn = true;
            break;
        }
    }

    free(buf);

    // Load sprites (slot 0 left null; enum starts at 1).
    lv->textures[TILE_NONE]    = NULL;
    lv->textures[TILE_HALLWAY] = sprite_load("rom:/hallway.sprite");
    lv->textures[TILE_ROOM]    = sprite_load("rom:/room.sprite");
    lv->textures[TILE_AIRLOCK] = sprite_load("rom:/airlock.sprite");

    lv->quad = malloc_uncached(sizeof(T3DVertPacked) * 2);
    build_quad(lv->quad);

    // One matrix per cell position, allocated for the full grid (empty cells
    // keep an unused matrix slot — cheap and simplifies indexing).
    lv->tile_matrices = malloc_uncached(sizeof(T3DMat4FP) * tile_count);
    for (int r = 0; r < lv->grid_h; r++) {
        for (int c = 0; c < lv->grid_w; c++) {
            float wx, wz;
            cell_to_world(lv->grid_w, lv->grid_h, c, r, &wx, &wz);
            // Quad spans (wx, 0, wz) to (wx+S, 0, wz+S) — bias so the tile's
            // center sits exactly at the cell_to_world coord.
            wx -= (float)TILE_SIZE * 0.5f;
            wz -= (float)TILE_SIZE * 0.5f;
            t3d_mat4fp_from_srt_euler(
                &lv->tile_matrices[r * lv->grid_w + c],
                (float[3]){1.0f, 1.0f, 1.0f},
                (float[3]){0.0f, 0.0f, 0.0f},
                (float[3]){wx, 0.0f, wz}
            );
        }
    }

    return lv;
}

void level_draw(Level *lv)
{
    rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
    rdpq_mode_filter(FILTER_BILINEAR);
    t3d_state_set_drawflags(T3D_FLAG_TEXTURED | T3D_FLAG_SHADED | T3D_FLAG_DEPTH);

    TileType last = TILE_NONE;  // avoid redundant tile uploads

    for (int r = 0; r < lv->grid_h; r++) {
        for (int c = 0; c < lv->grid_w; c++) {
            TileType t = lv->tiles[r * lv->grid_w + c];
            if (t == TILE_NONE) continue;

            if (t != last) {
                rdpq_sync_pipe();
                rdpq_sync_tile();
                rdpq_sprite_upload(TILE0, lv->textures[t], NULL);
                last = t;
            }

            t3d_matrix_push(&lv->tile_matrices[r * lv->grid_w + c]);
            t3d_vert_load(lv->quad, 0, 4);
            t3d_tri_draw(0, 1, 2);
            t3d_tri_draw(0, 2, 3);
            t3d_tri_sync();
            t3d_matrix_pop(1);
        }
    }
}

void level_get_center(Level *lv, float *out_x, float *out_z)
{
    (void)lv;
    *out_x = 0.0f;
    *out_z = 0.0f;
}

TileType level_tile_at(const Level *lv, int col, int row)
{
    if (col < 0 || col >= lv->grid_w || row < 0 || row >= lv->grid_h)
        return TILE_NONE;
    return lv->tiles[row * lv->grid_w + col];
}
