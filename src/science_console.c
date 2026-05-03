#include <math.h>
#include <stdlib.h>

#include "science_console.h"

#define SCI_INTERACT_RADIUS 35.0f
#define LOCAL_HALF_X 11.0f
#define LOCAL_HALF_Z 9.0f
#define SEAT_DISTANCE 18.0f

#define FEEDBACK_FLASH_FRAMES 18

static ScienceConsole console_instance = {0};

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

ScienceConsole* science_console_create(float x, float z, float facing_yaw)
{
    ScienceConsole *s = &console_instance;
    s->position = (T3DVec3){{x, 0.0f, z}};
    s->rot_y = facing_yaw;
    s->occupant_pid = -1;
    s->player_in_range_any = false;
    {
        float ca = fabsf(cosf(facing_yaw));
        float sa = fabsf(sinf(facing_yaw));
        s->half_x = ca * LOCAL_HALF_X + sa * LOCAL_HALF_Z;
        s->half_z = sa * LOCAL_HALF_X + ca * LOCAL_HALF_Z;
    }
    s->selected_face = 0;       // BOW by default
    s->prev_stick_x  = 0.0f;
    s->pending_hit   = 0;
    s->pending_miss  = 0;
    s->spawn_timer   = 0;
    s->feedback_timer = 0;
    for (int i = 0; i < SCI_TRACK_LEN; i++) s->notes[i] = -1.0f;
    for (int i = 0; i < 4; i++) { s->prev_a[i] = false; s->prev_b[i] = false; }

    float fx =  sinf(facing_yaw);
    float fz = -cosf(facing_yaw);
    s->seat_x = x + fx * SEAT_DISTANCE;
    s->seat_z = z + fz * SEAT_DISTANCE;
    s->seat_yaw = facing_yaw + 3.1415927f;

    s->texture = sprite_load("rom:/science_panel.sprite");
    s->verts   = malloc_uncached(sizeof(T3DVertPacked) * WEAPONS_PANEL_NUM_TRIS * 2);
    s->matrix  = malloc_uncached(sizeof(T3DMat4FP));
    build_verts(s->verts);

    t3d_mat4fp_from_srt_euler(s->matrix,
        (float[3]){1.0f, 1.0f, 1.0f},
        (float[3]){0.0f, facing_yaw, 0.0f},
        (float[3]){x, 0.0f, z}
    );
    return s;
}

static bool in_radius(float dx, float dz)
{
    return (dx * dx + dz * dz) <= (SCI_INTERACT_RADIUS * SCI_INTERACT_RADIUS);
}

bool science_console_try_engage(ScienceConsole *s, int pid,
                                float player_x, float player_z,
                                joypad_inputs_t inputs)
{
    if (pid < 0 || pid >= 4) return false;
    if (s->occupant_pid >= 0 && s->occupant_pid != pid) return false;

    bool a_now = inputs.btn.a != 0;
    bool a_pressed = a_now && !s->prev_a[pid];
    s->prev_a[pid] = a_now;

    if (s->occupant_pid == pid) return true;

    float dx = player_x - s->position.v[0];
    float dz = player_z - s->position.v[2];
    if (in_radius(dx, dz) && a_pressed) {
        s->occupant_pid = pid;
        return true;
    }
    return false;
}

// Find an empty note slot and seed it at progress 0.
static void spawn_note(ScienceConsole *s)
{
    for (int i = 0; i < SCI_TRACK_LEN; i++) {
        if (s->notes[i] < 0.0f) { s->notes[i] = 0.0f; return; }
    }
}

// Find the active note closest to the hit line (highest progress) inside the
// hit window. Returns -1 if none.
static int find_hittable_note(const ScienceConsole *s)
{
    int best = -1;
    float best_progress = -1.0f;
    for (int i = 0; i < SCI_TRACK_LEN; i++) {
        float p = s->notes[i];
        if (p < 0.0f) continue;
        float dist = fabsf(p - 1.0f);
        if (dist <= SCI_HIT_WINDOW && p > best_progress) {
            best = i;
            best_progress = p;
        }
    }
    return best;
}

