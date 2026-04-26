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
#include "bridge_panel.h"
#include "weapons_console.h"
#include "ship_view.h"
#include "stars.h"

// Character movement speed in world units per frame (at ~60fps).
#define MOVE_SPEED 0.6f

// Which level to boot into. The Makefile injects this as -DSTARTING_LEVEL=...
// from the `starting_map` field of levels/levels.json; the fallback is only
// hit if that shell step ever fails (e.g. malformed JSON).
#ifndef STARTING_LEVEL
#define STARTING_LEVEL "starting"
#endif
#define STARTING_LEVEL_PATH "rom:/" STARTING_LEVEL ".lvl"

// How quickly the hero rotates toward the stick direction (0..1 per frame).
#define TURN_SMOOTHING 0.2f

// Four point lights: two amber ones hovering over the airlock rows, one white
// over the hallway centre, and one that follows the character (updated each
// frame). Tuned so the interior reads as lit without swamping the key light.
#define LIGHT_HALLWAY  0
#define LIGHT_AIRLOCK_N 1
#define LIGHT_AIRLOCK_S 2
#define LIGHT_HERO      3
#define NUM_POINT_LIGHTS 4

// Where to place the bridge control panel in world coords. The starting
// level's "Bridge" room sits roughly around cell (5, 10) — see
// levels/starting.json. With TILE_SIZE=20 and a 30x20 grid centered on the
// origin, that cell maps to world (-195, 10), so we put the panel a couple
// tiles in from the bridge's far wall, facing -X (toward the player).
#define PANEL_WORLD_X   (-180.0f)
#define PANEL_WORLD_Z   (10.0f)
#define PANEL_FACING    (3.1415927f * 0.5f)   // face +X (away from the wall)

