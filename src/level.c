#include <math.h>
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

// Build a wall-sized vertical quad in local space. Centered on X (so the
// rotation around Y keeps the wall's centre fixed), standing on Y=0 rising to
// Y=WALL_HEIGHT, facing +Z. UVs stretch the full 32×32 wall texture across:
// V=0 at the floor edge (baseboard in the PNG), V=32 at the ceiling edge.
static void build_wall_quad(T3DVertPacked *out)
{
    const int16_t S = TILE_SIZE;
    const int16_t H = WALL_HEIGHT;
    uint16_t nFront = t3d_vert_pack_normal(&(T3DVec3){{0, 0, 1}});
    uint32_t white = 0xFFFFFFFF;
    int16_t u0 = UV(0), u1 = UV(32);
    int16_t v0 = UV(0), v1 = UV(32);

    // v0 bottom-left, v1 bottom-right, v2 top-right, v3 top-left (CCW from +Z).
    out[0] = (T3DVertPacked){
        .posA = {-S/2, 0, 0}, .rgbaA = white, .normA = nFront, .stA = {u0, v0},
        .posB = { S/2, 0, 0}, .rgbaB = white, .normB = nFront, .stB = {u1, v0},
    };
    out[1] = (T3DVertPacked){
        .posA = { S/2, H, 0}, .rgbaA = white, .normA = nFront, .stA = {u1, v1},
        .posB = {-S/2, H, 0}, .rgbaB = white, .normB = nFront, .stB = {u0, v1},
    };
}

// Generate walls on the -X and -Z edges of every walkable cell whose matching
// neighbour is TILE_NONE. Iterates once per target tile type so walls end up
// grouped in the output array (keeps sprite uploads contiguous in draw()).
static void build_walls(Level *lv)
{
    // First pass: count. Same predicate as the emit pass below so the two
    // loops agree on the wall count.
    int count = 0;
    for (int r = 0; r < lv->grid_h; r++) {
        for (int c = 0; c < lv->grid_w; c++) {
            TileType t = lv->tiles[r * lv->grid_w + c];
            if (t == TILE_NONE) continue;
            if (level_tile_at(lv, c - 1, r) == TILE_NONE) count++;   // -X wall
            if (level_tile_at(lv, c,     r - 1) == TILE_NONE) count++;   // -Z wall
        }
    }

    lv->num_walls = count;
    lv->walls = (count > 0) ? malloc_uncached(sizeof(Wall) * count) : NULL;

    int idx = 0;
    for (int pass_type = TILE_HALLWAY; pass_type <= TILE_AIRLOCK; pass_type++) {
        for (int r = 0; r < lv->grid_h; r++) {
            for (int c = 0; c < lv->grid_w; c++) {
                TileType t = lv->tiles[r * lv->grid_w + c];
                if (t != pass_type) continue;

                float wx, wz;
                cell_to_world(lv->grid_w, lv->grid_h, c, r, &wx, &wz);

                // -X wall: rotate the +Z-facing base quad by +90° around Y so
                // the normal points +X (into the cell), and sit it on the
                // cell's western edge spanning the full Z extent.
                if (level_tile_at(lv, c - 1, r) == TILE_NONE) {
                    t3d_mat4fp_from_srt_euler(
                        &lv->walls[idx].matrix,
                        (float[3]){1.0f, 1.0f, 1.0f},
                        (float[3]){0.0f, 1.5707963f, 0.0f},
                        (float[3]){wx - (float)TILE_SIZE * 0.5f, 0.0f, wz}
                    );
                    lv->walls[idx].tile_type = (uint8_t)t;
                    idx++;
                }
                // -Z wall: no rotation needed; base quad already faces +Z.
                // Sit it on the cell's northern edge, centered on X.
                if (level_tile_at(lv, c, r - 1) == TILE_NONE) {
                    t3d_mat4fp_from_srt_euler(
                        &lv->walls[idx].matrix,
                        (float[3]){1.0f, 1.0f, 1.0f},
                        (float[3]){0.0f, 0.0f, 0.0f},
                        (float[3]){wx, 0.0f, wz - (float)TILE_SIZE * 0.5f}
                    );
                    lv->walls[idx].tile_type = (uint8_t)t;
                    idx++;
                }
            }
        }
    }
}

// Read a file from the ROM filesystem into a freshly malloc'd buffer. Uses
// asset_load() rather than dfs_open() because dfs_open() is the low-level
// DragonFS API and does NOT handle the "rom:/" VFS prefix — it would look
// for a file literally named "rom:/foo" and fail. asset_load() is the right
// entry point for raw binary blobs that travel through the same path-alias
// pipeline as sprite_load / fopen.
static uint8_t* load_level_blob(const char *rom_path, int *out_size)
{
    int size = 0;
    void *buf = asset_load(rom_path, &size);
    assertf(buf != NULL, "level_load: asset_load('%s') failed", rom_path);
    assertf(size >= (int)sizeof(LevelHeader),
            "level_load: '%s' is %d bytes, too small for a header", rom_path, size);
    *out_size = size;
    return (uint8_t*)buf;
}

