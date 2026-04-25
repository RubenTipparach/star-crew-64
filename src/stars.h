#ifndef STARS_H
#define STARS_H

#include "game_config.h"

// 3D starfield: a fixed scatter of ~60 small textured quads on a spherical
// shell around the world origin. Each quad samples one of 4 star-type
// sprites (white / blue / yellow / red). Positions are precomputed once in
// stars_create; orientations are identity (quads lie flat on XZ). Because
// each star is only a few units wide, perspective foreshortening from the
// 3/4 camera renders them as tiny dots — the "billboard" look falls out for
// free without having to re-orient per frame.
#define STAR_TYPE_COUNT 4
#define STAR_COUNT      60

typedef struct {
    T3DVertPacked *quad;                          // small 4×4-unit quad (shared)
    T3DMat4FP     *matrices;                      // STAR_COUNT, one per star
    uint8_t       *tex_indices;                   // STAR_COUNT, 0..STAR_TYPE_COUNT-1
    sprite_t      *textures[STAR_TYPE_COUNT];     // white, blue, yellow, red
    int            num_stars;
} Stars;

Stars* stars_create(void);
void stars_draw(Stars *stars);

#endif // STARS_H
