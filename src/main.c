/**
 * star-crew-64 - N64 Game
 * Built with libdragon + tiny3d
 */

#include <math.h>
#include <stdio.h>

#include "game_config.h"
#include "camera.h"
#include "character.h"
#include "level.h"
#include "lighting.h"
#include "lobby.h"
#include "bridge_panel.h"
#include "weapons_console.h"
#include "engineering_console.h"
#include "science_console.h"
#include "ship_view.h"
#include "stars.h"
#include "prompts.h"

#define MOVE_SPEED 0.6f

#ifndef STARTING_LEVEL
#define STARTING_LEVEL "starting"
#endif
#define STARTING_LEVEL_PATH "rom:/" STARTING_LEVEL ".lvl"

#define TURN_SMOOTHING 0.2f

#define LIGHT_HALLWAY  0
#define LIGHT_AIRLOCK_N 1
#define LIGHT_AIRLOCK_S 2
#define LIGHT_HERO      3
#define NUM_POINT_LIGHTS 4

// ---- Station placements -----------------------------------------------
// Helm in the Bridge room (cells 2..7 / 8..11 → world ~(-200..-160) / (-30..30)).
#define HELM_WORLD_X    (-180.0f)
#define HELM_WORLD_Z    (10.0f)
#define HELM_FACING     (3.1415927f * 0.5f)    // front faces +X (toward player approach)

// Weapons in the Engines room (cells 17..23 / 7..12 → world ~(50..170) / (-50..50)).
#define WEAPONS_WORLD_X (110.0f)
#define WEAPONS_WORLD_Z (0.0f)
#define WEAPONS_FACING  (-3.1415927f * 0.5f)   // front faces -X (hallway approach)

// Engineering in the Mess room (cells 10..14 / 12..15 → world ~(-90..0) / (50..110)).
// The corridor drops into the Mess at cell (12, 11) from the south, so the
// player approaches from -Z. Yaw 0 means the mesh's local -Z front maps to
// world -Z, presenting the screen to the incoming player.
#define ENG_WORLD_X     (-30.0f)
#define ENG_WORLD_Z     (90.0f)
#define ENG_FACING      (0.0f)                  // front faces -Z (toward hallway)

// Science in the Quarters room (cells 10..14 / 4..7 → world ~(-90..0) / (-110..-50)).
// Corridor enters at cell (12, 8) from the north (hallway side), so the
// player approaches from +Z. Yaw π flips the front to face +Z.
#define SCI_WORLD_X     (-30.0f)
#define SCI_WORLD_Z     (-90.0f)
#define SCI_FACING      (3.1415927f)            // front faces +Z (toward hallway)

// Per-player spawn offsets from the level spawn point. Indexed by player
// slot; values are added to the spawn (x, z) so co-op players don't stack.
// Tuned so all four spots stay inside the Bridge room when the level uses
// the default starting.json (spawn at (-200, 10), bridge spans x ∈ [-200, -160], z ∈ [-30, 30]).
static const float SPAWN_OFFSETS[4][2] = {
    {   0.0f,   0.0f },   // P1 — on the spawn marker
    {   0.0f, -20.0f },   // P2 — one tile north
    {   0.0f,  20.0f },   // P3 — one tile south
    {  20.0f,   0.0f },   // P4 — one tile east (toward the helm exit)
};

#define PROMPT_FLOAT_Y    24.0f
#define PROMPT_PAIR_GAP   18.0f

#define MAX_PLAYERS 4

typedef enum {
    STATION_NONE     = -1,
    STATION_HELM     =  0,
    STATION_WEAPONS  =  1,
    STATION_ENG      =  2,
    STATION_SCIENCE  =  3,
} StationId;

typedef struct {
    Character *hero;
    bool       present;
    int        station;        // StationId or STATION_NONE while walking
} PlayerSlot;

static const joypad_port_t PADS[MAX_PLAYERS] = {
    JOYPAD_PORT_1, JOYPAD_PORT_2, JOYPAD_PORT_3, JOYPAD_PORT_4
};

