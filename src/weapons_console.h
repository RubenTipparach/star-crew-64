#ifndef WEAPONS_CONSOLE_H
#define WEAPONS_CONSOLE_H

#include "game_config.h"
#include "weapons_panel_model.h"

// A static interactive gunner station placed on the bridge. While in
// range, B fires a projectile from the ship in the corner viewport (one
// shot per press, with a cooldown). A separate console from BridgePanel —
// they don't time-share input.
typedef struct {
    T3DVertPacked *verts;        // WEAPONS_PANEL_NUM_TRIS * 2 packed structs
    T3DMat4FP     *matrix;       // single static placement matrix
    sprite_t      *texture;
    T3DVec3        position;     // world-space placement
    float          rot_y;        // facing yaw

    bool           player_in_range;

    // Edge-detect for B (the trigger). When `fire_requested` is true on a
    // given frame, the ship_view should spawn a projectile and reset it.
    bool           prev_b;
    bool           fire_requested;
    int            cooldown;     // frames remaining until next shot allowed
} WeaponsConsole;

WeaponsConsole* weapons_console_create(float x, float z, float facing_yaw);

// Update interaction state. Sets `fire_requested = true` on the same frame
// the player presses B while in range and the cooldown is zero. The caller
// is responsible for clearing fire_requested via weapons_console_consume_fire().
void weapons_console_update(WeaponsConsole *w,
                            float player_x, float player_z,
                            joypad_inputs_t inputs);

// Returns true if a fire was requested this frame, and clears the flag so
// the same frame's input doesn't double-fire.
bool weapons_console_consume_fire(WeaponsConsole *w);

void weapons_console_draw(WeaponsConsole *w);

#endif // WEAPONS_CONSOLE_H