Level* level_load(const char *rom_path)
{
    Level *lv = &level_instance;

    int size = 0;
    uint8_t *buf = load_level_blob(rom_path, &size);

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

    // Preserve the entity array for runtime scanning (Phase-7
    // extinguisher pickup, future event triggers). memcpy out of the
    // I/O buffer so we can free buf later.
    lv->num_entities = num_entities;
    lv->entities     = NULL;
    if (num_entities > 0) {
        lv->entities = malloc(sizeof(LevelEntity) * num_entities);
        assertf(lv->entities != NULL,
                "level_load: out of memory for %d entities", num_entities);
        memcpy(lv->entities, buf + ent_offset,
               sizeof(LevelEntity) * num_entities);
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

    // Phase-7 v2 trailer: rooms metadata + per-cell room id. Pre-v2 files
    // simply have no trailer; we degrade to num_rooms=0 + an all-0xFF
    // cell map so level_room_at always returns LEVEL_ROOM_NONE.
    int trailer_offset = ent_offset + num_entities * (int)sizeof(LevelEntity);
    lv->num_rooms     = 0;
    lv->rooms         = NULL;
    lv->cell_room_id  = malloc(tile_count);
    assertf(lv->cell_room_id != NULL,
            "level_load: out of memory for cell_room_id (%d bytes)", tile_count);
    memset(lv->cell_room_id, LEVEL_ROOM_NONE, tile_count);

    if (hdr->version >= 2 && trailer_offset + 4 <= size) {
        int nrooms = buf[trailer_offset];
        // 3 bytes pad after the count
        int rooms_offset = trailer_offset + 4;
        int rooms_bytes  = nrooms * (int)sizeof(LevelRoomRecord);
        int cell_offset  = rooms_offset + rooms_bytes;
        if (nrooms > 0 && cell_offset + tile_count <= size) {
            lv->num_rooms = nrooms;
            lv->rooms = malloc(sizeof(LevelRoom) * nrooms);
            assertf(lv->rooms != NULL, "level_load: out of memory for rooms");
            for (int i = 0; i < nrooms; i++) {
                const LevelRoomRecord *src = (const LevelRoomRecord*)
                    (buf + rooms_offset + i * sizeof(LevelRoomRecord));
                lv->rooms[i].id = src->id;
                memcpy(lv->rooms[i].name, src->name, LVL_ROOM_NAME_LEN);
                // ensure NUL-termination in the runtime copy
                lv->rooms[i].name[LVL_ROOM_NAME_LEN] = '\0';
            }
            memcpy(lv->cell_room_id, buf + cell_offset, tile_count);
        }
    }

    free(buf);

    // Load sprites (slot 0 left null; enum starts at 1).
    lv->textures[TILE_NONE]    = NULL;
    lv->textures[TILE_HALLWAY] = sprite_load("rom:/hallway.sprite");
    lv->textures[TILE_ROOM]    = sprite_load("rom:/room.sprite");
    lv->textures[TILE_AIRLOCK] = sprite_load("rom:/airlock.sprite");

    // Wall sprites. No dedicated airlock wall texture yet — fall back to the
    // hallway wall so those cells still render rather than going untextured.
    lv->wall_textures[TILE_NONE]    = NULL;
    lv->wall_textures[TILE_HALLWAY] = sprite_load("rom:/hallway_wall.sprite");
    lv->wall_textures[TILE_ROOM]    = sprite_load("rom:/room_wall.sprite");
    lv->wall_textures[TILE_AIRLOCK] = lv->wall_textures[TILE_HALLWAY];

    lv->quad = malloc_uncached(sizeof(T3DVertPacked) * 2);
    build_quad(lv->quad);
    lv->wall_quad = malloc_uncached(sizeof(T3DVertPacked) * 2);
    build_wall_quad(lv->wall_quad);
    build_walls(lv);

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

    // Walls — sorted by tile_type in build_walls, so the sprite upload only
    // changes at type boundaries.
    last = TILE_NONE;
    for (int i = 0; i < lv->num_walls; i++) {
        TileType wt = (TileType)lv->walls[i].tile_type;
        if (wt != last) {
            rdpq_sync_pipe();
            rdpq_sync_tile();
            rdpq_sprite_upload(TILE0, lv->wall_textures[wt], NULL);
            last = wt;
        }
        t3d_matrix_push(&lv->walls[i].matrix);
        t3d_vert_load(lv->wall_quad, 0, 4);
        t3d_tri_draw(0, 1, 2);
        t3d_tri_draw(0, 2, 3);
        t3d_tri_sync();
        t3d_matrix_pop(1);
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

bool level_is_walkable(const Level *lv, float wx, float wz)
{
    // Inverse of cell_to_world: that function maps col → (col − (W−1)/2)·S,
    // placing cell centres at odd half-integers in cell space. Each cell
    // covers a half-open [centre−S/2, centre+S/2) interval, which floor()
    // maps cleanly via `(wx/S + W/2)`.
    int col = (int)floorf(wx / (float)TILE_SIZE + lv->grid_w * 0.5f);
    int row = (int)floorf(wz / (float)TILE_SIZE + lv->grid_h * 0.5f);
    return level_tile_at(lv, col, row) != TILE_NONE;
}

uint8_t level_room_at(const Level *lv, float wx, float wz)
{
    if (!lv || !lv->cell_room_id) return LEVEL_ROOM_NONE;
    int col = (int)floorf(wx / (float)TILE_SIZE + lv->grid_w * 0.5f);
    int row = (int)floorf(wz / (float)TILE_SIZE + lv->grid_h * 0.5f);
    if (col < 0 || col >= lv->grid_w) return LEVEL_ROOM_NONE;
    if (row < 0 || row >= lv->grid_h) return LEVEL_ROOM_NONE;
    return lv->cell_room_id[row * lv->grid_w + col];
}