// Forward decls so we can lay out small helpers naturally.
static bool any_console_blocks(const BridgePanel *helm,
                               const WeaponsConsole *weapons,
                               const EngineeringConsole *eng,
                               const ScienceConsole *sci,
                               float wx, float wz)
{
    return bridge_panel_blocks(helm, wx, wz)
        || weapons_console_blocks(weapons, wx, wz)
        || engineering_console_blocks(eng, wx, wz)
        || science_console_blocks(sci, wx, wz);
}

// Stick-driven movement with axis-separated wall + console collision.
// Returns the resulting per-frame speed so the walk anim can sync.
static float move_hero(Character *hero, Level *level,
                       const BridgePanel *helm,
                       const WeaponsConsole *weapons,
                       const EngineeringConsole *eng,
                       const ScienceConsole *sci,
                       joypad_inputs_t inputs)
{
    float sx = inputs.stick_x / STICK_DIVISOR;
    float sy = inputs.stick_y / STICK_DIVISOR;
    const float k = 0.7071f;
    float dx = (sx - sy) * k * MOVE_SPEED;
    float dz = -(sx + sy) * k * MOVE_SPEED;

    float hx = hero->position.v[0];
    float hz = hero->position.v[2];

    float try_x = hx + dx;
    if (level_is_walkable(level, try_x, hz)
        && !any_console_blocks(helm, weapons, eng, sci, try_x, hz)) {
        hero->position.v[0] = try_x;
    } else {
        dx = 0.0f;
    }
    float try_z = hz + dz;
    if (level_is_walkable(level, hero->position.v[0], try_z)
        && !any_console_blocks(helm, weapons, eng, sci, hero->position.v[0], try_z)) {
        hero->position.v[2] = try_z;
    } else {
        dz = 0.0f;
    }

    float speed = sqrtf(dx * dx + dz * dz);
    if (speed > 0.01f) {
        character_face_direction(hero, atan2f(dx, -dz), TURN_SMOOTHING);
    }
    return speed;
}

// Snap a character to a station seat (position + facing) in one step. Used
// when the player engages a console so they're always at the right pose to
// drive it.
static void seat_player(Character *c, float seat_x, float seat_z, float seat_yaw)
{
    character_set_position(c, seat_x, 0.0f, seat_z);
    c->rot_y = seat_yaw;
}

// Renders the engineering HUD: three vertical energy bars, the active
// selection, and a repair-pulse indicator.
static void draw_eng_hud(int font_id, const EngineeringConsole *e)
{
    const int x0 = 14;
    const int y0 = 60;
    const int bar_w = 18;
    const int bar_h = 110;
    const int gap   = 6;
    const char *labels[ENG_NUM_SYSTEMS] = {"ENG", "WPN", "SHD"};

    rdpq_set_mode_fill(RGBA32(20, 20, 30, 0xFF));
    rdpq_fill_rectangle(x0 - 4, y0 - 16, x0 + (bar_w + gap) * 3 + 4, y0 + bar_h + 18);

    for (int i = 0; i < ENG_NUM_SYSTEMS; i++) {
        int bx = x0 + i * (bar_w + gap);
        // Slot frame
        rdpq_set_mode_fill(RGBA32(40, 40, 50, 0xFF));
        rdpq_fill_rectangle(bx, y0, bx + bar_w, y0 + bar_h);
        // Filled portion
        int fill_h = (int)((e->energy[i] / 100.0f) * (float)bar_h);
        if (fill_h > bar_h) fill_h = bar_h;
        if (fill_h > 0) {
            uint8_t r = 240, g = 175, b = 35;
            if (i == e->selected) { r = 255; g = 220; b = 90; }
            rdpq_set_mode_fill(RGBA32(r, g, b, 0xFF));
            rdpq_fill_rectangle(bx + 2, y0 + bar_h - fill_h,
                                bx + bar_w - 2, y0 + bar_h);
        }
        rdpq_set_mode_standard();
        rdpq_text_print(NULL, font_id, bx, y0 - 2, labels[i]);
        char val[8]; snprintf(val, sizeof val, "%d", (int)(e->energy[i] + 0.5f));
        rdpq_text_print(NULL, font_id, bx, y0 + bar_h + 12, val);
    }

    if (engineering_console_repair_active(e)) {
        rdpq_text_print(NULL, font_id, x0, y0 + bar_h + 26, "REPAIR PULSE ACTIVE");
    } else if (e->repair_cooldown > 0) {
        char buf[24];
        snprintf(buf, sizeof buf, "PULSE COOL %d", e->repair_cooldown / 60 + 1);
        rdpq_text_print(NULL, font_id, x0, y0 + bar_h + 26, buf);
    }
}

