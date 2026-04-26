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
#include "prompts.h"

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

// Player-2 spawn offset from player-1 (so they don't spawn on top of each
// other). Same world height; offset along +X by one tile.
#define P2_SPAWN_OFFSET_X  (20.0f)

// World-space height where prompt billboards float above a character.
// Character is ~16 units tall; we put the prompt a bit higher than that so
// it doesn't clip into the head.
#define PROMPT_FLOAT_Y    24.0f
#define PROMPT_PAIR_GAP   18.0f

// Apply stick-driven movement (with axis-separated wall collision) to
// `hero`. Returns the resulting per-frame speed so the walk anim can sync.
static float move_hero(Character *hero, Level *level, joypad_inputs_t inputs)
{
    float sx = inputs.stick_x / STICK_DIVISOR;
    float sy = inputs.stick_y / STICK_DIVISOR;
    const float k = 0.7071f;
    float dx = (sx - sy) * k * MOVE_SPEED;
    float dz = -(sx + sy) * k * MOVE_SPEED;

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

    float speed = sqrtf(dx * dx + dz * dz);
    if (speed > 0.01f) {
        character_face_direction(hero, atan2f(dx, -dz), TURN_SMOOTHING);
    }
    return speed;
}

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

    // Detect controller 2. If it's plugged in at boot we spawn a co-op
    // partner; otherwise the game stays single-player. Hot-plug isn't
    // handled — keeps the loop branch-free.
    joypad_poll();
    bool p2_present = joypad_is_connected(JOYPAD_PORT_2);
    Character *hero2 = p2_present ? character_create() : NULL;

    BridgePanel *panel = bridge_panel_create(PANEL_WORLD_X, PANEL_WORLD_Z, PANEL_FACING);
    WeaponsConsole *weapons = weapons_console_create(
        WEAPONS_WORLD_X, WEAPONS_WORLD_Z, WEAPONS_FACING);
    ShipView *ship = ship_view_create();
    Stars *stars = stars_create();
    Prompts *prompts = prompts_create();

    // Spawn the hero on the "spawn" entity if one was placed in the editor;
    // otherwise leave him at the world origin (which is the grid's center).
    // P2 spawns slightly offset so they're side-by-side instead of stacked.
    if (level->has_spawn) {
        character_set_position(hero, level->spawn_wx, 0.0f, level->spawn_wz);
        if (hero2) {
            character_set_position(hero2,
                level->spawn_wx + P2_SPAWN_OFFSET_X, 0.0f, level->spawn_wz);
        }
    } else if (hero2) {
        character_set_position(hero2, P2_SPAWN_OFFSET_X, 0.0f, 0.0f);
    }

    // Static lights. LIGHT_HERO is overwritten each frame with the hero's
    // current position.
    PointLight lights[NUM_POINT_LIGHTS] = {
        [LIGHT_HALLWAY]   = { .position = {{   0, 35,   0}}, .color = {200, 210, 230, 255}, .size = 80.0f },
        [LIGHT_AIRLOCK_N] = { .position = {{   0, 30, -40}}, .color = {255, 190,  60, 255}, .size = 55.0f },
        [LIGHT_AIRLOCK_S] = { .position = {{   0, 30,  40}}, .color = {255, 190,  60, 255}, .size = 55.0f },
        [LIGHT_HERO]      = { .position = {{   0, 18,   0}}, .color = {255, 225, 160, 255}, .size = 40.0f },
    };

    // Camera follows the hero(es) each frame. Seed it here so the very first
    // frame isn't pointed at (0,0,0) while the hero is off at his spawn cell.
    camera_set_target_pair(&camera,
        hero->position.v[0], 5.0f, hero->position.v[2],
        hero2 ? hero2->position.v[0] : 0.0f,
        hero2 ? hero2->position.v[2] : 0.0f,
        hero2 != NULL);

    int frameIdx = 0;

    for (;;)
    {
        frameIdx = (frameIdx + 1) % FB_COUNT;

        joypad_poll();
        joypad_inputs_t inputs1 = joypad_get_inputs(JOYPAD_PORT_1);
        joypad_inputs_t inputs2 = hero2
            ? joypad_get_inputs(JOYPAD_PORT_2)
            : (joypad_inputs_t){0};

        // ---- Console interaction (player 1 only for now) ----
        // Co-op consoles are a future enhancement; for now P2 is a runner /
        // companion who can't drive the ship or fire. Their character still
        // walks freely.
        bool piloting = bridge_panel_update(panel,
            hero->position.v[0], hero->position.v[2], inputs1);
        bool gunning  = false;

        if (!piloting) {
            gunning = weapons_console_update(weapons,
                hero->position.v[0], hero->position.v[2], inputs1);
            if (weapons_console_consume_phaser(weapons))
                ship_view_fire(ship, PROJ_PHASER,  weapons->aim_yaw);
            if (weapons_console_consume_torpedo(weapons))
                ship_view_fire(ship, PROJ_TORPEDO, weapons->aim_yaw);
        } else {
            weapons->prev_a = inputs1.btn.a != 0;
            weapons->prev_b = inputs1.btn.b != 0;
            weapons->prev_z = inputs1.btn.z != 0;
            weapons->player_in_range = false;
        }

        // ---- Movement ----
        bool stationed = piloting || gunning;
        float speed1 = stationed ? 0.0f : move_hero(hero, level, inputs1);
        float speed2 = hero2 ? move_hero(hero2, level, inputs2) : 0.0f;

        character_animate(hero, speed1);
        if (hero2) character_animate(hero2, speed2);

        camera_set_target_pair(&camera,
            hero->position.v[0], 5.0f, hero->position.v[2],
            hero2 ? hero2->position.v[0] : 0.0f,
            hero2 ? hero2->position.v[2] : 0.0f,
            hero2 != NULL);
        camera_update(&camera);
        character_update_matrix(hero, frameIdx);
        if (hero2) character_update_matrix(hero2, frameIdx);
        ship_view_update(ship, frameIdx, piloting, panel->steer, panel->impulse);

        rdpq_attach(display_get(), display_get_zbuf());
        t3d_frame_start();
        camera_attach(&camera);

        t3d_screen_clear_color(RGBA32(12, 14, 22, 0xFF));
        t3d_screen_clear_depth();

        // Hero carries a soft follow-light — position it just above his head.
        // (Single follow-light still tracks P1; P2 is lit by ambient + the
        // room's static fixtures.)
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
        if (hero2) character_draw(hero2, frameIdx);

        // ---- Floating button prompts ----
        // Only P1 can drive the consoles right now, so prompts hover above
        // P1 only. While active at a station we show that station's
        // controls; while in proximity but not yet active we show the
        // single "press A to engage" hint.
        float p1x = hero->position.v[0];
        float p1y = hero->position.v[1] + PROMPT_FLOAT_Y;
        float p1z = hero->position.v[2];
        if (piloting) {
            // STICK (turn / impulse) + B (leave)
            prompts_draw_pair(prompts, PROMPT_STICK, PROMPT_B,
                              p1x, p1y, p1z, PROMPT_PAIR_GAP);
        } else if (gunning) {
            // A (phaser) + Z (torpedo); separate row would be ideal but we
            // can fit both side-by-side at the prompt size.
            prompts_draw_pair(prompts, PROMPT_A, PROMPT_Z,
                              p1x, p1y, p1z, PROMPT_PAIR_GAP);
        } else if (panel->player_in_range || weapons->player_in_range) {
            prompts_draw(prompts, PROMPT_A, p1x, p1y, p1z);
        }

        // Corner-viewport "tactical view" of the ship in space. Drawn last so
        // it overlays the bridge interior. The function manages its own
        // viewport / scissor and restores them before returning.
        ship_view_draw(ship, frameIdx, &camera.viewport);

        rdpq_sync_pipe();
        rdpq_set_mode_standard();

        // Bottom HUD: only show when P1 is at or near a console — the
        // billboard prompt is the primary cue, this is just a textual
        // backup that names the action.
        if (piloting) {
            rdpq_text_print(NULL, 1, 14, 224, "[B] LEAVE HELM");
        } else if (gunning) {
            rdpq_text_print(NULL, 1, 14, 224, "[B] LEAVE GUNNER STATION");
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
