#ifndef SHIP_VIEW_H
#define SHIP_VIEW_H

#include "game_config.h"
#include "ship_model.h"

// Pixel size + position of the corner overlay (top-right). 50% larger than
// the original 80x60 (still 4:3) — fits the 320x240 frame with a 4px inset.
#define SHIP_VIEW_WIDTH    120
#define SHIP_VIEW_HEIGHT   90
#define SHIP_VIEW_X        (320 - SHIP_VIEW_WIDTH - 4)   // small inset from the screen edge
#define SHIP_VIEW_Y        4

// Number of background star billboards scattered around the corner-viewport
// camera. Each is a cool-stuff star_*.png sprite drawn as a small quad — the
// same approach src/stars.c uses for the bridge-exterior starfield. Bumped
// to 80 to give the 120×90 viewport a properly dense field.
#define SHIP_VIEW_STAR_COUNT  80
#define SHIP_VIEW_STAR_TYPES   4

// Projectile pool: shared between phasers (rapid yellow) and photon torpedoes
// (slow blue). Pool is intentionally small — N64 fillrate hates many alpha
// quads, and the gameplay doesn't need swarm fire.
#define SHIP_VIEW_PROJECTILES   8
#define PHASER_LIFETIME         60       // ~1s @ 60Hz, fast & short-lived
#define PHASER_SPEED            4.0f
#define TORPEDO_LIFETIME        150      // ~2.5s, drifts further before fading
#define TORPEDO_SPEED           1.8f

typedef enum {
    PROJ_PHASER  = 0,
    PROJ_TORPEDO = 1,
} ProjectileType;

typedef struct {
    float          x, y, z;
    float          vx, vy, vz;
    int            timer;        // remaining frames; <=0 means inactive
    ProjectileType type;
} ShipProjectile;

typedef struct {
    T3DViewport    viewport;       // its own viewport, sub-region of the framebuffer
    T3DVertPacked *verts;          // SHIP_NUM_TRIS * 2 packed structs
    T3DMat4FP     *matrices;       // FB_COUNT — animated each frame
    sprite_t      *texture;

    // Background star billboards. We allocate ALL star textures (white/blue/
    // yellow/red, sourced from cool-stuff's gen-textures.py), a shared
    // camera-aligned quad mesh, and per-star world positions. Matrices are
    // rebuilt every frame from positions + ship position so each star is a
    // true spherical billboard (quad normal aimed at the corner camera).
    sprite_t      *star_textures[SHIP_VIEW_STAR_TYPES];
    T3DVertPacked *star_quad;      // 2 packed structs, shared across stars
    T3DMat4FP     *star_matrices;  // SHIP_VIEW_STAR_COUNT entries
    float         *star_positions; // 3 * SHIP_VIEW_STAR_COUNT — XYZ in world
    uint8_t       *star_tex_idx;   // SHIP_VIEW_STAR_COUNT entries, into star_textures

    // World-space ship state. While "captive" (no pilot) the ship rides the
    // baked SHIP_IDLE animation; while a pilot drives it, steer rotates yaw
    // and impulse pushes it forward.
    float          yaw;            // radians, world-space heading
    float          x, y, z;        // world-space position (drift in +X+Z plane)
    float          vel;            // forward speed, units/frame
    float          drift_z;        // procedural forward drift integrated while idle

    // Idle clip playback (used only when pilot_active is false). Frame index
    // is float so we can sample fractional times for smooth playback.
    float          anim_frame;
    bool           pilot_active;   // mirrored from BridgePanel.player_active each frame

    // Projectile pool (fired by the weapons console). Inactive entries have
    // timer <= 0; we scan linearly for a free slot on spawn.
    ShipProjectile projectiles[SHIP_VIEW_PROJECTILES];
    T3DVertPacked *proj_mesh;            // small textureless cube, shared
    T3DMat4FP     *proj_matrices;        // FB_COUNT * SHIP_VIEW_PROJECTILES
} ShipView;

ShipView* ship_view_create(void);

// Step the ship state for the current frame and update its model matrix.
// `steer` is in [-1,+1], `impulse` is in [0,1]. When `pilot_active` is false,
// steer/impulse are ignored and the baked idle clip drives the motion.
void ship_view_update(ShipView *sv, int frameIdx,
                      bool pilot_active, float steer, float impulse);

// Spawn a projectile from the ship's nose. `aim_yaw_offset` is added to the
// ship's heading so the gunner can shoot off-axis from where the ship is
// pointed. Phaser vs. torpedo controls speed / lifetime / draw color. No-op
// if the pool is full.
void ship_view_fire(ShipView *sv, ProjectileType type, float aim_yaw_offset);

// Draw the ship + starfield into the corner of the current framebuffer. Must
// be called *after* the main scene has been rendered for this frame; this
// function temporarily swaps the active viewport / scissor.
void ship_view_draw(ShipView *sv, int frameIdx, T3DViewport *main_viewport);

#endif // SHIP_VIEW_H