// Renders the science HUD: scrolling notes track + shield bar.
static void draw_sci_hud(int font_id, const ScienceConsole *s)
{
    // Track is a horizontal bar near the bottom of the screen; notes scroll
    // right toward a hit line. Keep it in the lower-left so the corner ship
    // viewport (top-right) stays visible.
    const int track_x0 = 14;
    const int track_y0 = 168;
    const int track_w  = 220;
    const int track_h  = 22;
    const int hit_x    = track_x0 + track_w - 14;

    rdpq_set_mode_fill(RGBA32(20, 28, 40, 0xFF));
    rdpq_fill_rectangle(track_x0 - 4, track_y0 - 16, track_x0 + track_w + 4, track_y0 + track_h + 4);

    // Track lane.
    rdpq_set_mode_fill(RGBA32(30, 50, 70, 0xFF));
    rdpq_fill_rectangle(track_x0, track_y0, track_x0 + track_w, track_y0 + track_h);
    // Hit line.
    rdpq_set_mode_fill(RGBA32(180, 240, 255, 0xFF));
    rdpq_fill_rectangle(hit_x - 1, track_y0 - 2, hit_x + 2, track_y0 + track_h + 2);

    // Notes.
    for (int i = 0; i < SCI_TRACK_LEN; i++) {
        if (s->notes[i] < 0.0f) continue;
        int nx = track_x0 + (int)(s->notes[i] * (float)(track_w - 14));
        rdpq_set_mode_fill(RGBA32(90, 200, 255, 0xFF));
        rdpq_fill_rectangle(nx, track_y0 + 4, nx + 8, track_y0 + track_h - 4);
    }

    rdpq_set_mode_standard();
    rdpq_text_print(NULL, font_id, track_x0, track_y0 - 4, "RHYTHM:");

    // Shield bar above the rhythm track.
    const int sb_x = track_x0;
    const int sb_y = track_y0 - 14;
    const int sb_w = track_w;
    rdpq_set_mode_fill(RGBA32(30, 40, 60, 0xFF));
    rdpq_fill_rectangle(sb_x, sb_y, sb_x + sb_w, sb_y + 6);
    int fill = (int)((s->shield / SHIELD_MAX) * (float)sb_w);
    if (fill > 0) {
        rdpq_set_mode_fill(RGBA32(120, 220, 255, 0xFF));
        rdpq_fill_rectangle(sb_x, sb_y, sb_x + fill, sb_y + 6);
    }
    rdpq_set_mode_standard();
    char sbuf[24]; snprintf(sbuf, sizeof sbuf, "SHIELDS %d", (int)(s->shield + 0.5f));
    rdpq_text_print(NULL, font_id, sb_x + sb_w + 4, sb_y + 6, sbuf);

    if (s->feedback_timer > 0) {
        rdpq_text_print(NULL, font_id, hit_x - 12, track_y0 - 4, "HIT!");
    } else if (s->feedback_timer < 0) {
        rdpq_text_print(NULL, font_id, hit_x - 12, track_y0 - 4, "MISS");
    }
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

    // ---- Lobby ----
    int ready_mask = lobby_run(1);

    // ---- Game setup ----
    Camera camera = camera_create();
    Level *level = level_load(STARTING_LEVEL_PATH);

    PlayerSlot players[MAX_PLAYERS] = {0};
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (ready_mask & (1 << i)) {
            players[i].hero = character_create();
            players[i].present = true;
            players[i].station = STATION_NONE;
            float spawn_x = level->has_spawn ? level->spawn_wx : 0.0f;
            float spawn_z = level->has_spawn ? level->spawn_wz : 0.0f;
            character_set_position(players[i].hero,
                                   spawn_x + SPAWN_OFFSETS[i][0], 0.0f,
                                   spawn_z + SPAWN_OFFSETS[i][1]);
        }
    }

    BridgePanel       *helm    = bridge_panel_create(HELM_WORLD_X, HELM_WORLD_Z, HELM_FACING);
    WeaponsConsole    *weapons = weapons_console_create(WEAPONS_WORLD_X, WEAPONS_WORLD_Z, WEAPONS_FACING);
    EngineeringConsole *eng    = engineering_console_create(ENG_WORLD_X, ENG_WORLD_Z, ENG_FACING);
    ScienceConsole    *sci     = science_console_create(SCI_WORLD_X, SCI_WORLD_Z, SCI_FACING);
    ShipView *ship = ship_view_create();
    Stars *stars = stars_create();
    Prompts *prompts = prompts_create();

    PointLight lights[NUM_POINT_LIGHTS] = {
        [LIGHT_HALLWAY]   = { .position = {{   0, 35,   0}}, .color = {200, 210, 230, 255}, .size = 80.0f },
        [LIGHT_AIRLOCK_N] = { .position = {{   0, 30, -40}}, .color = {255, 190,  60, 255}, .size = 55.0f },
        [LIGHT_AIRLOCK_S] = { .position = {{   0, 30,  40}}, .color = {255, 190,  60, 255}, .size = 55.0f },
        [LIGHT_HERO]      = { .position = {{   0, 18,   0}}, .color = {255, 225, 160, 255}, .size = 40.0f },
    };

    // Camera frames the first two ready players for the existing pair-
    // framing path. With 3+ players we just frame P1 — keeps the corner
    // viewport stable.
    Character *p1_hero = players[0].present ? players[0].hero : NULL;
    Character *p2_hero = players[1].present ? players[1].hero : NULL;
    if (p1_hero) {
        camera_set_target_pair(&camera,
            p1_hero->position.v[0], 5.0f, p1_hero->position.v[2],
            p2_hero ? p2_hero->position.v[0] : 0.0f,
            p2_hero ? p2_hero->position.v[2] : 0.0f,
            p2_hero != NULL);
    }

    int frameIdx = 0;

    for (;;)
    {
        frameIdx = (frameIdx + 1) % FB_COUNT;

        joypad_poll();
        joypad_inputs_t inputs[MAX_PLAYERS] = {0};
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (players[i].present) inputs[i] = joypad_get_inputs(PADS[i]);
        }

        // ---- Try-engage / drive each player ----
        // The try_engage paths handle the lock: if the console is held by a
        // different pid, the attempt no-ops. drive() returns false when the
        // active player presses B (or otherwise releases the seat) so we can
        // mark the player as walking again.
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!players[i].present) continue;
            Character *hero = players[i].hero;

            switch (players[i].station) {
            case STATION_HELM:
                if (!bridge_panel_drive(helm, i, inputs[i])) {
                    players[i].station = STATION_NONE;
                }
                break;
            case STATION_WEAPONS:
                if (!weapons_console_drive(weapons, i, inputs[i])) {
                    players[i].station = STATION_NONE;
                } else {
                    if (weapons_console_consume_phaser(weapons))
                        ship_view_fire(ship, PROJ_PHASER,  weapons->aim_yaw);
                    if (weapons_console_consume_torpedo(weapons))
                        ship_view_fire(ship, PROJ_TORPEDO, weapons->aim_yaw);
                }
                break;
            case STATION_ENG:
                if (!engineering_console_drive(eng, i, inputs[i])) {
                    players[i].station = STATION_NONE;
                }
                break;
            case STATION_SCIENCE:
                if (!science_console_drive(sci, i, inputs[i])) {
                    players[i].station = STATION_NONE;
                }
                break;
            case STATION_NONE:
            default:
                if (bridge_panel_try_engage(helm, i,
                        hero->position.v[0], hero->position.v[2], inputs[i])) {
                    players[i].station = STATION_HELM;
                    seat_player(hero, helm->seat_x, helm->seat_z, helm->seat_yaw);
                } else if (weapons_console_try_engage(weapons, i,
                        hero->position.v[0], hero->position.v[2], inputs[i])) {
                    players[i].station = STATION_WEAPONS;
                    seat_player(hero, weapons->seat_x, weapons->seat_z, weapons->seat_yaw);
                } else if (engineering_console_try_engage(eng, i,
                        hero->position.v[0], hero->position.v[2], inputs[i])) {
                    players[i].station = STATION_ENG;
                    seat_player(hero, eng->seat_x, eng->seat_z, eng->seat_yaw);
                } else if (science_console_try_engage(sci, i,
                        hero->position.v[0], hero->position.v[2], inputs[i])) {
                    players[i].station = STATION_SCIENCE;
                    seat_player(hero, sci->seat_x, sci->seat_z, sci->seat_yaw);
                }
                break;
            }
        }

        // Drive station decay tick for stations with no occupant. This is
        // important for the helm so the ship coasts back to idle when no one
        // is piloting, and for engineering so the repair-pulse timer counts
        // down even when nobody is at the console.
        if (helm->occupant_pid == -1)    bridge_panel_drive(helm, -1, (joypad_inputs_t){0});
        if (eng->occupant_pid == -1)     engineering_console_drive(eng, -1, (joypad_inputs_t){0});
        if (sci->occupant_pid == -1)     science_console_drive(sci, -1, (joypad_inputs_t){0});

        // ---- Movement (only for players who aren't seated) ----
        float speeds[MAX_PLAYERS] = {0};
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!players[i].present) continue;
            if (players[i].station != STATION_NONE) continue;
            speeds[i] = move_hero(players[i].hero, level,
                                  helm, weapons, eng, sci, inputs[i]);
        }

        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!players[i].present) continue;
            character_animate(players[i].hero, speeds[i]);
        }

        // ---- Proximity update (for prompt UI on free consoles) ----
        float positions[MAX_PLAYERS][2];
        bool  presence[MAX_PLAYERS];
        for (int i = 0; i < MAX_PLAYERS; i++) {
            presence[i] = players[i].present && players[i].station == STATION_NONE;
            positions[i][0] = players[i].present ? players[i].hero->position.v[0] : 0.0f;
            positions[i][1] = players[i].present ? players[i].hero->position.v[2] : 0.0f;
        }
        bridge_panel_update_proximity(helm, positions, presence, MAX_PLAYERS);
        weapons_console_update_proximity(weapons, positions, presence, MAX_PLAYERS);
        engineering_console_update_proximity(eng, positions, presence, MAX_PLAYERS);
        science_console_update_proximity(sci, positions, presence, MAX_PLAYERS);

        // ---- Camera follow ----
        if (p1_hero) {
            camera_set_target_pair(&camera,
                p1_hero->position.v[0], 5.0f, p1_hero->position.v[2],
                p2_hero ? p2_hero->position.v[0] : 0.0f,
                p2_hero ? p2_hero->position.v[2] : 0.0f,
                p2_hero != NULL);
        }
        camera_update(&camera);

        // Per-player render matrices.
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!players[i].present) continue;
            character_update_matrix(players[i].hero, frameIdx);
        }

        bool helm_held = helm->occupant_pid >= 0;
        ship_view_update(ship, frameIdx, helm_held, helm->steer, helm->impulse);

        rdpq_attach(display_get(), display_get_zbuf());
        t3d_frame_start();
        camera_attach(&camera);

        t3d_screen_clear_color(RGBA32(12, 14, 22, 0xFF));
        t3d_screen_clear_depth();

        if (p1_hero) {
            lights[LIGHT_HERO].position = (T3DVec3){{
                p1_hero->position.v[0],
                p1_hero->position.v[1] + 18.0f,
                p1_hero->position.v[2],
            }};
        }

        lighting_setup_main();
        int totalLights = lighting_apply_points(lights, NUM_POINT_LIGHTS);
        lighting_finalize(totalLights);

        stars_draw(stars, &camera.viewport);
        level_draw(level);
        bridge_panel_draw(helm);
        weapons_console_draw(weapons);
        engineering_console_draw(eng);
        science_console_draw(sci);
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!players[i].present) continue;
            character_draw(players[i].hero, frameIdx);
        }

        // ---- Floating button prompts ----
        // For each present player, decide which prompt(s) to draw above
        // their character based on station status.
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!players[i].present) continue;
            float px = players[i].hero->position.v[0];
            float py = players[i].hero->position.v[1] + PROMPT_FLOAT_Y;
            float pz = players[i].hero->position.v[2];

            switch (players[i].station) {
            case STATION_HELM:
                prompts_draw_pair(prompts, PROMPT_STICK, PROMPT_B, px, py, pz, PROMPT_PAIR_GAP);
                break;
            case STATION_WEAPONS:
                prompts_draw_pair(prompts, PROMPT_A, PROMPT_Z, px, py, pz, PROMPT_PAIR_GAP);
                break;
            case STATION_ENG:
            case STATION_SCIENCE:
                prompts_draw_pair(prompts, PROMPT_A, PROMPT_B, px, py, pz, PROMPT_PAIR_GAP);
                break;
            case STATION_NONE:
            default: {
                // Show the "press A" prompt if this specific player is
                // close to any FREE station. Inline radius check so the
                // logic is clearly per-player rather than aggregated.
                const float R2 = 35.0f * 35.0f;
                #define CLOSE_FREE(c) ((c)->occupant_pid == -1 \
                    && ((px - (c)->position.v[0]) * (px - (c)->position.v[0]) \
                      + (pz - (c)->position.v[2]) * (pz - (c)->position.v[2]) <= R2))
                if (CLOSE_FREE(helm) || CLOSE_FREE(weapons)
                    || CLOSE_FREE(eng) || CLOSE_FREE(sci)) {
                    prompts_draw(prompts, PROMPT_A, px, py, pz);
                }
                #undef CLOSE_FREE
                break;
            }
            }
        }

        // Corner-viewport "tactical view" of the ship in space. Drawn last
        // so it overlays the bridge interior.
        ship_view_draw(ship, frameIdx, &camera.viewport);

        rdpq_sync_pipe();
        rdpq_set_mode_standard();

        // ---- "In use by pX" status flash above each occupied console ----
        // Drawn in 2D as small text along the top edge — keeps the message
        // visible without doing perspective math.
        int line_x = 14;
        int line_y = 14;
        char tag[40];
        if (helm->occupant_pid >= 0) {
            snprintf(tag, sizeof tag, "HELM: P%d", helm->occupant_pid + 1);
            rdpq_text_print(NULL, 1, line_x, line_y, tag); line_y += 10;
        }
        if (weapons->occupant_pid >= 0) {
            snprintf(tag, sizeof tag, "WEAPONS: P%d", weapons->occupant_pid + 1);
            rdpq_text_print(NULL, 1, line_x, line_y, tag); line_y += 10;
        }
        if (eng->occupant_pid >= 0) {
            snprintf(tag, sizeof tag, "ENGINEERING: P%d", eng->occupant_pid + 1);
            rdpq_text_print(NULL, 1, line_x, line_y, tag); line_y += 10;
        }
        if (sci->occupant_pid >= 0) {
            snprintf(tag, sizeof tag, "SCIENCE: P%d", sci->occupant_pid + 1);
            rdpq_text_print(NULL, 1, line_x, line_y, tag); line_y += 10;
        }
        if (engineering_console_repair_active(eng)) {
            rdpq_text_print(NULL, 1, line_x, line_y, "REPAIR BUFF: ALL CREW");
            line_y += 10;
        }

        // Kill counter for the corner viewport's enemy fighters. Tucked just
        // under the corner overlay's lower edge so it reads as part of that
        // panel rather than the bridge HUD.
        char score[32];
        snprintf(score, sizeof score, "KILLS: %d", ship_view_score(ship));
        rdpq_text_print(NULL, 1, SHIP_VIEW_X, SHIP_VIEW_Y + SHIP_VIEW_HEIGHT + 12, score);

        // Bottom HUD: name the action of whichever P1 is doing (matches
        // pre-multiplayer behaviour for muscle memory).
        if (players[0].present) {
            const char *bottom = NULL;
            switch (players[0].station) {
            case STATION_HELM:    bottom = "[B] LEAVE HELM"; break;
            case STATION_WEAPONS: bottom = "[B] LEAVE GUNNER STATION"; break;
            case STATION_ENG:     bottom = "[B] LEAVE ENGINEERING"; break;
            case STATION_SCIENCE: bottom = "[B] LEAVE SCIENCE"; break;
            default: break;
            }
            if (bottom) rdpq_text_print(NULL, 1, 14, 224, bottom);
        }

        // Draw any active station HUDs (engineering bars, science rhythm).
        if (eng->occupant_pid >= 0) draw_eng_hud(1, eng);
        if (sci->occupant_pid >= 0) draw_sci_hud(1, sci);

        rdpq_detach_show();
    }

    t3d_destroy();
    return 0;
}
