#include <math.h>
#include <stdlib.h>

#include "bridge_panel.h"

// Distance (world units) at which the player can activate the panel.
#define PANEL_INTERACT_RADIUS 22.0f

// Smoothing for steering / impulse — input lerps toward target so the ship
// doesn't snap.
#define STEER_LERP   0.25f
#define IMPULSE_LERP 0.10f

static BridgePanel panel_instance = {0};

// Build flat-shaded T3DVertPacked records straight from the baked
// BRIDGE_PANEL_POS / BRIDGE_PANEL_UV / BRIDGE_PANEL_TRI_NORMAL tables. Two
// packed structs per triangle: pack_a holds verts 0+1, pack_b holds vert 2
// padded with a copy of vert 0 (we only ever issue (0,1,2) so the duplicate
// is never referenced).
static void build_verts(T3DVertPacked *out)
{
    uint32_t white = 0xFFFFFFFF;
    for (int t = 0; t < BRIDGE_PANEL_NUM_TRIS; t++) {
        const float *n = BRIDGE_PANEL_TRI_NORMAL[t];
        T3DVec3 nv = {{n[0], n[1], n[2]}};
        uint16_t pn = t3d_vert_pack_normal(&nv);

        int v0 = t * 3 + 0;
        int v1 = t * 3 + 1;
        int v2 = t * 3 + 2;

        int16_t u0 = UV(BRIDGE_PANEL_UV[v0][0] * 32.0f);
        int16_t v0t = UV(BRIDGE_PANEL_UV[v0][1] * 32.0f);
        int16_t u1 = UV(BRIDGE_PANEL_UV[v1][0] * 32.0f);
        int16_t v1t = UV(BRIDGE_PANEL_UV[v1][1] * 32.0f);
        int16_t u2 = UV(BRIDGE_PANEL_UV[v2][0] * 32.0f);
        int16_t v2t = UV(BRIDGE_PANEL_UV[v2][1] * 32.0f);

        out[t * 2 + 0] = (T3DVertPacked){
            .posA = {BRIDGE_PANEL_POS[v0][0], BRIDGE_PANEL_POS[v0][1], BRIDGE_PANEL_POS[v0][2]},
            .rgbaA = white, .normA = pn, .stA = {u0, v0t},
            .posB = {BRIDGE_PANEL_POS[v1][0], BRIDGE_PANEL_POS[v1][1], BRIDGE_PANEL_POS[v1][2]},
            .rgbaB = white, .normB = pn, .stB = {u1, v1t},
        };
        out[t * 2 + 1] = (T3DVertPacked){
            .posA = {BRIDGE_PANEL_POS[v2][0], BRIDGE_PANEL_POS[v2][1], BRIDGE_PANEL_POS[v2][2]},
            .rgbaA = white, .normA = pn, .stA = {u2, v2t},
            // pad — never referenced, but must be valid mem
            .posB = {BRIDGE_PANEL_POS[v0][0], BRIDGE_PANEL_POS[v0][1], BRIDGE_PANEL_POS[v0][2]},
            .rgbaB = white, .normB = pn, .stB = {u0, v0t},
        };
    }
}

BridgePanel* bridge_panel_create(float x, float z, float facing_yaw)
{
    BridgePanel *p = &panel_instance;
    p->position = (T3DVec3){{x, 0.0f, z}};
    p->rot_y = facing_yaw;
    p->player_in_range = false;
    p->player_active   = false;
    p->steer   = 0.0f;
    p->impulse = 0.0f;
    p->prev_a  = false;

    p->texture = sprite_load("rom:/bridge_panel.sprite");
    p->verts   = malloc_uncached(sizeof(T3DVertPacked) * BRIDGE_PANEL_NUM_TRIS * 2);
    p->matrix  = malloc_uncached(sizeof(T3DMat4FP));

    build_verts(p->verts);

    t3d_mat4fp_from_srt_euler(p->matrix,
        (float[3]){1.0f, 1.0f, 1.0f},
        (float[3]){0.0f, facing_yaw, 0.0f},
        (float[3]){x, 0.0f, z}
    );

    return p;
}

bool bridge_panel_update(BridgePanel *p, float player_x, float player_z,
                         joypad_inputs_t inputs)
{
    float dx = player_x - p->position.v[0];
    float dz = player_z - p->position.v[2];
    float d2 = dx * dx + dz * dz;
    p->player_in_range = (d2 <= PANEL_INTERACT_RADIUS * PANEL_INTERACT_RADIUS);

    // Edge-detect A: toggle "active" while in range; force-exit if the player
    // walks out of range.
    bool a_now = inputs.btn.a != 0;
    bool a_pressed = a_now && !p->prev_a;
    p->prev_a = a_now;

    if (p->player_in_range && a_pressed) {
        p->player_active = !p->player_active;
    }
    if (!p->player_in_range) {
        p->player_active = false;
    }

    if (p->player_active) {
        // Steer with stick X, impulse held while B is down.
        float target_steer   = (float)inputs.stick_x / STICK_DIVISOR;
        if (target_steer < -1.0f) target_steer = -1.0f;
        if (target_steer >  1.0f) target_steer =  1.0f;
        float target_impulse = inputs.btn.b ? 1.0f : 0.0f;
        p->steer   += (target_steer   - p->steer)   * STEER_LERP;
        p->impulse += (target_impulse - p->impulse) * IMPULSE_LERP;
    } else {
        // Decay back to neutral when nobody's at the helm.
        p->steer   += (0.0f - p->steer)   * STEER_LERP;
        p->impulse += (0.0f - p->impulse) * IMPULSE_LERP * 0.5f;
    }

    return p->player_active;
}

void bridge_panel_draw(BridgePanel *p)
{
    rdpq_sync_pipe();
    rdpq_sync_tile();
    rdpq_sprite_upload(TILE0, p->texture, NULL);

    rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
    rdpq_mode_filter(FILTER_BILINEAR);
    t3d_state_set_drawflags(T3D_FLAG_TEXTURED | T3D_FLAG_SHADED | T3D_FLAG_DEPTH);

    t3d_matrix_push(p->matrix);
    // Chunk loads to stay under tiny3d's internal vertex-buffer cap. Each
    // tri = 2 packed structs = 4 individual verts (slot 3 is a padding
    // duplicate of slot 0 and is never indexed).
    const int TRIS_PER_LOAD = 6;
    for (int tri = 0; tri < BRIDGE_PANEL_NUM_TRIS; tri += TRIS_PER_LOAD) {
        int chunk = BRIDGE_PANEL_NUM_TRIS - tri;
        if (chunk > TRIS_PER_LOAD) chunk = TRIS_PER_LOAD;
        t3d_vert_load(p->verts + tri * 2, 0, chunk * 4);
        for (int i = 0; i < chunk; i++) {
            int base = i * 4;
            t3d_tri_draw(base + 0, base + 1, base + 2);
        }
        t3d_tri_sync();
    }
    t3d_matrix_pop(1);
}
