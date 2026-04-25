#include "camera.h"

// 3/4 isometric-style view. Narrow FOV + elevated, diagonal position gives
// that FTL top-down-ish feel while still being a perspective projection.
//
// The camera frames the player into the bottom half of the screen so the
// upper half can show the bridge interior + the corner ship viewport. We
// achieve this by pushing the look-at target up along +Y above the
// character's head, *and* zooming out (larger XYZ offset) so the player
// reads as a smaller figure in the lower part of the frame.
#define CAMERA_FOV_DEG   55.0f
#define CAMERA_NEAR      10.0f
#define CAMERA_FAR       600.0f

// Offset from the look-at point to the camera position. Y is the up axis;
// pulling X/Y/Z proportionally further out zooms the view.
#define CAMERA_OFFSET_X   85.0f
#define CAMERA_OFFSET_Y  120.0f
#define CAMERA_OFFSET_Z   85.0f

// Vertical bias added to the focus point so the player sits in the bottom
// half of the framing. Larger values push the player further down-screen.
#define CAMERA_TARGET_Y_BIAS  35.0f

Camera camera_create(void)
{
    Camera camera;
    camera.viewport = t3d_viewport_create_buffered(FB_COUNT);
    camera.target   = (T3DVec3){{0, CAMERA_TARGET_Y_BIAS, 0}};
    camera.position = (T3DVec3){{
        camera.target.v[0] + CAMERA_OFFSET_X,
        camera.target.v[1] + CAMERA_OFFSET_Y,
        camera.target.v[2] + CAMERA_OFFSET_Z,
    }};
    return camera;
}

void camera_set_target(Camera *camera, float x, float y, float z)
{
    // Lift the focus point above the character's head (+Y) so the rendered
    // result has him in the bottom half of the screen.
    float ty = y + CAMERA_TARGET_Y_BIAS;
    camera->target = (T3DVec3){{x, ty, z}};
    camera->position = (T3DVec3){{
        x + CAMERA_OFFSET_X,
        ty + CAMERA_OFFSET_Y,
        z + CAMERA_OFFSET_Z,
    }};
}

void camera_update(Camera *camera)
{
    t3d_viewport_set_projection(&camera->viewport,
        T3D_DEG_TO_RAD(CAMERA_FOV_DEG), CAMERA_NEAR, CAMERA_FAR);
    t3d_viewport_look_at(&camera->viewport,
        &camera->position, &camera->target, &(T3DVec3){{0, 1, 0}});
}

void camera_attach(Camera *camera)
{
    t3d_viewport_attach(&camera->viewport);
}
