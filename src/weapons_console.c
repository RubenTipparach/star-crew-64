#include <math.h>
#include <stdlib.h>

#include "weapons_console.h"

#define WEAPONS_INTERACT_RADIUS 35.0f

// Cooldowns per weapon (frames @ ~60Hz). Phasers are the rapid primary;
// torpedoes are heavy and slow.
#define PHASER_COOLDOWN_FRAMES   12
#define TORPEDO_COOLDOWN_FRAMES  45

// Aim adjustment rate while at the gunner seat.
#define AIM_RATE   0.045f
#define AIM_LIMIT  1.0472f      // 60° each way

// Mesh-local half-extents (WEAPONS_PANEL_POS body bounds ±10 X, ±8 Z plus a
// 1-unit pad). The barrel pokes out the front, but we only care about the
// body for collision since the barrel is above character chest height.
#define LOCAL_HALF_X 11.0f
#define LOCAL_HALF_Z 9.0f

#define SEAT_DISTANCE 18.0f

static WeaponsConsole console_instance = {0};

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
    w->occupant_pid = -1;
    w->player_in_range_any = false;
    w->aim_yaw = 0.0f;
    w->fire_phaser_requested  = false;
    w->fire_torpedo_requested = false;
    w->phaser_cooldown  = 0;
    w->torpedo_cooldown = 0;
    {
        float ca = fabsf(cosf(facing_yaw));
        float sa = fabsf(sinf(facing_yaw));
        w->half_x = ca * LOCAL_HALF_X + sa * LOCAL_HALF_Z;
        w->half_z = sa * LOCAL_HALF_X + ca * LOCAL_HALF_Z;
    }
    for (int i = 0; i < 4; i++) {
        w->prev_a[i] = false;
        w->prev_b[i] = false;
        w->prev_z[i] = false;
    }

    // Seat = panel position + front_world * SEAT_DISTANCE. Mesh front is on
    // local -Z, so front_world = (sin(yaw), 0, -cos(yaw)).
    float fx =  sinf(facing_yaw);
    float fz = -cosf(facing_yaw);
    w->seat_x = x + fx * SEAT_DISTANCE;
    w->seat_z = z + fz * SEAT_DISTANCE;
    w->seat_yaw = facing_yaw + 3.1415927f;

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

static bool in_radius(float dx, float dz)
{
    return (dx * dx + dz * dz) <= (WEAPONS_INTERACT_RADIUS * WEAPONS_INTERACT_RADIUS);
}

bool weapons_console_try_engage(WeaponsConsole *w, int pid,
                                float player_x, float player_z,
                                joypad_inputs_t inputs)
{
    if (pid < 0 || pid >= 4) return false;
    if (w->occupant_pid >= 0 && w->occupant_pid != pid) return false;

    bool a_now = inputs.btn.a != 0;
    bool a_pressed = a_now && !w->prev_a[pid];
    w->prev_a[pid] = a_now;

    if (w->occupant_pid == pid) return true;

    float dx = player_x - w->position.v[0];
    float dz = player_z - w->position.v[2];
    if (in_radius(dx, dz) && a_pressed) {
        w->occupant_pid = pid;
        w->aim_yaw = 0.0f;
        w->phaser_cooldown  = 0;
        w->torpedo_cooldown = 0;
        return true;
    }
    return false;
}

bool weapons_console_drive(WeaponsConsole *w, int pid, joypad_inputs_t inputs)
{
    if (pid < 0 || pid >= 4 || w->occupant_pid != pid) return false;

    bool a_now = inputs.btn.a != 0;
    bool b_now = inputs.btn.b != 0;
    bool z_now = inputs.btn.z != 0;
    bool a_pressed = a_now && !w->prev_a[pid];
    bool b_pressed = b_now && !w->prev_b[pid];
    bool z_pressed = z_now && !w->prev_z[pid];
    w->prev_a[pid] = a_now;
    w->prev_b[pid] = b_now;
    w->prev_z[pid] = z_now;

    if (b_pressed) {
        w->occupant_pid = -1;
        return false;
    }

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
    return true;
}

void weapons_console_update_proximity(WeaponsConsole *w,
                                      const float (*positions)[2],
                                      const bool *present, int max_pids)
{
    w->player_in_range_any = false;
    for (int i = 0; i < max_pids; i++) {
        if (!present[i]) continue;
        if (i == w->occupant_pid) continue;
        float dx = positions[i][0] - w->position.v[0];
        float dz = positions[i][1] - w->position.v[2];
        if (in_radius(dx, dz)) { w->player_in_range_any = true; break; }
    }
}

bool weapons_console_blocks(const WeaponsConsole *w, float wx, float wz)
{
    float dx = wx - w->position.v[0];
    float dz = wz - w->position.v[2];
    if (dx < 0) dx = -dx;
    if (dz < 0) dz = -dz;
    return dx <= w->half_x && dz <= w->half_z;
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
