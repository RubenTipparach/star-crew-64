#ifndef BRIDGE_PANEL_H
#define BRIDGE_PANEL_H

#include "game_config.h"
#include "bridge_panel_model.h"

// A static interactive console placed somewhere on the bridge. The player
// walks up to it; while in proximity, the stick steers the ship and a button
// applies impulse. The corner viewport reads `bridge_panel_get_steer()` /
// `bridge_panel_get_impulse()` to drive the ship's motion when active.
typedef struct {
    T3DVertPacked *verts;        // BRIDGE_PANEL_NUM_TRIS * 2 packed structs (3 verts per tri, 2 verts per packed struct ⇒ ceil(3/2) = 2 per tri)
    T3DMat4FP     *matrix;       // single static placement matrix
    sprite_t      *texture;
    T3DVec3        position;     // world-space placement
    float          rot_y;        // facing yaw (radians)
    bool           player_in_range;
    bool           player_active; // true while the player is actively steering from this panel

    // Steering state — produced by panel input, consumed by the ship view.
    // steer is in [-1, +1] (left = -1, right = +1).
    // impulse is in [-1, +1] (back = -1, forward = +1).
    float          steer;
    float          impulse;

    // Edge-detect: A enters the helm (when in range, not active);
    // B leaves the helm (when active).
    bool           prev_a;
    bool           prev_b;
} BridgePanel;

// Build geometry, load texture, place the panel at (x, z) facing +Z by default.
BridgePanel* bridge_panel_create(float x, float z, float facing_yaw);

// Update interaction state given the player's current world position and pad
// inputs. Call once per frame. Returns true if the panel "captured" the
// player's input this frame (i.e. they're driving the ship; main.c should
// skip its own movement code).
bool bridge_panel_update(BridgePanel *p,
                         float player_x, float player_z,
                         joypad_inputs_t inputs);

void bridge_panel_draw(BridgePanel *p);

#endif // BRIDGE_PANEL_H