// Weapons console lives in the Engines room — far away from the bridge so
// the gunner station is its own physical area and there's no chance of B
// conflicting with the bridge panel's "impulse" hold. Engines spans
// cells (17,7)..(23,12); world center ≈ (110, 0). Facing -π/2 puts the
// screen visible from the -X side (where the hallway approaches).
#define WEAPONS_WORLD_X (110.0f)
#define WEAPONS_WORLD_Z (0.0f)
#define WEAPONS_FACING  (-3.1415927f * 0.5f)

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
    Level *level = level_load(STARTING_LEVEL_PATH);
    Character *hero = character_create();
    BridgePanel *panel = bridge_panel_create(PANEL_WORLD_X, PANEL_WORLD_Z, PANEL_FACING);
    WeaponsConsole *weapons = weapons_console_create(
        WEAPONS_WORLD_X, WEAPONS_WORLD_Z, WEAPONS_FACING);
    ShipView *ship = ship_view_create();
    Stars *stars = stars_create();

    // Spawn the hero on the "spawn" entity if one was placed in the editor;
    // otherwise leave him at the world origin (which is the grid's center).
    if (level->has_spawn) {
        character_set_position(hero, level->spawn_wx, 0.0f, level->spawn_wz);
    }

    // Static lights. LIGHT_HERO is overwritten each frame with the hero's
    // current position.
    PointLight lights[NUM_POINT_LIGHTS] = {
        [LIGHT_HALLWAY]   = { .position = {{   0, 35,   0}}, .color = {200, 210, 230, 255}, .size = 80.0f },
        [LIGHT_AIRLOCK_N] = { .position = {{   0, 30, -40}}, .color = {255, 190,  60, 255}, .size = 55.0f },
        [LIGHT_AIRLOCK_S] = { .position = {{   0, 30,  40}}, .color = {255, 190,  60, 255}, .size = 55.0f },
        [LIGHT_HERO]      = { .position = {{   0, 18,   0}}, .color = {255, 225, 160, 255}, .size = 40.0f },
    };

    // Camera follows the hero each frame (see loop body). Seed it here so the
    // very first frame isn't pointed at (0,0,0) while the hero is off at his
    // spawn cell.
    camera_set_target(&camera, hero->position.v[0], 5.0f, hero->position.v[2]);

    int frameIdx = 0;

    for (;;)
    {
        frameIdx = (frameIdx + 1) % FB_COUNT;

        joypad_poll();
        joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);

        // Run station updates. The player can be at most one of these at a
        // time; the consoles are in different rooms.
        bool piloting = bridge_panel_update(panel,
            hero->position.v[0], hero->position.v[2], inputs);
        bool gunning  = false;

        if (!piloting) {
            gunning = weapons_console_update(weapons,
                hero->position.v[0], hero->position.v[2], inputs);
            if (weapons_console_consume_phaser(weapons))
                ship_view_fire(ship, PROJ_PHASER,  weapons->aim_yaw);
            if (weapons_console_consume_torpedo(weapons))
                ship_view_fire(ship, PROJ_TORPEDO, weapons->aim_yaw);
        } else {
            // Keep edge-detect state coherent so the next post-piloting press
            // still fires correctly.
            weapons->prev_a = inputs.btn.a != 0;
            weapons->prev_b = inputs.btn.b != 0;
            weapons->prev_z = inputs.btn.z != 0;
            weapons->player_in_range = false;
        }

        bool stationed = piloting || gunning;
        float speed = 0.0f;
        if (!stationed) {
            // Camera sits at (+X, +Y, +Z) looking at the origin, so its forward
            // vector projected onto XZ is (-k, -k) and its right vector is
            // (+k, -k), where k = √2/2. Map the stick into those basis vectors
            // so stick-up moves the hero into the scene (camera-forward) and
            // stick-right moves along camera-right.
            float sx = inputs.stick_x / STICK_DIVISOR;
            float sy = inputs.stick_y / STICK_DIVISOR;
            const float k = 0.7071f;
            float dx = (sx - sy) * k * MOVE_SPEED;
            float dz = -(sx + sy) * k * MOVE_SPEED;

            // Axis-separated collision: apply each axis only if it lands on a
            // walkable tile, so the hero slides along walls instead of
            // stopping dead when motion is blocked on one axis.
            float hx = hero->position.v[0];
            float hz = hero->position.v[2];
            if (level_is_walkable(level, hx + dx, hz)) {
                hero->position.v[0] = hx + dx;
            } else {
                dx = 0.0f;
            }
            if (level_is_walkable(level, hero->position.v[0], hz + dz)) {
                hero->position.v[2] = hz + dz;
            } else {
                dz = 0.0f;
            }

            speed = sqrtf(dx * dx + dz * dz);
            if (speed > 0.01f) {
                character_face_direction(hero, atan2f(dx, -dz), TURN_SMOOTHING);
            }
        }

        character_animate(hero, speed);
        camera_set_target(&camera, hero->position.v[0], 5.0f, hero->position.v[2]);
        camera_update(&camera);
        character_update_matrix(hero, frameIdx);
        ship_view_update(ship, frameIdx, piloting, panel->steer, panel->impulse);

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

        stars_draw(stars);
        level_draw(level);
        bridge_panel_draw(panel);
        weapons_console_draw(weapons);
        character_draw(hero, frameIdx);

        // Corner-viewport "tactical view" of the ship in space. Drawn last so
        // it overlays the bridge interior. The function manages its own
        // viewport / scissor and restores them before returning.
        ship_view_draw(ship, frameIdx, &camera.viewport);

        rdpq_sync_pipe();
        rdpq_set_mode_standard();

        // Console UI: two states per station.
        //   in range, not active  → "Press A to <verb>"
        //   active                → controls reference, plus "B to leave"
        // Active prompts get two stacked lines so the layout fits the screen
        // even with the verbose control list.
        if (piloting) {
            rdpq_text_print(NULL, 1, 14, 210,
                            "STICK: turn / forward / reverse");
            rdpq_text_print(NULL, 1, 14, 224,
                            "[B] LEAVE HELM");
        } else if (gunning) {
            rdpq_text_print(NULL, 1, 14, 210,
                            "STICK: aim   [A] PHASER   [Z] TORPEDO");
            rdpq_text_print(NULL, 1, 14, 224,
                            "[B] LEAVE GUNNER STATION");
        } else if (panel->player_in_range) {
            rdpq_text_print(NULL, 1, 60, 222, "[A] TAKE THE HELM");
        } else if (weapons->player_in_range) {
            rdpq_text_print(NULL, 1, 60, 222, "[A] MAN WEAPONS");
        }

        rdpq_detach_show();
    }

    t3d_destroy();
    return 0;
}