bool science_console_drive(ScienceConsole *s, int pid, joypad_inputs_t inputs)
{
    if (pid < 0 || pid >= 4 || s->occupant_pid != pid) {
        // Even when unattended, decay the feedback flash so it doesn't
        // linger if the player walks away mid-note.
        if (s->feedback_timer > 0) s->feedback_timer--;
        if (s->feedback_timer < 0) s->feedback_timer++;
        return false;
    }

    bool a_now = inputs.btn.a != 0;
    bool b_now = inputs.btn.b != 0;
    bool a_pressed = a_now && !s->prev_a[pid];
    bool b_pressed = b_now && !s->prev_b[pid];
    s->prev_a[pid] = a_now;
    s->prev_b[pid] = b_now;

    if (b_pressed) {
        s->occupant_pid = -1;
        return false;
    }

    // Phase-5 face cycling: stick-X flick steps the selected_face. Edge-
    // detected via prev_stick_x so a held stick = single step. SHIELD_FACE_COUNT
    // is 6, ordered BOW / STERN / PORT / STARBOARD / DORSAL / VENTRAL.
    float sx = (float)inputs.stick_x;
    if (sx > SCI_STICK_FLICK && s->prev_stick_x <= SCI_STICK_FLICK) {
        s->selected_face = (s->selected_face + 1) % 6;
    } else if (sx < -SCI_STICK_FLICK && s->prev_stick_x >= -SCI_STICK_FLICK) {
        s->selected_face = (s->selected_face + 5) % 6;   // -1 mod 6
    }
    s->prev_stick_x = sx;

    // Spawn cadence.
    s->spawn_timer++;
    if (s->spawn_timer >= SCI_NOTE_INTERVAL) {
        s->spawn_timer = 0;
        spawn_note(s);
    }

    // Advance every active note. Notes that pass the hit window without an
    // A press are counted as misses on the same frame they expire — queued
    // for main.c to forward to ship_view_shield_add(selected_face, -PEN).
    const float step = 1.0f / (float)SCI_NOTE_TRAVEL;
    for (int i = 0; i < SCI_TRACK_LEN; i++) {
        if (s->notes[i] < 0.0f) continue;
        s->notes[i] += step;
        if (s->notes[i] > 1.0f + SCI_HIT_WINDOW) {
            s->notes[i] = -1.0f;
            s->pending_miss++;
            s->feedback_timer = -FEEDBACK_FLASH_FRAMES;
        }
    }

    // A press: hit the closest note in the hit window if any. Queue a hit
    // event for main.c to forward; an A press with no note in window is a
    // soft miss so spamming A doesn't trivialise the minigame.
    if (a_pressed) {
        int idx = find_hittable_note(s);
        if (idx >= 0) {
            s->notes[idx] = -1.0f;
            s->pending_hit++;
            s->feedback_timer = FEEDBACK_FLASH_FRAMES;
        } else {
            s->pending_miss++;
            s->feedback_timer = -FEEDBACK_FLASH_FRAMES;
        }
    }

    if (s->feedback_timer > 0) s->feedback_timer--;
    if (s->feedback_timer < 0) s->feedback_timer++;
    return true;
}

void science_console_update_proximity(ScienceConsole *s,
                                      const float (*positions)[2],
                                      const bool *present, int max_pids)
{
    s->player_in_range_any = false;
    for (int i = 0; i < max_pids; i++) {
        if (!present[i]) continue;
        if (i == s->occupant_pid) continue;
        float dx = positions[i][0] - s->position.v[0];
        float dz = positions[i][1] - s->position.v[2];
        if (in_radius(dx, dz)) { s->player_in_range_any = true; break; }
    }
}

bool science_console_blocks(const ScienceConsole *s, float wx, float wz)
{
    float dx = wx - s->position.v[0];
    float dz = wz - s->position.v[2];
    if (dx < 0) dx = -dx;
    if (dz < 0) dz = -dz;
    return dx <= s->half_x && dz <= s->half_z;
}

int science_console_consume_hit(ScienceConsole *s)
{
    int n = s->pending_hit;
    s->pending_hit = 0;
    return n;
}

int science_console_consume_miss(ScienceConsole *s)
{
    int n = s->pending_miss;
    s->pending_miss = 0;
    return n;
}

void science_console_draw(ScienceConsole *s)
{
    rdpq_sync_pipe();
    rdpq_sync_tile();
    rdpq_sprite_upload(TILE0, s->texture, NULL);

    rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
    rdpq_mode_filter(FILTER_BILINEAR);
    t3d_state_set_drawflags(T3D_FLAG_TEXTURED | T3D_FLAG_SHADED | T3D_FLAG_DEPTH);

    t3d_matrix_push(s->matrix);
    const int TRIS_PER_LOAD = 6;
    for (int tri = 0; tri < WEAPONS_PANEL_NUM_TRIS; tri += TRIS_PER_LOAD) {
        int chunk = WEAPONS_PANEL_NUM_TRIS - tri;
        if (chunk > TRIS_PER_LOAD) chunk = TRIS_PER_LOAD;
        t3d_vert_load(s->verts + tri * 2, 0, chunk * 4);
        for (int i = 0; i < chunk; i++) {
            int base = i * 4;
            t3d_tri_draw(base + 0, base + 1, base + 2);
        }
        t3d_tri_sync();
    }
    t3d_matrix_pop(1);
}
