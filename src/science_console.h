#ifndef SCIENCE_CONSOLE_H
#define SCIENCE_CONSOLE_H

#include "game_config.h"
#include "weapons_panel_model.h"

// Science console. Manages the 6-face shield array. The rhythm track is
// the input mechanic: notes scroll toward a hit line, A on a note adds
// HP to whichever face science currently has selected, a missed note
// drains it. Stick-X flicks step the selected face through the six
// shield faces (matches the gamedesign.md table). Per-face HP lives in
// ship_view; this console just signals hit/miss events that main.c
// forwards via ship_view_shield_add. Console-local `shield` from the
// pre-Phase-5 design is gone — it had nothing to track once the
// minigame stopped owning the shield value.

#define SCI_TRACK_LEN     16            // notes per loop
#define SCI_NOTE_INTERVAL 36             // frames between note spawns (~0.6s @60fps)
#define SCI_NOTE_TRAVEL   90             // frames a note takes to scroll through

// Per-note position, in [0,1] where 1 == hit line. <0 means inactive.
#define SCI_HIT_WINDOW    0.10f          // how close to 1.0 counts as a hit

// Hit/miss amounts forwarded to ship_view_shield_add for the selected
// face. Tuning matches the original single-shield gameplay so the
// rhythm minigame still feels meaningful.
#define SHIELD_HIT_BONUS  6
#define SHIELD_MISS_PEN   3

// Stick-X edge threshold for face-cycling — match the engineering
// console pattern where a flick = single step.
#define SCI_STICK_FLICK   60.0f

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

    // Phase-5: which shield face the next hit/miss applies to.
    // 0..SHIELD_FACE_COUNT-1, defaults to 0 (BOW). Cycled by stick-X.
    int            selected_face;
    float          prev_stick_x;

    // Pending hit/miss events. Set by drive() when a note crosses the hit
    // line; consumed by main.c, which forwards the signed amount to
    // ship_view_shield_add(face, ±). Mirrors the weapons_console_consume_*
    // pattern so the console stays free of ship_view dependencies.
    int            pending_hit;    // # hits queued this frame
    int            pending_miss;   // # misses queued this frame

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

// Drain the pending hit / miss counters for this frame. Returns the
// count and clears the field. main.c calls these and forwards the
// signed amount to ship_view_shield_add(s->selected_face, ...).
int  science_console_consume_hit(ScienceConsole *s);
int  science_console_consume_miss(ScienceConsole *s);

#endif // SCIENCE_CONSOLE_H
