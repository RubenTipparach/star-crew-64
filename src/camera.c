#include "camera.h"

// 3/4 isometric-style view. Narrow FOV + elevated, diagonal position gives
// that FTL top-down-ish feel while still being a perspective projection.
#define CAMERA_FOV_DEG   50.0f
#define CAMERA_NEAR      10.0f
#define CAMERA_FAR       900.0f   // must comfortably exceed STAR_R_MAX (500) + camera offset (~105)

// Offset from the camera's target point. Y is height, X/Z are equal so we
// look down the diagonal.
#define CAMERA_OFFSET_X   60.0f
#define CAMERA_OFFSET_Y   80.0f
#define CAMERA_OFFSET_Z   60.0f

Camera camera_create(void)
{
    Camera camera;
    camera.viewport = t3d_viewport_create_buffered(FB_COUNT);
    camera.target   = (T3DVec3){{0, 5.0f, 0}};
    camera.position = (T3DVec3){{
        camera.target.v[0] + CAMERA_OFFSET_X,
        camera.target.v[1] + CAMERA_OFFSET_Y,
        camera.target.v[2] + CAMERA_OFFSET_Z,
    }};
    return camera;
}

void camera_set_target(Camera *camera, float x, float y, float z)
{
    camera->target = (T3DVec3){{x, y, z}};
    camera->position = (T3DVec3){{
        x + CAMERA_OFFSET_X,
        y + CAMERA_OFFSET_Y,
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
