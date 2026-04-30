#ifndef MESHES_H
#define MESHES_H

#include "game_config.h"

// Create a textured cube mesh with the given texture dimensions
T3DVertPacked* mesh_create_cube(int texWidth, int texHeight);

// Create a small laser projectile cube with vertex colors
T3DVertPacked* mesh_create_laser_cube(void);

// Create an enemy-fighter cube: vertex-coloured, no texture. Red base with a
// magenta-tinted dorsal so the silhouette reads as a menacing alien craft
// against the dark space background. `half` is the cube's half-extent in
// world units.
T3DVertPacked* mesh_create_enemy_cube(int16_t half);

// Draw a cube mesh (6 faces)
void mesh_draw_cube(T3DVertPacked *verts);

#endif // MESHES_H
