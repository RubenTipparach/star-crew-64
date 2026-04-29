#ifndef BRIDGE_PANEL_H
#define BRIDGE_PANEL_H

#include "game_config.h"
#include "bridge_panel_model.h"

// Helm console. Any player (1..4) can walk up and press A to take the seat;
// while seated, the stick steers the ship and applies impulse. Only one
// player can occupy the console at a time — `occupant_pid` is the 0-based
// pad index of the active player, or -1 if free.
typedef struct {
    T3DVertPacked *verts;        // BRIDGE_PANEL_NUM_TRIS * 2 packed structs
    T3DMat4FP     *matrix;       // single static placement matrix
    sprite_t      *texture;
    T3DVec3        position;     // world-space placement
    float          rot_y;        // facing yaw (radians)

    // World-space "seat" the player snaps to when they engage. Faces the
    // panel so the character is reading the controls. Set in _create() based
    // on the panel's facing direction.
    float          seat_x;
    float          seat_z;
    float          seat_yaw;

    // AABB used for collision so players can't walk through the console.
    // Half-extents from `position` along world X/Z. Y collision isn't needed
    // (characters live at y=0 and the panel is short).
    float          half_x;
    float          half_z;

    int            occupant_pid;  // 0..3 if occupied; -1 if free.
    bool           player_in_range_any; // true if at least one non-occupant is close

    // Steering state — produced by panel input, consumed by the ship view.
    // steer is in [-1, +1] (left = -1, right = +1).
    // impulse is in [-1, +1] (back = -1, forward = +1).
    float          steer;
    float          impulse;

    // Per-pad edge-detect state. Indexed by pad number 0..3.
    bool           prev_a[4];
    bool           prev_b[4];
} BridgePanel;

BridgePanel* bridge_panel_create(float x, float z, float facing_yaw);

// Try to engage the panel for player `pid` (0..3). Returns true if `pid`
// successfully took (or already holds) the seat after this call. The caller
// passes the player's world position so we can range-check.
bool bridge_panel_try_engage(BridgePanel *p, int pid,
                             float player_x, float player_z,
                             joypad_inputs_t inputs);

// Run the active player's input through the panel (steering, impulse, B-to-
// leave). Should be called every frame for the player who currently holds
// the seat. No-op + returns false if pid != occupant_pid.
bool bridge_panel_drive(BridgePanel *p, int pid, joypad_inputs_t inputs);

// Update "is anyone in range?" flag for prompt UI; doesn't change ownership.
// `positions` is an array of {x, z} for each connected player. `present[i]`
// indicates which slots are populated.
void bridge_panel_update_proximity(BridgePanel *p,
                                   const float (*positions)[2],
                                   const bool *present, int max_pids);

// Returns true if the world-space point (wx, wz) lies inside the panel's
// AABB (used to block character movement).
bool bridge_panel_blocks(const BridgePanel *p, float wx, float wz);

void bridge_panel_draw(BridgePanel *p);

#endif // BRIDGE_PANEL_H
