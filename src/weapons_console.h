#ifndef WEAPONS_CONSOLE_H
#define WEAPONS_CONSOLE_H

#include "game_config.h"
#include "weapons_panel_model.h"

// Gunner station. Any player walks up, presses A to take the seat. While
// active, the stick aims a yaw offset off the ship's forward, A fires
// phasers, Z fires a heavier photon torpedo, and B leaves the station.
// `occupant_pid` is the 0-based pad index that holds the seat (-1 if free).
typedef struct {
    T3DVertPacked *verts;        // WEAPONS_PANEL_NUM_TRIS * 2 packed structs
    T3DMat4FP     *matrix;
    sprite_t      *texture;
    T3DVec3        position;
    float          rot_y;

    float          seat_x;
    float          seat_z;
    float          seat_yaw;
    float          half_x;
    float          half_z;

    int            occupant_pid;     // 0..3 active, -1 free
    bool           player_in_range_any;

    // Per-pad edge-detect state.
    bool           prev_a[4];
    bool           prev_b[4];
    bool           prev_z[4];

    // Aim, in radians, relative to the ship's forward direction. Stick X
    // drives this while active; clamped to a sensible cone so the player can
    // still always see roughly where their shots will go.
    float          aim_yaw;

    // Fire-request flags (consumed by main.c / ship_view) plus per-weapon
    // cooldowns so the player can't spam.
    bool           fire_phaser_requested;
    bool           fire_torpedo_requested;
    int            phaser_cooldown;
    int            torpedo_cooldown;
} WeaponsConsole;

WeaponsConsole* weapons_console_create(float x, float z, float facing_yaw);

bool weapons_console_try_engage(WeaponsConsole *w, int pid,
                                float player_x, float player_z,
                                joypad_inputs_t inputs);

// Drive the active player's controls (aim, fire, leave). Returns true if the
// player still holds the seat after this frame.
bool weapons_console_drive(WeaponsConsole *w, int pid, joypad_inputs_t inputs);

void weapons_console_update_proximity(WeaponsConsole *w,
                                      const float (*positions)[2],
                                      const bool *present, int max_pids);

bool weapons_console_blocks(const WeaponsConsole *w, float wx, float wz);

bool weapons_console_consume_phaser(WeaponsConsole *w);
bool weapons_console_consume_torpedo(WeaponsConsole *w);

void weapons_console_draw(WeaponsConsole *w);

#endif // WEAPONS_CONSOLE_H
