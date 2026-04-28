#include <math.h>
#include <stdlib.h>

#include "engineering_console.h"

#define ENG_INTERACT_RADIUS 35.0f
#define LOCAL_HALF_X 11.0f
#define LOCAL_HALF_Z 9.0f
#define SEAT_DISTANCE 18.0f

// How fast stick Y pumps energy (units per frame at full deflection). 100 / 60
// would refill an empty bar in ~1s; we go a touch slower.
#define ENERGY_PUMP_RATE 1.6f

// Stick X threshold for selecting a different slot. Edge-detected.
#define STICK_NAV_THRESHOLD 0.55f

static EngineeringConsole console_instance = {0};

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

EngineeringConsole* engineering_console_create(float x, float z, float facing_yaw)
{
    EngineeringConsole *e = &console_instance;
    e->position = (T3DVec3){{x, 0.0f, z}};
    e->rot_y = facing_yaw;
    e->occupant_pid = -1;
    e->player_in_range_any = false;
    {
        float ca = fabsf(cosf(facing_yaw));
        float sa = fabsf(sinf(facing_yaw));
        e->half_x = ca * LOCAL_HALF_X + sa * LOCAL_HALF_Z;
        e->half_z = sa * LOCAL_HALF_X + ca * LOCAL_HALF_Z;
    }
    e->selected = ENG_ENGINES;
    e->stick_x_prev = 0.0f;
    e->repair_buff_frames = 0;
    e->repair_cooldown = 0;
    for (int i = 0; i < 4; i++) { e->prev_a[i] = false; e->prev_b[i] = false; }
    // Default to an even split so the systems read as balanced at start.
    e->energy[ENG_ENGINES] = 33.0f;
    e->energy[ENG_WEAPONS] = 33.0f;
    e->energy[ENG_SHIELDS] = 34.0f;

    float fx =  sinf(facing_yaw);
    float fz = -cosf(facing_yaw);
    e->seat_x = x + fx * SEAT_DISTANCE;
    e->seat_z = z + fz * SEAT_DISTANCE;
    e->seat_yaw = facing_yaw + 3.1415927f;

    e->texture = sprite_load("rom:/engineering_panel.sprite");
    e->verts   = malloc_uncached(sizeof(T3DVertPacked) * WEAPONS_PANEL_NUM_TRIS * 2);
    e->matrix  = malloc_uncached(sizeof(T3DMat4FP));
    build_verts(e->verts);

    t3d_mat4fp_from_srt_euler(e->matrix,
        (float[3]){1.0f, 1.0f, 1.0f},
        (float[3]){0.0f, facing_yaw, 0.0f},
        (float[3]){x, 0.0f, z}
    );
    return e;
}

static bool in_radius(float dx, float dz)
{
    return (dx * dx + dz * dz) <= (ENG_INTERACT_RADIUS * ENG_INTERACT_RADIUS);
}

bool engineering_console_try_engage(EngineeringConsole *e, int pid,
                                    float player_x, float player_z,
                                    joypad_inputs_t inputs)
{
    if (pid < 0 || pid >= 4) return false;
    if (e->occupant_pid >= 0 && e->occupant_pid != pid) return false;

    bool a_now = inputs.btn.a != 0;
    bool a_pressed = a_now && !e->prev_a[pid];
    e->prev_a[pid] = a_now;

    if (e->occupant_pid == pid) return true;

    float dx = player_x - e->position.v[0];
    float dz = player_z - e->position.v[2];
    if (in_radius(dx, dz) && a_pressed) {
        e->occupant_pid = pid;
        e->stick_x_prev = 0.0f;
        return true;
    }
    return false;
}

// Re-distribute energy so the bars sum to 100 after one was changed by `delta`.
// The active slot absorbs the input directly; the other two share the
// opposite-signed delta in proportion to their current values (so we don't
// pull a bar that's already at 0 below zero).
static void rebalance_energy(EngineeringConsole *e, int slot, float delta)
{
    float new_val = e->energy[slot] + delta;
    if (new_val < 0.0f)   new_val = 0.0f;
    if (new_val > 100.0f) new_val = 100.0f;
    float real_delta = new_val - e->energy[slot];
    e->energy[slot] = new_val;
    if (real_delta == 0.0f) return;

    int other_a = (slot + 1) % ENG_NUM_SYSTEMS;
    int other_b = (slot + 2) % ENG_NUM_SYSTEMS;
    float total_other = e->energy[other_a] + e->energy[other_b];
    if (total_other <= 0.0001f) {
        // Edge case: both other bars empty. Spread the change equally.
        e->energy[other_a] -= real_delta * 0.5f;
        e->energy[other_b] -= real_delta * 0.5f;
    } else {
        float share_a = e->energy[other_a] / total_other;
        e->energy[other_a] -= real_delta * share_a;
        e->energy[other_b] -= real_delta * (1.0f - share_a);
    }
    // Clamp & re-normalise so the sum doesn't drift due to FP error.
    for (int i = 0; i < ENG_NUM_SYSTEMS; i++) {
        if (e->energy[i] < 0.0f)   e->energy[i] = 0.0f;
        if (e->energy[i] > 100.0f) e->energy[i] = 100.0f;
    }
    float sum = e->energy[0] + e->energy[1] + e->energy[2];
    if (sum > 0.0001f) {
        float scale = 100.0f / sum;
        for (int i = 0; i < ENG_NUM_SYSTEMS; i++) e->energy[i] *= scale;
    }
}

