#include <math.h>
#include <stdlib.h>

#include "weapons_console.h"

// Distance (world units) at which the player can interact with the console.
// Slightly tighter than the bridge panel's 22 — a gunner needs to be right
// at the station.
#define WEAPONS_INTERACT_RADIUS 20.0f

// Cooldown between shots (frames @ ~60Hz). 18 → ~3 shots/sec, fast enough
// to feel responsive without filling the projectile pool instantly.
#define WEAPONS_COOLDOWN_FRAMES 18

static WeaponsConsole console_instance = {0};

// Same shared build pattern as bridge_panel: each tri becomes two packed
// structs (slot 3 is a duplicate of slot 0, never indexed).
static void build_verts(T3DVertPacked *out)
{
    uint32_t white = 0xFFFFFFFF;
    for (int t = 0; t < WEAPONS_PANEL_NUM_TRIS; t++) {
        const float *n = WEAPONS_PANEL_TRI_NORMAL[t];
        T3DVec3 nv = {{n[0], n[1], n[2]}};
        uint16_t pn = t3d_vert_pack_normal(&nv);

        int v0 = t * 3 + 0;
        int v1 = t * 3 + 1;
        int v2 = t * 3 + 2;

        int16_t u0  = UV(WEAPONS_PANEL_UV[v0][0] * 32.0f);
        int16_t v0t = UV(WEAPONS_PANEL_UV[v0][1] * 32.0f);
        int16_t u1  = UV(WEAPONS_PANEL_UV[v1][0] * 32.0f);
        int16_t v1t = UV(WEAPONS_PANEL_UV[v1][1] * 32.0f);
        int16_t u2  = UV(WEAPONS_PANEL_UV[v2][0] * 32.0f);
        int16_t v2t = UV(WEAPONS_PANEL_UV[v2][1] * 32.0f);

        out[t * 2 + 0] = (T3DVertPacked){
            .posA = {WEAPONS_PANEL_POS[v0][0], WEAPONS_PANEL_POS[v0][1], WEAPONS_PANEL_POS[v0][2]},
            .rgbaA = white, .normA = pn, .stA = {u0, v0t},
            .posB = {WEAPONS_PANEL_POS[v1][0], WEAPONS_PANEL_POS[v1][1], WEAPONS_PANEL_POS[v1][2]},
            .rgbaB = white, .normB = pn, .stB = {u1, v1t},
        };
        out[t * 2 + 1] = (T3DVertPacked){
            .posA = {WEAPONS_PANEL_POS[v2][0], WEAPONS_PANEL_POS[v2][1], WEAPONS_PANEL_POS[v2][2]},
            .rgbaA = white, .normA = pn, .stA = {u2, v2t},
            .posB = {WEAPONS_PANEL_POS[v0][0], WEAPONS_PANEL_POS[v0][1], WEAPONS_PANEL_POS[v0][2]},
            .rgbaB = white, .normB = pn, .stB = {u0, v0t},
        };
    }
}

WeaponsConsole* weapons_console_create(float x, float z, float facing_yaw)
{
    WeaponsConsole *w = &console_instance;
    w->position = (T3DVec3){{x, 0.0f, z}};
    w->rot_y = facing_yaw;
    w->player_in_range = false;
    w->prev_b = false;
    w->fire_requested = false;
    w->cooldown = 0;

    w->texture = sprite_load("rom:/weapons_panel.sprite");
    w->verts   = malloc_uncached(sizeof(T3DVertPacked) * WEAPONS_PANEL_NUM_TRIS * 2);
    w->matrix  = malloc_uncached(sizeof(T3DMat4FP));

    build_verts(w->verts);

    t3d_mat4fp_from_srt_euler(w->matrix,
        (float[3]){1.0f, 1.0f, 1.0f},
        (float[3]){0.0f, facing_yaw, 0.0f},
        (float[3]){x, 0.0f, z}
    );

    return w;
}

void weapons_console_update(WeaponsConsole *w, float player_x, float player_z,
                            joypad_inputs_t inputs)
{
    float dx = player_x - w->position.v[0];
    float dz = player_z - w->position.v[2];
    float d2 = dx * dx + dz * dz;
    w->player_in_range = (d2 <= WEAPONS_INTERACT_RADIUS * WEAPONS_INTERACT_RADIUS);

    if (w->cooldown > 0) {
        w->cooldown--;
    }

    bool b_now = inputs.btn.b != 0;
    bool b_pressed = b_now && !w->prev_b;
    w->prev_b = b_now;

    if (w->player_in_range && b_pressed && w->cooldown == 0) {
        w->fire_requested = true;
        w->cooldown = WEAPONS_COOLDOWN_FRAMES;
    }
}

bool weapons_console_consume_fire(WeaponsConsole *w)
{
    bool fired = w->fire_requested;
    w->fire_requested = false;
    return fired;
}

void weapons_console_draw(WeaponsConsole *w)
{
    rdpq_sync_pipe();
    rdpq_sync_tile();
    rdpq_sprite_upload(TILE0, w->texture, NULL);

    rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
    rdpq_mode_filter(FILTER_BILINEAR);
    t3d_state_set_drawflags(T3D_FLAG_TEXTURED | T3D_FLAG_SHADED | T3D_FLAG_DEPTH);

    t3d_matrix_push(w->matrix);
    const int TRIS_PER_LOAD = 6;
    for (int tri = 0; tri < WEAPONS_PANEL_NUM_TRIS; tri += TRIS_PER_LOAD) {
        int chunk = WEAPONS_PANEL_NUM_TRIS - tri;
        if (chunk > TRIS_PER_LOAD) chunk = TRIS_PER_LOAD;
        t3d_vert_load(w->verts + tri * 2, 0, chunk * 4);
        for (int i = 0; i < chunk; i++) {
            int base = i * 4;
            t3d_tri_draw(base + 0, base + 1, base + 2);
        }
        t3d_tri_sync();
    }
    t3d_matrix_pop(1);
}
