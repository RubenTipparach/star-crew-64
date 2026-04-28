#ifndef SCIENCE_CONSOLE_H
#define SCIENCE_CONSOLE_H

#include "game_config.h"
#include "weapons_panel_model.h"

// Science console. The science officer manages the shield array. The full
// vision (reallocate shields by side) is out of scope for now; this
// implementation gives them a Guitar-Hero-style rhythm track where notes
// scroll toward a hit line and tapping A in time charges the shield bar.
//
// Shield charge is in [0, 100]. Hits add `SHIELD_HIT_BONUS`; missed notes
// (note crosses past the hit window without an A press) decay it slightly.
// The track uses a deterministic note schedule so we don't need RNG state.

#define SCI_TRACK_LEN     16            // notes per loop
#define SCI_NOTE_INTERVAL 36             // frames between note spawns (~0.6s @60fps)
#define SCI_NOTE_TRAVEL   90             // frames a note takes to scroll through

// Per-note position, in [0,1] where 1 == hit line. <0 means inactive.
#define SCI_HIT_WINDOW    0.10f          // how close to 1.0 counts as a hit

#define SHIELD_HIT_BONUS  6.0f
#define SHIELD_MISS_PEN   2.5f
#define SHIELD_MAX        100.0f

typedef struct {
    T3DVertPacked *verts;
    T3DMat4FP     *matrix;
    sprite_t      *texture;
    T3DVec3        position;
    float          rot_y;

    float          seat_x;
    float          seat_z;
    float          seat_yaw;
    float          half_x;
    float          half_z;

    int            occupant_pid;
    bool           player_in_range_any;
    bool           prev_a[4];
    bool           prev_b[4];

    // Shield charge accumulator.
    float          shield;

    // Rhythm track. Each active note has a `progress` value in [0,1]; -1
    // means "slot empty". Spawn cadence is driven by `spawn_timer` and the
    // per-frame increment is 1.0 / SCI_NOTE_TRAVEL.
    float          notes[SCI_TRACK_LEN];
    int            spawn_timer;

    // Most-recent hit feedback for the HUD: positive = HIT (frames left to
    // flash), negative = MISS (abs value = frames left).
    int            feedback_timer;
} ScienceConsole;

ScienceConsole* science_console_create(float x, float z, float facing_yaw);

bool science_console_try_engage(ScienceConsole *s, int pid,
                                float player_x, float player_z,
                                joypad_inputs_t inputs);
bool science_console_drive(ScienceConsole *s, int pid, joypad_inputs_t inputs);
void science_console_update_proximity(ScienceConsole *s,
                                      const float (*positions)[2],
                                      const bool *present, int max_pids);
bool science_console_blocks(const ScienceConsole *s, float wx, float wz);

void science_console_draw(ScienceConsole *s);

#endif // SCIENCE_CONSOLE_H
