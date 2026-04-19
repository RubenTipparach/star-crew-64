#ifndef LIGHTING_H
#define LIGHTING_H

#include "game_config.h"

// A positional light source (attenuated by distance).
typedef struct {
    T3DVec3 position;        // world-space position
    uint8_t color[4];        // RGBA (alpha is ignored by t3d but keep 4 bytes for alignment)
    float   size;            // falloff size — tune per-light, roughly the radius at which it fades out
} PointLight;

// Setup main scene lighting: ambient + one directional "sun" at slot 0.
void lighting_setup_main(void);

// Register point lights starting at slot 1. Returns the slot index AFTER the
// last point light (i.e. 1 + number_used), clamped to t3d's 8-light maximum.
int lighting_apply_points(const PointLight *lights, int count);

// Finalize with total light count (= value returned by lighting_apply_points,
// or 1 when only the directional main light is active).
void lighting_finalize(int lightCount);

// Kept for the old laser demo. Unused by the new game loop but retained so
// laser.c + the old lighting path still link.
int lighting_add_laser_lights(Laser *lasers, int maxLasers);

#endif // LIGHTING_H
