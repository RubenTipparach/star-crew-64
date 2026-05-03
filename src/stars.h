#ifndef STARS_H
#define STARS_H

#include "game_config.h"

// Starfield: a fixed scatter of small 8×8 sprites on a spherical shell
// around the world origin. Each sprite is drawn as a 2D rdpq_sprite_blit at
// the projected screen position of its 3D world point, which keeps the
// "stars rotate with the camera" parallax look while sidestepping the
// libdragon/t3d cutout-transparency pitfalls of the 3D textured-quad path
// (alpha-compare + textured-shaded combiner + edge AA together produced
// 8×8 opaque squares instead of dots, no matter which mode bit we set).
// Each star samples one of 4 type sprites (white / blue / yellow / red).
#define STAR_TYPE_COUNT 4
#define STAR_COUNT      160

typedef struct {
    T3DVec3   *positions;                        // STAR_COUNT world positions
    uint8_t   *tex_indices;                      // STAR_COUNT, 0..STAR_TYPE_COUNT-1
    sprite_t  *textures[STAR_TYPE_COUNT];        // white, blue, yellow, red
    int        num_stars;
} Stars;

Stars* stars_create(void);
void stars_draw(Stars *stars, T3DViewport *viewport);

#endif // STARS_H
