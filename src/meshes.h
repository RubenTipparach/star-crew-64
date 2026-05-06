#ifndef MESHES_H
#define MESHES_H

#include "game_config.h"

// Create a textured cube mesh with the given texture dimensions
T3DVertPacked* mesh_create_cube(int texWidth, int texHeight);

// Create a small laser projectile cube with vertex colors
T3DVertPacked* mesh_create_laser_cube(void);

// Phase-8 capital ship: chunky red cube with darker faces, scaled up
// from the fighter so it reads as a much larger threat in the corner
// viewport. Same vertex-coloured no-texture pipeline as the fighter
// cube; only colours differ to distinguish at a glance.
T3DVertPacked* mesh_create_capital_cube(int16_t half);

// Phase-7 fire extinguisher prop: a tall red cube with a black top face
// simulating the cap. At the N64 framebuffer's resolution and at
// gameplay distances this reads as a red cylinder. half_xz is the body
// half-width on the floor plane; half_y is the half-height (cube origin
// is at the centre, so model offset Y = half_y to sit on the floor).
T3DVertPacked* mesh_create_extinguisher(int16_t half_xz, int16_t half_y);

// Draw a cube mesh (6 faces)
void mesh_draw_cube(T3DVertPacked *verts);

#endif // MESHES_H