bool engineering_console_drive(EngineeringConsole *e, int pid, joypad_inputs_t inputs)
{
    if (pid < 0 || pid >= 4 || e->occupant_pid != pid) {
        if (e->repair_buff_frames > 0) e->repair_buff_frames--;
        if (e->repair_cooldown > 0)    e->repair_cooldown--;
        return false;
    }

    bool a_now = inputs.btn.a != 0;
    bool b_now = inputs.btn.b != 0;
    bool a_pressed = a_now && !e->prev_a[pid];
    bool b_pressed = b_now && !e->prev_b[pid];
    e->prev_a[pid] = a_now;
    e->prev_b[pid] = b_now;

    if (b_pressed) {
        e->occupant_pid = -1;
        return false;
    }

    float sx = (float)inputs.stick_x / STICK_DIVISOR;
    float sy = (float)inputs.stick_y / STICK_DIVISOR;
    if (sx < -1.0f) sx = -1.0f;
    if (sx >  1.0f) sx =  1.0f;
    if (sy < -1.0f) sy = -1.0f;
    if (sy >  1.0f) sy =  1.0f;

    // Edge-detected slot navigation: stick moves left/right past a threshold
    // selects the next/previous slot; release back below threshold to re-arm.
    if (e->stick_x_prev <= STICK_NAV_THRESHOLD && sx > STICK_NAV_THRESHOLD) {
        e->selected = (e->selected + 1) % ENG_NUM_SYSTEMS;
    } else if (e->stick_x_prev >= -STICK_NAV_THRESHOLD && sx < -STICK_NAV_THRESHOLD) {
        e->selected = (e->selected + ENG_NUM_SYSTEMS - 1) % ENG_NUM_SYSTEMS;
    }
    e->stick_x_prev = sx;

    // Stick Y pumps power into the selected slot.
    if (fabsf(sy) > 0.1f) {
        rebalance_energy(e, e->selected, sy * ENERGY_PUMP_RATE);
    }

    // A press fires a repair pulse if cooldown allows.
    if (e->repair_buff_frames > 0) e->repair_buff_frames--;
    if (e->repair_cooldown > 0)    e->repair_cooldown--;
    if (a_pressed && e->repair_cooldown == 0) {
        e->repair_buff_frames = ENG_REPAIR_BUFF_FRAMES;
        e->repair_cooldown    = ENG_REPAIR_COOLDOWN;
    }

    return true;
}

void engineering_console_update_proximity(EngineeringConsole *e,
                                          const float (*positions)[2],
                                          const bool *present, int max_pids)
{
    e->player_in_range_any = false;
    for (int i = 0; i < max_pids; i++) {
        if (!present[i]) continue;
        if (i == e->occupant_pid) continue;
        float dx = positions[i][0] - e->position.v[0];
        float dz = positions[i][1] - e->position.v[2];
        if (in_radius(dx, dz)) { e->player_in_range_any = true; break; }
    }
}

bool engineering_console_blocks(const EngineeringConsole *e, float wx, float wz)
{
    float dx = wx - e->position.v[0];
    float dz = wz - e->position.v[2];
    if (dx < 0) dx = -dx;
    if (dz < 0) dz = -dz;
    return dx <= e->half_x && dz <= e->half_z;
}

bool engineering_console_repair_active(const EngineeringConsole *e)
{
    return e->repair_buff_frames > 0;
}

void engineering_console_draw(EngineeringConsole *e)
{
    rdpq_sync_pipe();
    rdpq_sync_tile();
    rdpq_sprite_upload(TILE0, e->texture, NULL);

    rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
    rdpq_mode_filter(FILTER_BILINEAR);
    t3d_state_set_drawflags(T3D_FLAG_TEXTURED | T3D_FLAG_SHADED | T3D_FLAG_DEPTH);

    t3d_matrix_push(e->matrix);
    const int TRIS_PER_LOAD = 6;
    for (int tri = 0; tri < WEAPONS_PANEL_NUM_TRIS; tri += TRIS_PER_LOAD) {
        int chunk = WEAPONS_PANEL_NUM_TRIS - tri;
        if (chunk > TRIS_PER_LOAD) chunk = TRIS_PER_LOAD;
        t3d_vert_load(e->verts + tri * 2, 0, chunk * 4);
        for (int i = 0; i < chunk; i++) {
            int base = i * 4;
            t3d_tri_draw(base + 0, base + 1, base + 2);
        }
        t3d_tri_sync();
    }
    t3d_matrix_pop(1);
}
