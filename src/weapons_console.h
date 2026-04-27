#ifndef WEAPONS_CONSOLE_H
#define WEAPONS_CONSOLE_H

#include "game_config.h"
#include "weapons_panel_model.h"

// Gunner station. Player walks up, presses A to take the seat. While active,
// the stick aims a yaw offset off the ship's forward, A fires phasers, Z
// fires a heavier photon torpedo, and B leaves the station.
typedef struct {
    T3DVertPacked *verts;        // WEAPONS_PANEL_NUM_TRIS * 2 packed structs
    T3DMat4FP     *matrix;       // single static placement matrix
    sprite_t      *texture;
    T3DVec3        position;     // world-space placement
    float          rot_y;        // facing yaw

    bool           player_in_range;
    bool           player_active;     // toggled with A in / B out

    // Edge-detect state for the buttons we read while active.
    bool           prev_a;
    bool           prev_b;
    bool           prev_z;

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

// Update interaction state. Returns true if the player is currently active at
// the station (caller should suppress walking + character animation in that
// case, mirroring the bridge panel's pattern).
bool weapons_console_update(WeaponsConsole *w,
                            float player_x, float player_z,
                            joypad_inputs_t inputs);

// Returns true (and clears the flag) if a phaser shot was requested this
// frame. Same for torpedoes. Both should be polled every frame from main.c
// while the station is active.
bool weapons_console_consume_phaser(WeaponsConsole *w);
bool weapons_console_consume_torpedo(WeaponsConsole *w);

void weapons_console_draw(WeaponsConsole *w);

#endif // WEAPONS_CONSOLE_H
