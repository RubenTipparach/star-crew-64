#include <math.h>

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
#define CAMERA_FAR       1300.0f  // must comfortably exceed STAR_R_MAX (700) + camera offset (~250)

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

// How much to scale the camera offset per world unit of player separation.
// At separation 0 the offset is 1× (default zoom); at separation
// SEPARATION_MAX it's about 1.7× — enough to keep both players in frame
// without the camera flying impossibly high on a normal-sized bridge.
#define ZOOM_SLOPE       0.0035f
#define ZOOM_MAX         1.7f
#define ZOOM_LERP        0.10f       // smooth zoom-in/out so it doesn't snap

static float current_zoom = 1.0f;

void camera_set_target_pair(Camera *camera,
                            float p1_x, float y, float p1_z,
                            float p2_x, float p2_z,
                            bool p2_active)
{
    float tx, tz;
    float target_zoom = 1.0f;

    if (p2_active) {
        tx = (p1_x + p2_x) * 0.5f;
        tz = (p1_z + p2_z) * 0.5f;
        float dx = p2_x - p1_x;
        float dz = p2_z - p1_z;
        float sep = sqrtf(dx * dx + dz * dz);
        target_zoom = 1.0f + sep * ZOOM_SLOPE;
        if (target_zoom > ZOOM_MAX) target_zoom = ZOOM_MAX;
    } else {
        tx = p1_x;
        tz = p1_z;
    }

    current_zoom += (target_zoom - current_zoom) * ZOOM_LERP;

    float ty = y + CAMERA_TARGET_Y_BIAS;
    camera->target = (T3DVec3){{tx, ty, tz}};
    camera->position = (T3DVec3){{
        tx + CAMERA_OFFSET_X * current_zoom,
        ty + CAMERA_OFFSET_Y * current_zoom,
        tz + CAMERA_OFFSET_Z * current_zoom,
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
