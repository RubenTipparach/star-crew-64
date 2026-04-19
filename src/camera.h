#ifndef CAMERA_H
#define CAMERA_H

#include "game_config.h"

// Camera state structure
typedef struct {
    T3DViewport viewport;
    T3DVec3 position;
    T3DVec3 target;
} Camera;

// Create and initialize the camera (3/4 view, looking at origin).
Camera camera_create(void);

// Reposition the camera so it frames the given world-space target.
// Keeps the fixed 3/4 offset.
void camera_set_target(Camera *camera, float x, float y, float z);

// Update camera viewport for the current frame.
void camera_update(Camera *camera);

// Attach camera viewport for rendering.
void camera_attach(Camera *camera);

#endif // CAMERA_H
