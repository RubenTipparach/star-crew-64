#ifndef SHIP_VIEW_H
#define SHIP_VIEW_H

#include "game_config.h"
#include "ship_model.h"

// Pixel size + position of the corner overlay (top-right) — 1/4 of a 320x240
// frame ≈ 80x60. Constants live in the header so main.c can reason about them.
#define SHIP_VIEW_WIDTH    80
#define SHIP_VIEW_HEIGHT   60
#define SHIP_VIEW_X        (320 - SHIP_VIEW_WIDTH - 4)   // small inset from the screen edge
#define SHIP_VIEW_Y        4

// Number of background star billboards scattered around the corner-viewport
// camera. Each is a cool-stuff star_*.png sprite drawn as a small quad — the
// same approach src/stars.c uses for the bridge-exterior starfield. Scale is
// tuned so each star reads as a clear 2-3px dot at the 80×60 corner size.
#define SHIP_VIEW_STAR_COUNT  24
#define SHIP_VIEW_STAR_TYPES   4

typedef struct {
    T3DViewport    viewport;       // its own viewport, sub-region of the framebuffer
    T3DVertPacked *verts;          // SHIP_NUM_TRIS * 2 packed structs
    T3DMat4FP     *matrices;       // FB_COUNT — animated each frame
    sprite_t      *texture;

    // Background star billboards. We allocate ALL star textures (white/blue/
    // yellow/red, sourced from cool-stuff's gen-textures.py) and per-star
    // matrices wrapped in a small shared quad mesh.
    sprite_t      *star_textures[SHIP_VIEW_STAR_TYPES];
    T3DVertPacked *star_quad;      // 2 packed structs, shared across stars
    T3DMat4FP     *star_matrices;  // SHIP_VIEW_STAR_COUNT entries
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
} ShipView;

ShipView* ship_view_create(void);

// Step the ship state for the current frame and update its model matrix.
// `steer` is in [-1,+1], `impulse` is in [0,1]. When `pilot_active` is false,
// steer/impulse are ignored and the baked idle clip drives the motion.
void ship_view_update(ShipView *sv, int frameIdx,
                      bool pilot_active, float steer, float impulse);

// Draw the ship + starfield into the corner of the current framebuffer. Must
// be called *after* the main scene has been rendered for this frame; this
// function temporarily swaps the active viewport / scissor.
void ship_view_draw(ShipView *sv, int frameIdx, T3DViewport *main_viewport);

#endif // SHIP_VIEW_H
