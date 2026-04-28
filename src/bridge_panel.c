#include <math.h>
#include <stdlib.h>

#include "bridge_panel.h"

// Distance (world units) at which the player can activate the panel. Tuned
// to be generous enough that the player doesn't have to be stood pixel-perfect
// next to the console — a tile is 20 units wide and the character ~16 tall,
// so 35 means "anywhere within ~1.5 tiles of the console".
#define PANEL_INTERACT_RADIUS 35.0f

// Smoothing for steering / impulse — input lerps toward target so the ship
// doesn't snap.
#define STEER_LERP   0.25f
#define IMPULSE_LERP 0.10f

// Mesh-local half-extents (BRIDGE_PANEL_POS bounds: ±15 along X, ±8 along Z,
// +1 unit pad so the character doesn't visibly kiss the surface).
#define LOCAL_HALF_X 16.0f
#define LOCAL_HALF_Z 9.0f

// Distance from panel center to "seat" (where the operator stands). Pushes
// the player far enough back to not clip but close enough to look engaged.
#define SEAT_DISTANCE 18.0f

static BridgePanel panel_instance = {0};

// Same flat-shaded packing pattern used elsewhere — see weapons_console.c
// for full notes.
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
    // Compute the world-space AABB half-extents from the local OBB. For an
    // axis-aligned local box rotated by yaw, the bounding world AABB is:
    //   half_world.x = |cos|·half_local.x + |sin|·half_local.z
    //   half_world.z = |sin|·half_local.x + |cos|·half_local.z
    float ca = fabsf(cosf(facing_yaw));
    float sa = fabsf(sinf(facing_yaw));
    p->half_x = ca * LOCAL_HALF_X + sa * LOCAL_HALF_Z;
    p->half_z = sa * LOCAL_HALF_X + ca * LOCAL_HALF_Z;
    p->occupant_pid = -1;
    p->player_in_range_any = false;
    p->steer   = 0.0f;
    p->impulse = 0.0f;
    for (int i = 0; i < 4; i++) { p->prev_a[i] = false; p->prev_b[i] = false; }

    // Seat sits in front of the panel along its facing direction. The mesh
    // is authored with -Z as the front face; with t3d's Y-yaw convention the
    // world-space front direction is (sin(yaw), 0, -cos(yaw)).
    float fx =  sinf(facing_yaw);
    float fz = -cosf(facing_yaw);
    p->seat_x = x + fx * SEAT_DISTANCE;
    p->seat_z = z + fz * SEAT_DISTANCE;
    // Character rot_y of θ makes the model face (sin θ, 0, -cos θ); to face
    // back at the panel (i.e., along -front_world), set rot_y = facing + π.
    p->seat_yaw = facing_yaw + 3.1415927f;

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

static bool in_radius(float dx, float dz)
{
    return (dx * dx + dz * dz) <= (PANEL_INTERACT_RADIUS * PANEL_INTERACT_RADIUS);
}

bool bridge_panel_try_engage(BridgePanel *p, int pid,
                             float player_x, float player_z,
                             joypad_inputs_t inputs)
{
    if (pid < 0 || pid >= 4) return false;
    if (p->occupant_pid >= 0 && p->occupant_pid != pid) return false;

    bool a_now = inputs.btn.a != 0;
    bool a_pressed = a_now && !p->prev_a[pid];
    p->prev_a[pid] = a_now;

    if (p->occupant_pid == pid) return true;

    float dx = player_x - p->position.v[0];
    float dz = player_z - p->position.v[2];
    if (in_radius(dx, dz) && a_pressed) {
        p->occupant_pid = pid;
        return true;
    }
    return false;
}

bool bridge_panel_drive(BridgePanel *p, int pid, joypad_inputs_t inputs)
{
    if (pid < 0 || pid >= 4 || p->occupant_pid != pid) {
        // Decay back to neutral when nobody's at the helm.
        p->steer   += (0.0f - p->steer)   * STEER_LERP;
        p->impulse += (0.0f - p->impulse) * IMPULSE_LERP * 0.5f;
        return false;
    }

    bool b_now = inputs.btn.b != 0;
    bool b_pressed = b_now && !p->prev_b[pid];
    p->prev_b[pid] = b_now;
    // Keep A edge state up to date so re-engaging on the next tap works.
    p->prev_a[pid] = inputs.btn.a != 0;

    if (b_pressed) {
        p->occupant_pid = -1;
        return false;
    }

    float target_steer   = (float)inputs.stick_x / STICK_DIVISOR;
    float target_impulse = (float)inputs.stick_y / STICK_DIVISOR;
    if (target_steer < -1.0f)   target_steer   = -1.0f;
    if (target_steer >  1.0f)   target_steer   =  1.0f;
    if (target_impulse < -1.0f) target_impulse = -1.0f;
    if (target_impulse >  1.0f) target_impulse =  1.0f;
    p->steer   += (target_steer   - p->steer)   * STEER_LERP;
    p->impulse += (target_impulse - p->impulse) * IMPULSE_LERP;
    return true;
}

void bridge_panel_update_proximity(BridgePanel *p,
                                   const float (*positions)[2],
                                   const bool *present, int max_pids)
{
    p->player_in_range_any = false;
    for (int i = 0; i < max_pids; i++) {
        if (!present[i]) continue;
        if (i == p->occupant_pid) continue;
        float dx = positions[i][0] - p->position.v[0];
        float dz = positions[i][1] - p->position.v[2];
        if (in_radius(dx, dz)) { p->player_in_range_any = true; break; }
    }
}

bool bridge_panel_blocks(const BridgePanel *p, float wx, float wz)
{
    float dx = wx - p->position.v[0];
    float dz = wz - p->position.v[2];
    if (dx < 0) dx = -dx;
    if (dz < 0) dz = -dz;
    return dx <= p->half_x && dz <= p->half_z;
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
