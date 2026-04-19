/**
 * star-crew-64 - N64 Game
 * Built with libdragon + tiny3d
 */

#include <math.h>

#include "game_config.h"
#include "camera.h"
#include "character.h"
#include "level.h"
#include "lighting.h"

// Character movement speed in world units per frame (at ~60fps).
#define MOVE_SPEED 0.6f

// Four point lights: two amber ones hovering over the airlock rows, one white
// over the hallway centre, and one that follows the character (updated each
// frame). Tuned so the interior reads as lit without swamping the key light.
#define LIGHT_HALLWAY  0
#define LIGHT_AIRLOCK_N 1
#define LIGHT_AIRLOCK_S 2
#define LIGHT_HERO      3
#define NUM_POINT_LIGHTS 4

int main(void)
{
    debug_init_isviewer();
    debug_init_usblog();

    dfs_init(DFS_DEFAULT_LOCATION);
    joypad_init();

    display_init(RESOLUTION_320x240, DEPTH_16_BPP, FB_COUNT, GAMMA_NONE, FILTERS_RESAMPLE);
    rdpq_init();
    t3d_init((T3DInitParams){});

    rdpq_font_t *font = rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO);
    rdpq_text_register_font(1, font);

    Camera camera = camera_create();
    Level *level = level_create();
    Character *hero = character_create();

    // Static lights. LIGHT_HERO is overwritten each frame with the hero's
    // current position.
    PointLight lights[NUM_POINT_LIGHTS] = {
        [LIGHT_HALLWAY]   = { .position = {{   0, 35,   0}}, .color = {200, 210, 230, 255}, .size = 80.0f },
        [LIGHT_AIRLOCK_N] = { .position = {{   0, 30, -40}}, .color = {255, 190,  60, 255}, .size = 55.0f },
        [LIGHT_AIRLOCK_S] = { .position = {{   0, 30,  40}}, .color = {255, 190,  60, 255}, .size = 55.0f },
        [LIGHT_HERO]      = { .position = {{   0, 18,   0}}, .color = {255, 225, 160, 255}, .size = 40.0f },
    };

    // Center camera on the level.
    float lx, lz;
    level_get_center(level, &lx, &lz);
    camera_set_target(&camera, lx, 5.0f, lz);

    int frameIdx = 0;

    for (;;)
    {
        frameIdx = (frameIdx + 1) % FB_COUNT;

        joypad_poll();
        joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);

        // Camera sits at (+X, +Y, +Z) looking at the origin, so its forward
        // vector projected onto XZ is (-k, -k) and its right vector is
        // (+k, -k), where k = √2/2. Map the stick into those basis vectors so
        // stick-up moves the hero into the scene (camera-forward) and
        // stick-right moves along camera-right.
        float sx = inputs.stick_x / STICK_DIVISOR;
        float sy = -inputs.stick_y / STICK_DIVISOR;   // stick up should go into the scene
        const float k = 0.7071f;
        float dx = (sx - sy) * k * MOVE_SPEED;   // sx·right.x + sy·forward.x
        float dz = -(sx + sy) * k * MOVE_SPEED;  // sx·right.z + sy·forward.z
        hero->position.v[0] += dx;
        hero->position.v[2] += dz;
        float speed = sqrtf(dx * dx + dz * dz);
        if (speed > 0.01f) {
            hero->rot_y = atan2f(dx, dz);
        }

        // character_animate(hero, speed);  // walk cycle disabled — swing needs tuning
        camera_update(&camera);
        character_update_matrix(hero, frameIdx);

        rdpq_attach(display_get(), display_get_zbuf());
        t3d_frame_start();
        camera_attach(&camera);

        t3d_screen_clear_color(RGBA32(12, 14, 22, 0xFF));
        t3d_screen_clear_depth();

        // Hero carries a soft follow-light — position it just above his head.
        lights[LIGHT_HERO].position = (T3DVec3){{
            hero->position.v[0],
            hero->position.v[1] + 18.0f,
            hero->position.v[2],
        }};

        lighting_setup_main();
        int totalLights = lighting_apply_points(lights, NUM_POINT_LIGHTS);
        lighting_finalize(totalLights);

        level_draw(level);
        character_draw(hero, frameIdx);

        rdpq_sync_pipe();
        rdpq_set_mode_standard();
        rdpq_text_print(NULL, 1, 100, 20, "STAR CREW 64");

        rdpq_detach_show();
    }

    t3d_destroy();
    return 0;
}
