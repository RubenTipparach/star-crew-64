#include <math.h>
#include <stdlib.h>

#include "weapons_console.h"

// Distance (world units) at which the player can interact with the console.
// Matches the bridge panel's radius so both stations feel equally forgiving.
#define WEAPONS_INTERACT_RADIUS 35.0f

// Cooldowns per weapon (frames @ ~60Hz). Phasers are the rapid primary;
// torpedoes are heavy and slow.
#define PHASER_COOLDOWN_FRAMES   12     // ~5 shots/sec
#define TORPEDO_COOLDOWN_FRAMES  45     // ~1.3 shots/sec

// Aim adjustment rate while at the gunner seat. Stick fully over rotates the
// aim by AIM_RATE radians per frame; clamped to +/- AIM_LIMIT off the ship's
// forward so the gunner can't shoot directly behind.
#define AIM_RATE   0.045f
#define AIM_LIMIT  1.0472f      // 60° each way

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
    w->player_active   = false;
    w->prev_a = false;
    w->prev_b = false;
    w->prev_z = false;
    w->aim_yaw = 0.0f;
    w->fire_phaser_requested  = false;
    w->fire_torpedo_requested = false;
    w->phaser_cooldown  = 0;
    w->torpedo_cooldown = 0;

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

bool weapons_console_update(WeaponsConsole *w, float player_x, float player_z,
                            joypad_inputs_t inputs)
{
    float dx = player_x - w->position.v[0];
    float dz = player_z - w->position.v[2];
    float d2 = dx * dx + dz * dz;
    w->player_in_range = (d2 <= WEAPONS_INTERACT_RADIUS * WEAPONS_INTERACT_RADIUS);

    bool a_now = inputs.btn.a != 0;
    bool b_now = inputs.btn.b != 0;
    bool z_now = inputs.btn.z != 0;
    bool a_pressed = a_now && !w->prev_a;
    bool b_pressed = b_now && !w->prev_b;
    bool z_pressed = z_now && !w->prev_z;
    w->prev_a = a_now;
    w->prev_b = b_now;
    w->prev_z = z_now;

    // ENTER: A while in range and not active. Consume the press so the same
    // frame doesn't immediately fire a phaser through the active branch below.
    if (!w->player_active && w->player_in_range && a_pressed) {
        w->player_active = true;
        w->aim_yaw = 0.0f;
        w->phaser_cooldown  = 0;
        w->torpedo_cooldown = 0;
        a_pressed = false;
    }
    // LEAVE: B while active. Don't fall through to fire logic on the same
    // frame either.
    if (w->player_active && b_pressed) {
        w->player_active = false;
        return false;
    }
    // Force-exit if the player walks out of range.
    if (!w->player_in_range) {
        w->player_active = false;
    }

    if (w->player_active) {
        // Stick X drives the aim cone. Decay slowly back toward zero when
        // the stick is centered so the gunner doesn't have to fight drift.
        float sx = (float)inputs.stick_x / STICK_DIVISOR;
        if (sx < -1.0f) sx = -1.0f;
        if (sx >  1.0f) sx =  1.0f;
        w->aim_yaw += sx * AIM_RATE;
        if (fabsf(sx) < 0.05f) w->aim_yaw *= 0.97f;
        if (w->aim_yaw < -AIM_LIMIT) w->aim_yaw = -AIM_LIMIT;
        if (w->aim_yaw >  AIM_LIMIT) w->aim_yaw =  AIM_LIMIT;

        if (w->phaser_cooldown > 0)  w->phaser_cooldown--;
        if (w->torpedo_cooldown > 0) w->torpedo_cooldown--;

        if (a_pressed && w->phaser_cooldown == 0) {
            w->fire_phaser_requested = true;
            w->phaser_cooldown = PHASER_COOLDOWN_FRAMES;
        }
        if (z_pressed && w->torpedo_cooldown == 0) {
            w->fire_torpedo_requested = true;
            w->torpedo_cooldown = TORPEDO_COOLDOWN_FRAMES;
        }
    }

    return w->player_active;
}

bool weapons_console_consume_phaser(WeaponsConsole *w)
{
    bool fired = w->fire_phaser_requested;
    w->fire_phaser_requested = false;
    return fired;
}

bool weapons_console_consume_torpedo(WeaponsConsole *w)
{
    bool fired = w->fire_torpedo_requested;
    w->fire_torpedo_requested = false;
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
