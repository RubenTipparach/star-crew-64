#include "level.h"

// FTL-ish ship layout: central 3-wide hallway, rooms along the sides,
// airlocks top and bottom. `.` = empty, tiles[row][col].
//   row 0  -> -Z edge (top of ship in 3/4 view)
//   col 0  -> -X edge (port side)
#define _ TILE_NONE
#define H TILE_HALLWAY
#define R TILE_ROOM
#define A TILE_AIRLOCK
static const TileType LEVEL_LAYOUT[LEVEL_HEIGHT][LEVEL_WIDTH] = {
    { _, A, A, A, _ },
    { R, H, H, H, R },
    { R, H, H, H, R },
    { R, H, H, H, R },
    { _, A, A, A, _ },
};
#undef _
#undef H
#undef R
#undef A

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

Level* level_create(void)
{
    Level *lv = &level_instance;

    // Copy layout in.
    for (int r = 0; r < LEVEL_HEIGHT; r++)
        for (int c = 0; c < LEVEL_WIDTH; c++)
            lv->tiles[r][c] = LEVEL_LAYOUT[r][c];

    // Load sprites (slot 0 left null; enum starts at 1).
    lv->textures[TILE_NONE]    = NULL;
    lv->textures[TILE_HALLWAY] = sprite_load("rom:/hallway.sprite");
    lv->textures[TILE_ROOM]    = sprite_load("rom:/room.sprite");
    lv->textures[TILE_AIRLOCK] = sprite_load("rom:/airlock.sprite");

    lv->quad = malloc_uncached(sizeof(T3DVertPacked) * 2);
    build_quad(lv->quad);

    // One matrix per tile position, centered on the origin.
    // World X = (col - (W-1)/2.0) * TILE_SIZE, Z similarly.
    lv->tile_matrices = malloc_uncached(sizeof(T3DMat4FP) * LEVEL_WIDTH * LEVEL_HEIGHT);
    for (int r = 0; r < LEVEL_HEIGHT; r++) {
        for (int c = 0; c < LEVEL_WIDTH; c++) {
            float wx = (c - (LEVEL_WIDTH  - 1) * 0.5f) * (float)TILE_SIZE;
            float wz = (r - (LEVEL_HEIGHT - 1) * 0.5f) * (float)TILE_SIZE;
            // Quad extends from (wx, 0, wz) to (wx+S, 0, wz+S); bias so
            // the tile's center sits at (wx, 0, wz).
            wx -= (float)TILE_SIZE * 0.5f;
            wz -= (float)TILE_SIZE * 0.5f;
            t3d_mat4fp_from_srt_euler(
                &lv->tile_matrices[r * LEVEL_WIDTH + c],
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

    for (int r = 0; r < LEVEL_HEIGHT; r++) {
        for (int c = 0; c < LEVEL_WIDTH; c++) {
            TileType t = lv->tiles[r][c];
            if (t == TILE_NONE) continue;

            if (t != last) {
                rdpq_sync_pipe();
                rdpq_sync_tile();
                rdpq_sprite_upload(TILE0, lv->textures[t], NULL);
                last = t;
            }

            t3d_matrix_push(&lv->tile_matrices[r * LEVEL_WIDTH + c]);
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
