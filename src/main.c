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
#include "fire.h"
#include "extinguisher.h"
#include "meshes.h"
#include "missions.h"
#include "mission_select.h"

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

// Phase-4 officer health. Tuning matches gamedesign.md. HEAL_RANGE2 is the
// squared-distance threshold for an officer to revive a downed teammate by
// mashing A — squared so the per-frame proximity check avoids a sqrt.
#define OFFICER_HP_MAX        100
#define OFFICER_HEAL_PER_PRESS 8
#define HEAL_RANGE             18.0f
#define HEAL_RANGE2            (HEAL_RANGE * HEAL_RANGE)

typedef enum {
    STATION_NONE     = -1,
    STATION_HELM     =  0,
    STATION_WEAPONS  =  1,
    STATION_ENG      =  2,
    STATION_SCIENCE  =  3,
} StationId;

typedef struct {
    Character *hero;
    bool       present;        // Phase-9: now always true post-init — every
                               // slot has a character. Used to be the "is a
                               // player" check; that's now controlling_port >= 0.
    int        station;        // StationId or STATION_NONE while walking
    int        hp;             // Phase-4: officer HP, 0 → down
    int        hp_max;
    bool       down;           // latched true when hp hits 0; cleared on heal
    bool       prev_a;         // edge-detect for the heal-A-mash interaction
    int        carrying_ext;   // -1 = empty hands, else extinguisher index

    // Phase-9 player/NPC distinction. controlling_port == -1 means this
    // slot is an NPC (auto-wandering, no input source); otherwise it's
    // the joypad port driving this body. The L/R-bumper swap moves the
    // port between slots, never destroys/creates characters.
    int        controlling_port;
    bool       prev_l;         // edge-detect for L-bumper body swap
    bool       prev_r;         // edge-detect for R-bumper body swap

    // NPC wander state. home_room_id is the LevelRoom id this NPC
    // "belongs to" — they wander randomly inside it and return after
    // any task. wander_target is the world XZ they're walking toward;
    // wander_cooldown counts down before picking a new target.
    uint8_t    home_room_id;
    float      wander_target_x;
    float      wander_target_z;
    int        wander_cooldown;
} PlayerSlot;

// Convenience: a slot is a player slot iff some port is controlling it.
#define IS_PLAYER(p) ((p).controlling_port >= 0)

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

// Phase-9: walk an NPC toward (target_x, target_z) one frame at a time.
// Same collision rules as move_hero (level walkable + console-blocks),
// just with a fixed direction toward the target. Returns the actual
// distance moved so character_animate picks the right walk pose.
static float move_npc(Character *hero, Level *level,
                      const BridgePanel *helm,
                      const WeaponsConsole *weapons,
                      const EngineeringConsole *eng,
                      const ScienceConsole *sci,
                      float target_x, float target_z,
                      float speed_scale)
{
    float hx = hero->position.v[0];
    float hz = hero->position.v[2];
    float dx = target_x - hx;
    float dz = target_z - hz;
    float dist = sqrtf(dx * dx + dz * dz);
    if (dist < 0.001f) return 0.0f;
    float step = MOVE_SPEED * speed_scale;
    if (step > dist) step = dist;
    float ndx = dx / dist * step;
    float ndz = dz / dist * step;

    float try_x = hx + ndx;
    if (level_is_walkable(level, try_x, hz)
        && !any_console_blocks(helm, weapons, eng, sci, try_x, hz)) {
        hero->position.v[0] = try_x;
    } else {
        ndx = 0.0f;
    }
    float try_z = hz + ndz;
    if (level_is_walkable(level, hero->position.v[0], try_z)
        && !any_console_blocks(helm, weapons, eng, sci, hero->position.v[0], try_z)) {
        hero->position.v[2] = try_z;
    } else {
        ndz = 0.0f;
    }

    float moved = sqrtf(ndx * ndx + ndz * ndz);
    if (moved > 0.01f) {
        character_face_direction(hero, atan2f(ndx, -ndz), TURN_SMOOTHING);
    }
    return moved;
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

    // Phase-6 repair-mode banner. When the engineer is holding Z, the
    // energy bars freeze; tell the player what they're doing.
    if (e->repairing) {
        static const char *target_label[STATION_COUNT] = {
            "HELM", "WEAPONS", "ENGINEERING", "SCIENCE"
        };
        int t = e->repair_target;
        if (t < 0 || t >= STATION_COUNT) t = 0;
        char rb[40];
        snprintf(rb, sizeof rb, "REPAIRING: %s", target_label[t]);
        rdpq_text_print(NULL, font_id, x0, y0 + bar_h + 38, rb);
        rdpq_text_print(NULL, font_id, x0, y0 + bar_h + 50,
                        "STICK L/R: target  release Z to resume");
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
    static const char *face_label[SHIELD_FACE_COUNT] = {
        "BOW", "STERN", "PORT", "STARBOARD", "DORSAL", "VENTRAL"
    };
    int sf = s->selected_face;
    if (sf < 0 || sf >= SHIELD_FACE_COUNT) sf = 0;
    char hdr[40];
    snprintf(hdr, sizeof hdr, "RHYTHM -> %s", face_label[sf]);
    rdpq_text_print(NULL, font_id, track_x0, track_y0 - 4, hdr);
    rdpq_text_print(NULL, font_id, track_x0, track_y0 + track_h + 14,
                    "STICK L/R: switch face   A: hit");

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

    // Phase-8: pick the mission *after* the lobby readies the crew. The
    // selection is reused on game-over retries; the win overlay can
    // re-enter mission_select_run to switch modes between runs.
    MissionId selected_mission = mission_select_run(1);

    // ---- Game setup ----
    Camera camera = camera_create();
    Level *level = level_load(STARTING_LEVEL_PATH);

    // Phase-9: ALWAYS create all 4 characters. Slots that the lobby's
    // ready_mask covers start under player control; the rest start as
    // NPCs that idle-wander in their assigned home rooms. The L/R
    // bumpers let any player swap their controlling_port to a different
    // body during play. ready_mask doesn't need to align with port 1 —
    // a port-3-only ready_mask leaves ports 0/1/2 as NPCs, with port 3
    // controlling slot 3.
    // Per-slot shirt tints so the four crew members can be told apart at
    // a glance — slot 0 = red, 1 = blue, 2 = yellow, 3 = green. Mirrors
    // the classic Mario-Party / Smash player-color palette.
    static const uint8_t SHIRT_COLORS[MAX_PLAYERS][3] = {
        {220,  60,  60},   // P1 red
        { 70, 120, 230},   // P2 blue
        {240, 200,  60},   // P3 yellow
        { 70, 200,  90},   // P4 green
    };

    PlayerSlot players[MAX_PLAYERS] = {0};
    for (int i = 0; i < MAX_PLAYERS; i++) {
        players[i].hero             = character_create();
        character_set_shirt_color(players[i].hero,
                                  SHIRT_COLORS[i][0],
                                  SHIRT_COLORS[i][1],
                                  SHIRT_COLORS[i][2]);
        players[i].present          = true;
        players[i].station          = STATION_NONE;
        players[i].hp               = OFFICER_HP_MAX;
        players[i].hp_max           = OFFICER_HP_MAX;
        players[i].down             = false;
        players[i].prev_a           = false;
        players[i].prev_l           = false;
        players[i].prev_r           = false;
        players[i].carrying_ext     = -1;
        players[i].controlling_port = (ready_mask & (1 << i)) ? i : -1;
        players[i].wander_cooldown  = 0;
        float spawn_x = level->has_spawn ? level->spawn_wx : 0.0f;
        float spawn_z = level->has_spawn ? level->spawn_wz : 0.0f;
        character_set_position(players[i].hero,
                               spawn_x + SPAWN_OFFSETS[i][0], 0.0f,
                               spawn_z + SPAWN_OFFSETS[i][1]);
        // home_room_id is filled in below once the consoles are
        // created — each NPC gets one of the four console rooms so
        // the bridge always has someone in every department.
        players[i].home_room_id     = LEVEL_ROOM_NONE;
        players[i].wander_target_x  = players[i].hero->position.v[0];
        players[i].wander_target_z  = players[i].hero->position.v[2];
    }

    BridgePanel       *helm    = bridge_panel_create(HELM_WORLD_X, HELM_WORLD_Z, HELM_FACING);
    WeaponsConsole    *weapons = weapons_console_create(WEAPONS_WORLD_X, WEAPONS_WORLD_Z, WEAPONS_FACING);
    EngineeringConsole *eng    = engineering_console_create(ENG_WORLD_X, ENG_WORLD_Z, ENG_FACING);
    ScienceConsole    *sci     = science_console_create(SCI_WORLD_X, SCI_WORLD_Z, SCI_FACING);
    ShipView *ship = ship_view_create();
    Stars *stars = stars_create();
    Prompts *prompts = prompts_create();

    // Phase-7: per-room fire state. Cache the room id covering each
    // station once at startup (consoles don't move). LEVEL_ROOM_NONE is
    // valid for stations placed in hallway tiles, in which case
    // fire_register_hit no-ops on that subsystem's damage events.
    fire_init(level->num_rooms);
    uint8_t station_room[STATION_COUNT] = {
        level_room_at(level, helm->position.v[0],    helm->position.v[2]),
        level_room_at(level, weapons->position.v[0], weapons->position.v[2]),
        level_room_at(level, eng->position.v[0],     eng->position.v[2]),
        level_room_at(level, sci->position.v[0],     sci->position.v[2]),
    };
    // Phase-9: distribute the four crew slots across the four console
    // rooms. Slot i's home is the room of station i — clean default
    // for the four-station bridge. Any rooms that aren't backed by a
    // station resolve to LEVEL_ROOM_NONE and the NPC will fall back
    // to wandering near their spawn position. The first-mission
    // character_set_position call will be overwritten on the first
    // wander_cooldown tick once they have a target.
    for (int i = 0; i < MAX_PLAYERS; i++) {
        players[i].home_room_id = station_room[i];
    }
    extinguishers_init_from_level(level);

    // Shared procedural mesh for extinguisher props on the floor. ~3
    // wide × 6 tall — pickable by a walking character without dwarfing
    // them. Rendered with a simple per-frame matrix per active pickup.
    T3DVertPacked *ext_mesh = mesh_create_extinguisher(2, 4);
    T3DMat4FP     *ext_matrix = malloc_uncached(sizeof(T3DMat4FP));

    // Phase-8: convert the chosen mission's roster into the ship_view
    // spawn format and seed the enemy pool. Done here, *before* the
    // game loop, so the first frame already has enemies in flight.
    {
        const MissionDef *mdef = missions_get(selected_mission);
        ShipViewSpawnEntry sv_spawns[MISSION_ROSTER_MAX];
        for (int i = 0; i < mdef->roster_count && i < MISSION_ROSTER_MAX; i++) {
            sv_spawns[i].kind  = (mdef->roster[i].type == ENEMY_CAPITAL)
                                ? ENEMY_KIND_CAPITAL
                              : (mdef->roster[i].type == ENEMY_DUMMY)
                                ? ENEMY_KIND_DUMMY
                                : ENEMY_KIND_FIGHTER;
            sv_spawns[i].count = mdef->roster[i].count;
        }
        ship_view_set_mission(ship, sv_spawns, mdef->roster_count);
    }

    PointLight lights[NUM_POINT_LIGHTS] = {
        [LIGHT_HALLWAY]   = { .position = {{   0, 35,   0}}, .color = {200, 210, 230, 255}, .size = 80.0f },
        [LIGHT_AIRLOCK_N] = { .position = {{   0, 30, -40}}, .color = {255, 190,  60, 255}, .size = 55.0f },
        [LIGHT_AIRLOCK_S] = { .position = {{   0, 30,  40}}, .color = {255, 190,  60, 255}, .size = 55.0f },
        [LIGHT_HERO]      = { .position = {{   0, 18,   0}}, .color = {255, 225, 160, 255}, .size = 40.0f },
    };

    // Phase-9: p1_hero / p2_hero are now derived per-frame inside the
    // game loop (the L/R-bumper body swap can change which slot a port
    // controls), but we still want a sane camera framing on frame zero
    // before the loop body has run. Pick the first two player-controlled
    // slots and frame them.
    Character *p1_hero = NULL;
    Character *p2_hero = NULL;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!IS_PLAYER(players[i])) continue;
        if (!p1_hero)      p1_hero = players[i].hero;
        else if (!p2_hero) { p2_hero = players[i].hero; break; }
    }
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
            // Phase-9: a slot's input comes from its controlling_port,
            // which can be any joypad port (not necessarily port == slot
            // index). NPCs (controlling_port < 0) get a zeroed inputs
            // entry, which the station/wander loops below interpret as
            // "no input".
            if (IS_PLAYER(players[i])) {
                inputs[i] = joypad_get_inputs(PADS[players[i].controlling_port]);
            }
        }

        // ---- Try-engage / drive each player ----
        // The try_engage paths handle the lock: if the console is held by a
        // different pid, the attempt no-ops. drive() returns false when the
        // active player presses B (or otherwise releases the seat) so we can
        // mark the player as walking again. Phase-4: downed officers are
        // skipped entirely — no input handling, no station ops; they're
        // unconscious until a teammate revives them via the heal A-mash.
        for (int i = 0; i < MAX_PLAYERS; i++) {
            // Phase-9: NPCs are skipped — they have no input and the
            // wander loop below drives them. NPCs at stations are
            // treated as present-but-idle (their occupant_pid keeps
            // pointing at the slot, but no station_drive runs for
            // them, so the helm doesn't steer, weapons don't fire, etc).
            if (!IS_PLAYER(players[i])) continue;
            if (players[i].down) {
                // Keep prev_a tracking stable so a downed officer's A
                // releases don't fire stale presses on revive.
                players[i].prev_a = (inputs[i].btn.a != 0);
                continue;
            }
            Character *hero = players[i].hero;

            // Phase-9 L/R body swap. Runs *before* the station switch so
            // a player who's currently driving a console can still hop
            // into another body. Cycles to the next NPC (controlling_port
            // < 0) in either direction; ports already controlled by
            // someone else are skipped so a swap never steals another
            // player's body. If the swapper was at a console, the seat
            // is left intact: the body stays put, station/occupant_pid
            // unchanged. The wander loop already skips NPCs with
            // station != STATION_NONE, so the body remains seated until
            // someone (the same player or another) swaps back into it.
            {
                bool l_now   = (inputs[i].btn.l != 0);
                bool r_now   = (inputs[i].btn.r != 0);
                bool l_press = l_now && !players[i].prev_l;
                bool r_press = r_now && !players[i].prev_r;
                players[i].prev_l = l_now;
                players[i].prev_r = r_now;
                if (l_press || r_press) {
                    int dir = r_press ? +1 : -1;
                    int target = -1;
                    for (int step = 1; step <= MAX_PLAYERS; step++) {
                        int idx = ((i + step * dir) % MAX_PLAYERS
                                   + MAX_PLAYERS) % MAX_PLAYERS;
                        if (idx == i) break;
                        if (!IS_PLAYER(players[idx])) { target = idx; break; }
                    }
                    if (target >= 0) {
                        int port = players[i].controlling_port;
                        players[i].controlling_port = -1;
                        players[target].controlling_port = port;
                        // Seed edge-detect prevs from the held button state so
                        // the new body doesn't fire a phantom press next frame.
                        players[target].prev_a = (inputs[i].btn.a != 0);
                        players[target].prev_l = l_now;
                        players[target].prev_r = r_now;
                        // Slot i is now an NPC. If it was driving a station
                        // (station != STATION_NONE, occupant_pid still set
                        // on the console), it just sits at the seat — the
                        // station drive only runs for IS_PLAYER slots, so
                        // the helm doesn't steer / weapons don't fire while
                        // unattended, but the body keeps the seat warm for
                        // the next swap-back.
                        continue;
                    }
                }
            }

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
                    // The console still ticks (animations, aim cursor)
                    // even when the subsystem is destroyed, but consumed
                    // fire-events are dropped. consume_* are still called
                    // so internal one-shot latches reset cleanly.
                    bool weapons_online =
                        ship_view_station_hp(ship, SUBSYS_WEAPONS) > 0;
                    bool fire_phaser  = weapons_console_consume_phaser(weapons);
                    bool fire_torpedo = weapons_console_consume_torpedo(weapons);
                    if (weapons_online) {
                        if (fire_phaser)
                            ship_view_fire(ship, PROJ_PHASER,  weapons->aim_yaw);
                        if (fire_torpedo)
                            ship_view_fire(ship, PROJ_TORPEDO, weapons->aim_yaw);
                    }
                }
                break;
            case STATION_ENG:
                if (!engineering_console_drive(eng, i, inputs[i])) {
                    players[i].station = STATION_NONE;
                } else {
                    // Phase-6: forward subsystem repair amounts to
                    // ship_view. Repair rate scales linearly with the
                    // engineer's own console HP — a half-broken
                    // engineering only repairs at half speed. Gated on
                    // the eng subsystem being online; a destroyed
                    // engineering can't repair anything (including
                    // itself).
                    int eng_hp = ship_view_station_hp(ship, SUBSYS_ENG);
                    int eng_max = ship_view_station_max(ship);
                    float share = (eng_max > 0) ? (float)eng_hp / (float)eng_max : 0.0f;
                    int rep = engineering_console_consume_repair(eng, share);
                    if (rep > 0 && eng_hp > 0) {
                        ship_view_repair_station(ship,
                                                  (SubsystemId)eng->repair_target,
                                                  rep);
                    }

                    // Phase-7 vent. C-down ventilates the targeted
                    // station's room, instantly clearing any fire there
                    // — but officers in that room take VENT_OFFICER_DMG
                    // (30 HP) one-shot. A destroyed engineering still
                    // consumes the latch (so it doesn't leak to the
                    // next frame) but no effect is applied.
                    bool vented = engineering_console_consume_vent(eng);
                    if (vented && eng_hp > 0) {
                        uint8_t target_room =
                            station_room[eng->repair_target];
                        if (target_room != LEVEL_ROOM_NONE) {
                            fire_extinguish(target_room);
                            const int VENT_OFFICER_DMG = 30;
                            // Phase-9: vent damages anyone in the
                            // room — players AND NPCs.
                            for (int p = 0; p < MAX_PLAYERS; p++) {
                                if (players[p].down) continue;
                                uint8_t pr = level_room_at(level,
                                    players[p].hero->position.v[0],
                                    players[p].hero->position.v[2]);
                                if (pr != target_room) continue;
                                players[p].hp -= VENT_OFFICER_DMG;
                                if (players[p].hp <= 0) {
                                    players[p].hp = 0;
                                    players[p].down = true;
                                    players[p].station = STATION_NONE;
                                }
                            }
                        }
                    }
                }
                break;
            case STATION_SCIENCE:
                if (!science_console_drive(sci, i, inputs[i])) {
                    players[i].station = STATION_NONE;
                } else {
                    // Forward queued rhythm-minigame events to the
                    // ship_view shield face the science officer has
                    // selected. Hits charge it, misses drain it. Gated
                    // on the science subsystem being online — a dead
                    // sci console can't do anything useful.
                    bool sci_online =
                        ship_view_station_hp(ship, SUBSYS_SCIENCE) > 0;
                    int hits   = science_console_consume_hit(sci);
                    int misses = science_console_consume_miss(sci);
                    if (sci_online) {
                        ShieldFace face = (ShieldFace)sci->selected_face;
                        if (hits > 0) {
                            ship_view_shield_add(ship, face,
                                                  hits * SHIELD_HIT_BONUS);
                        }
                        if (misses > 0) {
                            ship_view_shield_add(ship, face,
                                                  -misses * SHIELD_MISS_PEN);
                        }
                    }
                }
                break;
            case STATION_NONE:
            default: {
                // L/R body-swap is handled above the station switch so it
                // works at any station. The walking arm just runs heal /
                // engage / move from here.

                // Phase-4 heal interaction: if this walking officer just
                // pressed A and is in range of a downed teammate, top up
                // the teammate's HP and consume the press so the engage
                // logic below doesn't also act on it. Edge-detected via
                // PlayerSlot.prev_a.
                bool a_now   = (inputs[i].btn.a != 0);
                bool a_press = a_now && !players[i].prev_a;
                players[i].prev_a = a_now;
                if (a_press) {
                    bool consumed = false;

                    // Priority 1: heal a nearby downed teammate
                    // (player or NPC — anyone with hp == 0).
                    for (int j = 0; j < MAX_PLAYERS; j++) {
                        if (j == i) continue;
                        if (!players[j].down) continue;
                        float dx = hero->position.v[0]
                                 - players[j].hero->position.v[0];
                        float dz = hero->position.v[2]
                                 - players[j].hero->position.v[2];
                        if (dx*dx + dz*dz > HEAL_RANGE2) continue;
                        players[j].hp += OFFICER_HEAL_PER_PRESS;
                        if (players[j].hp >= players[j].hp_max) {
                            players[j].hp   = players[j].hp_max;
                            players[j].down = false;
                        }
                        inputs[i].btn.a = 0;
                        consumed = true;
                        break;
                    }

                    // Priority 2: discharge a carried extinguisher if
                    // the carrier is standing in a burning room.
                    if (!consumed && players[i].carrying_ext >= 0) {
                        uint8_t r = level_room_at(level,
                                                  hero->position.v[0],
                                                  hero->position.v[2]);
                        if (r != LEVEL_ROOM_NONE && fire_is_burning(r)) {
                            fire_extinguish(r);
                            extinguishers_discharge(players[i].carrying_ext);
                            players[i].carrying_ext = -1;
                            inputs[i].btn.a = 0;
                            consumed = true;
                        }
                    }

                    // Priority 3: pick up a nearby available extinguisher.
                    if (!consumed && players[i].carrying_ext < 0) {
                        int idx = extinguishers_try_grab(
                            hero->position.v[0], hero->position.v[2]);
                        if (idx >= 0) {
                            players[i].carrying_ext = idx;
                            inputs[i].btn.a = 0;
                            consumed = true;
                        }
                    }
                    (void)consumed;   // priority chain done; engage falls through
                }
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
            }   // close switch
        }

        // Drive station decay tick for stations with no occupant. This is
        // important for the helm so the ship coasts back to idle when no one
        // is piloting, and for engineering so the repair-pulse timer counts
        // down even when nobody is at the console.
        if (helm->occupant_pid == -1)    bridge_panel_drive(helm, -1, (joypad_inputs_t){0});
        if (eng->occupant_pid == -1)     engineering_console_drive(eng, -1, (joypad_inputs_t){0});
        if (sci->occupant_pid == -1)     science_console_drive(sci, -1, (joypad_inputs_t){0});

        // ---- Movement (only for player-driven non-seated officers) ----
        // Phase-4: downed officers don't move; their `speed` stays 0 so
        // the idle pose holds in character_animate below. Phase-9: NPCs
        // get their movement from the wander block further down — this
        // loop is just for player input → hero translation.
        float speeds[MAX_PLAYERS] = {0};
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!IS_PLAYER(players[i])) continue;
            if (players[i].down) continue;
            if (players[i].station != STATION_NONE) continue;
            speeds[i] = move_hero(players[i].hero, level,
                                  helm, weapons, eng, sci, inputs[i]);
        }

        // Phase-9: NPC idle wander. Each NPC picks a random walkable
        // cell inside their assigned home_room_id, walks toward it,
        // pauses on arrival, picks another. Skips NPCs at stations
        // (they sit there motionless until a player swaps in) and
        // downed NPCs (they're prone). The wander LCG is seeded once;
        // it doesn't need to be reproducible.
        static uint32_t wander_lcg = 0xC1F00D37u;
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (IS_PLAYER(players[i])) continue;
            if (players[i].down) continue;
            if (players[i].station != STATION_NONE) continue;

            Character *hero = players[i].hero;
            float dx = players[i].wander_target_x - hero->position.v[0];
            float dz = players[i].wander_target_z - hero->position.v[2];
            float dist2 = dx * dx + dz * dz;

            // Reached the current target (or we never had one): if
            // the cooldown is still ticking, idle in place; otherwise
            // pick a fresh target inside the home room.
            if (dist2 < 16.0f) {
                if (players[i].wander_cooldown > 0) {
                    players[i].wander_cooldown--;
                } else if (players[i].home_room_id != LEVEL_ROOM_NONE
                           && level->cell_room_id != NULL) {
                    int gw = level->grid_w, gh = level->grid_h;
                    for (int t = 0; t < 32; t++) {
                        wander_lcg = wander_lcg * 1664525u + 1013904223u;
                        int col = (wander_lcg >> 8) % gw;
                        wander_lcg = wander_lcg * 1664525u + 1013904223u;
                        int row = (wander_lcg >> 16) % gh;
                        if (level->cell_room_id[row * gw + col]
                            == players[i].home_room_id) {
                            float wx = ((float)col - (gw - 1) * 0.5f)
                                     * (float)TILE_SIZE;
                            float wz = ((float)row - (gh - 1) * 0.5f)
                                     * (float)TILE_SIZE;
                            players[i].wander_target_x = wx;
                            players[i].wander_target_z = wz;
                            break;
                        }
                    }
                    // 1–2.5 s pause once we arrive next time.
                    players[i].wander_cooldown = 60 + (int)(wander_lcg & 0x7F);
                }
                speeds[i] = 0.0f;
                continue;
            }

            // Walk toward the target at half MOVE_SPEED — NPCs stroll.
            speeds[i] = move_npc(hero, level, helm, weapons, eng, sci,
                                 players[i].wander_target_x,
                                 players[i].wander_target_z, 0.5f);
        }

        // Animate every character — players AND NPCs. character_animate
        // takes the speed (from move_hero for players, or from the
        // NPC wander step above) so it picks the right walk/idle pose.
        for (int i = 0; i < MAX_PLAYERS; i++) {
            character_animate(players[i].hero, speeds[i]);
        }

        // ---- Proximity update (for prompt UI on free consoles) ----
        // Downed officers are excluded — they're flat on the floor, not
        // walking up to anything.
        // Phase-9: only player-controlled walking-state slots are
        // counted as present for engage prompts. NPCs aren't candidates
        // — even if they wander past a console, we don't want them to
        // trigger engage UI for the player.
        float positions[MAX_PLAYERS][2];
        bool  presence[MAX_PLAYERS];
        for (int i = 0; i < MAX_PLAYERS; i++) {
            presence[i] = IS_PLAYER(players[i])
                       && !players[i].down
                       && players[i].station == STATION_NONE;
            positions[i][0] = players[i].hero->position.v[0];
            positions[i][1] = players[i].hero->position.v[2];
        }
        bridge_panel_update_proximity(helm, positions, presence, MAX_PLAYERS);
        weapons_console_update_proximity(weapons, positions, presence, MAX_PLAYERS);
        engineering_console_update_proximity(eng, positions, presence, MAX_PLAYERS);
        science_console_update_proximity(sci, positions, presence, MAX_PLAYERS);

        // ---- Camera follow ----
        // Recompute p1_hero / p2_hero each frame so the camera tracks
        // whichever bodies the players are currently driving (L/R
        // bumpers can swap port → slot during the run).
        p1_hero = NULL;
        p2_hero = NULL;
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!IS_PLAYER(players[i])) continue;
            if (!p1_hero)      p1_hero = players[i].hero;
            else if (!p2_hero) { p2_hero = players[i].hero; break; }
        }
        if (p1_hero) {
            camera_set_target_pair(&camera,
                p1_hero->position.v[0], 5.0f, p1_hero->position.v[2],
                p2_hero ? p2_hero->position.v[0] : 0.0f,
                p2_hero ? p2_hero->position.v[2] : 0.0f,
                p2_hero != NULL);
        }
        camera_update(&camera);

        // Per-character render matrices — players AND NPCs.
        for (int i = 0; i < MAX_PLAYERS; i++) {
            character_update_matrix(players[i].hero, frameIdx);
        }

        // Phase-3: pump the engineer's allocation into ship_view before the
        // tick so this frame's shield max / regen / heat dissipation /
        // damage scaling all read consistent power values. Engineering at
        // 0 HP also nukes power transfer (subsystem-down): allocations
        // freeze at their last value rather than letting the engineer
        // re-balance with the console destroyed.
        if (ship_view_station_hp(ship, SUBSYS_ENG) > 0) {
            ship_view_set_power(ship,
                                eng->energy[ENG_ENGINES],
                                eng->energy[ENG_WEAPONS],
                                eng->energy[ENG_SHIELDS]);
        }

        // Subsystem-down: helm at 0 HP locks the controls — pilot input is
        // ignored even if someone is at the seat. Same gating happens in
        // the WEAPONS arm of the station drive switch above. Phase 2 only
        // wires the two subsystems that have a concrete effect today;
        // engineering / science gating slots in during their respective
        // phases.
        bool helm_online = ship_view_station_hp(ship, SUBSYS_HELM) > 0;
        bool helm_held = (helm->occupant_pid >= 0) && helm_online;
        float steer_in   = helm_online ? helm->steer   : 0.0f;
        float impulse_in = helm_online ? helm->impulse : 0.0f;

        // Phase-4: snapshot station HPs so we can detect damage taken
        // during this frame's ship_view_update and route it to the
        // station's occupant. ship_view itself doesn't know about the
        // crew — it just decrements station HP — so the routing layer
        // lives here in main.c.
        int prev_station_hp[STATION_COUNT];
        for (int s = 0; s < STATION_COUNT; s++) {
            prev_station_hp[s] = ship_view_station_hp(ship, (SubsystemId)s);
        }

        ship_view_update(ship, frameIdx, helm_held, steer_in, impulse_in);

        // Drain damage events: any station whose HP dropped this frame
        // sends the same delta of damage to whoever was sitting at it.
        // If the occupant's HP hits zero they're marked `down` and
        // forced to leave the station so the seat shows up empty in
        // the HUD and prompts.
        const int station_occ[STATION_COUNT] = {
            helm->occupant_pid,    weapons->occupant_pid,
            eng->occupant_pid,     sci->occupant_pid,
        };
        for (int s = 0; s < STATION_COUNT; s++) {
            int delta = prev_station_hp[s]
                      - ship_view_station_hp(ship, (SubsystemId)s);
            if (delta <= 0) continue;

            // Phase-7: damage events feed the fire-ignition ring buffer
            // for the room containing this station. A station-less
            // hallway hit (LEVEL_ROOM_NONE) is dropped silently.
            if (station_room[s] != LEVEL_ROOM_NONE) {
                fire_register_hit(station_room[s]);
            }

            int pid = station_occ[s];
            if (pid < 0 || pid >= MAX_PLAYERS) continue;
            // Phase-9: NPC officers take station damage too — they
            // count as crew for both health and the all-down loss
            // condition. Skip only the down check; no IS_PLAYER gate.
            if (players[pid].down) continue;

            players[pid].hp -= delta;
            if (players[pid].hp <= 0) {
                players[pid].hp   = 0;
                players[pid].down = true;
                // Force-leave the station so the seat reads empty.
                players[pid].station = STATION_NONE;
                switch (s) {
                case STATION_HELM:    helm->occupant_pid    = -1; break;
                case STATION_WEAPONS: weapons->occupant_pid = -1; break;
                case STATION_ENG:     eng->occupant_pid     = -1; break;
                case STATION_SCIENCE: sci->occupant_pid     = -1; break;
                }
            }
        }

        // Phase-7: tick fire state, then apply burning-room damage. Done
        // once per frame, gated on game_over so a destroyed ship doesn't
        // keep cooking the corpses. Officer fire damage and station
        // fire damage both use small float accumulators so the sub-
        // 1-HP-per-frame deltas don't get truncated to zero.
        fire_tick();
        if (!ship_view_is_game_over(ship)) {
            static float fire_off_acc[MAX_PLAYERS] = {0};
            static float fire_st_acc [STATION_COUNT] = {0};
            for (int i = 0; i < MAX_PLAYERS; i++) {
                // Phase-9: fire burns NPCs and players the same.
                if (players[i].down) continue;
                uint8_t r = level_room_at(level,
                                          players[i].hero->position.v[0],
                                          players[i].hero->position.v[2]);
                if (r == LEVEL_ROOM_NONE || !fire_is_burning(r)) continue;
                fire_off_acc[i] += FIRE_OFFICER_DPS / 60.0f;
                if (fire_off_acc[i] >= 1.0f) {
                    int dmg = (int)fire_off_acc[i];
                    fire_off_acc[i] -= (float)dmg;
                    players[i].hp -= dmg;
                    if (players[i].hp <= 0) {
                        players[i].hp = 0;
                        players[i].down = true;
                        players[i].station = STATION_NONE;
                    }
                }
            }
            for (int s = 0; s < STATION_COUNT; s++) {
                if (station_room[s] == LEVEL_ROOM_NONE) continue;
                if (!fire_is_burning(station_room[s])) continue;
                fire_st_acc[s] += FIRE_STATION_DPS / 60.0f;
                if (fire_st_acc[s] >= 1.0f) {
                    int dmg = (int)fire_st_acc[s];
                    fire_st_acc[s] -= (float)dmg;
                    ship_view_damage_station(ship, (SubsystemId)s, dmg);
                }
            }
        }

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
        // Phase-9: render every character — players + NPCs both.
        for (int i = 0; i < MAX_PLAYERS; i++) {
            character_draw(players[i].hero, frameIdx);
        }

        // ---- Phase-7 extinguisher pickups ----
        // Vertex-coloured cubes drawn at floor positions, only for
        // pickups that are still available (not carried, not consumed).
        // Single shared mesh, single matrix overwritten per draw — the
        // RDP doesn't need a fresh allocation per pickup since the
        // matrix is uploaded each push.
        rdpq_sync_pipe();
        rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
        t3d_state_set_drawflags(T3D_FLAG_SHADED | T3D_FLAG_DEPTH);
        for (int e = 0; e < extinguishers_count(); e++) {
            const Extinguisher *ex = extinguishers_get(e);
            if (!ex || !ex->available || ex->carried) continue;
            t3d_mat4fp_from_srt_euler(ext_matrix,
                (float[3]){1.0f, 1.0f, 1.0f},
                (float[3]){0.0f, 0.0f, 0.0f},
                (float[3]){ex->x, 4.0f, ex->z});   // half_y=4 so it sits on floor
            t3d_matrix_push(ext_matrix);
            mesh_draw_cube(ext_mesh);
            t3d_matrix_pop(1);
        }

        // ---- Floating button prompts ----
        // For each *player-controlled* character, decide which prompt(s)
        // to draw above their head based on station status. NPCs don't
        // get prompts — they have no input source to act on them.
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!IS_PLAYER(players[i])) continue;
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

        // ---- Per-station HP bars over each console ----
        // For each station, project the console's world position
        // (raised in Y to clear the mesh) into screen space using the
        // main camera viewport, then draw a tiny 2D HP bar centered on
        // the projected point. Same projection trick stars.c uses for
        // its 2D sprite blits — main_viewport->matCamProj is fresh
        // because camera_update ran earlier this frame.
        {
            const T3DVec3 *wpos[STATION_COUNT] = {
                &helm->position, &weapons->position,
                &eng->position,  &sci->position,
            };
            int station_max = ship_view_station_max(ship);
            if (station_max <= 0) station_max = 1;
            const int bar_w = 28;
            const int bar_h = 4;
            for (int s = 0; s < STATION_COUNT; s++) {
                T3DVec3 above = *wpos[s];
                above.v[1] += 30.0f;   // raise above the console mesh
                T3DVec3 vp;
                t3d_viewport_calc_viewspace_pos(&camera.viewport, &vp, &above);
                int cx = (int)vp.v[0];
                int cy = (int)vp.v[1];
                // Skip stations that ended up far off-screen — the projection
                // sends behind-camera points to large outliers.
                if (cx < -bar_w || cx > 320 + bar_w) continue;
                if (cy < -bar_h * 6 || cy > 240 + bar_h * 6) continue;

                int hp = ship_view_station_hp(ship, (SubsystemId)s);
                int fill_w = (bar_w - 2) * hp / station_max;
                if (fill_w < 0) fill_w = 0;
                uint8_t r, g;
                if (hp * 2 >= station_max)      { r = 80;  g = 220; }
                else if (hp * 4 >= station_max) { r = 230; g = 200; }
                else                            { r = 240; g = 60;  }

                int x0 = cx - bar_w / 2;
                int y0 = cy;
                rdpq_set_mode_fill(RGBA32(20, 20, 28, 0xFF));
                rdpq_fill_rectangle(x0, y0, x0 + bar_w, y0 + bar_h);
                rdpq_set_mode_fill(RGBA32(60, 64, 80, 0xFF));
                rdpq_fill_rectangle(x0 + 1, y0 + 1,
                                    x0 + bar_w - 1, y0 + bar_h - 1);
                if (fill_w > 0) {
                    rdpq_set_mode_fill(RGBA32(r, g, 50, 0xFF));
                    rdpq_fill_rectangle(x0 + 1, y0 + 1,
                                        x0 + 1 + fill_w, y0 + bar_h - 1);
                }
            }
            rdpq_set_mode_standard();
        }

        // ---- Per-officer HP bars (above each character's head) ----
        // Same projection trick as the station bars — find the screen
        // pixel for a point above the character, draw a small fill-rect
        // bar there. Downed officers get a red "DOWN" label and an "[A]
        // REVIVE" hint if any alive teammate is standing within healing
        // range. Compact — 22×3 px so they don't crowd the bridge.
        {
            const int oh_bar_w = 22;
            const int oh_bar_h = 3;
            // Phase-9: HP bars are drawn over EVERY character — players
            // and NPCs alike — so the captain can see at a glance which
            // crewmember is hurt regardless of who's controlling them.
            for (int i = 0; i < MAX_PLAYERS; i++) {
                T3DVec3 above = {{
                    players[i].hero->position.v[0],
                    players[i].hero->position.v[1] + 36.0f,
                    players[i].hero->position.v[2],
                }};
                T3DVec3 vp;
                t3d_viewport_calc_viewspace_pos(&camera.viewport, &vp, &above);
                int cx = (int)vp.v[0];
                int cy = (int)vp.v[1];
                if (cx < -oh_bar_w || cx > 320 + oh_bar_w) continue;
                if (cy < -20 || cy > 240 + 20) continue;

                int hp     = players[i].hp;
                int hp_max = players[i].hp_max > 0 ? players[i].hp_max : 1;
                int fill_w = (oh_bar_w - 2) * hp / hp_max;
                if (fill_w < 0) fill_w = 0;
                uint8_t r, g;
                if (players[i].down)            { r = 240; g = 60;  }
                else if (hp * 2 >= hp_max)      { r = 80;  g = 220; }
                else if (hp * 4 >= hp_max)      { r = 230; g = 200; }
                else                            { r = 240; g = 60;  }

                int x0 = cx - oh_bar_w / 2;
                int y0 = cy;
                rdpq_set_mode_fill(RGBA32(20, 20, 28, 0xFF));
                rdpq_fill_rectangle(x0, y0, x0 + oh_bar_w, y0 + oh_bar_h);
                rdpq_set_mode_fill(RGBA32(60, 64, 80, 0xFF));
                rdpq_fill_rectangle(x0 + 1, y0 + 1,
                                    x0 + oh_bar_w - 1, y0 + oh_bar_h - 1);
                if (fill_w > 0 && !players[i].down) {
                    rdpq_set_mode_fill(RGBA32(r, g, 50, 0xFF));
                    rdpq_fill_rectangle(x0 + 1, y0 + 1,
                                        x0 + 1 + fill_w, y0 + oh_bar_h - 1);
                }
                rdpq_set_mode_standard();

                if (players[i].down) {
                    rdpq_text_print(NULL, 1, x0 - 2, y0 + oh_bar_h + 9, "DOWN");
                    // "[A] REVIVE" hint when any alive teammate is in
                    // healing range — gives the rescuer a clear cue.
                    bool can_revive = false;
                    for (int j = 0; j < MAX_PLAYERS; j++) {
                        if (j == i || !players[j].present) continue;
                        if (players[j].down) continue;
                        if (players[j].station != STATION_NONE) continue;
                        float dx = players[i].hero->position.v[0]
                                 - players[j].hero->position.v[0];
                        float dz = players[i].hero->position.v[2]
                                 - players[j].hero->position.v[2];
                        if (dx*dx + dz*dz <= HEAL_RANGE2) {
                            can_revive = true;
                            break;
                        }
                    }
                    if (can_revive) {
                        rdpq_text_print(NULL, 1, x0 - 8, y0 + oh_bar_h + 18,
                                        "[A] REVIVE");
                    }
                }
            }
        }

        // ---- "In use by pX" status flash above each occupied console ----
        // Drawn in 2D as small text on the left edge, below the upper-left
        // ship-stats panel (KILLS / bars / shield grid). Sit at y=120 so we
        // clear the bottom of the stats block (~y=97) with breathing room.
        int line_x = 4;
        int line_y = 120;
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

        // ---- Upper-left ship-stats panel ---------------------------------
        // Phase-9 tweak: ship stats (KILLS, hull/shield/heat bars, six-face
        // shield grid) live in the upper-left so the right side of the
        // screen stays focused on the radar/external view. The whole block
        // is anchored at HUD_X / HUD_Y; ship_view_render() draws the
        // radar at the top-right and the bottom-row hint sits at y=224.

        // Stats panel anchor (matches the bar / grid layout below).
        const int HUD_X = 4;
        const int HUD_Y = 4;

        // KILLS counter — first line of the upper-left panel.
        char score[32];
        snprintf(score, sizeof score, "KILLS: %d", ship_view_score(ship));
        rdpq_text_print(NULL, 1, HUD_X, HUD_Y + 6, score);

        // ---- Ship status bars: hull / shields / heat ----
        // Three stacked bars under the KILLS line. Each is a frame + empty
        // + fill triple of fill-rects, with the fill colour ramping
        // green → amber → red as the value drops (or for heat, the colour
        // ramps the opposite way: cool → hot → overheat). Same 2D rdpq
        // fill style used by the lobby and viewport border.
        {
            const int bar_x = HUD_X;
            const int bar_w = 120;             // matches the radar width
            const int bar_h = 7;
            int bar_y       = HUD_Y + 10;      // just under the KILLS line
            const int bar_step = bar_h + 12;   // extra space for label text below

            // -- helper: draw one labelled bar at (bar_x, bar_y) sized by
            //    `cur` of `max`, with the indicator colour and a label. --
            #define DRAW_BAR(cur_, max_, r_, g_, b_, label_, value_) do {     \
                int _max = (max_) > 0 ? (max_) : 1;                            \
                int _cur = (cur_) < 0 ? 0 : ((cur_) > _max ? _max : (cur_));   \
                int _fill = (bar_w - 2) * _cur / _max;                         \
                rdpq_set_mode_fill(RGBA32(20, 20, 28, 0xFF));                  \
                rdpq_fill_rectangle(bar_x, bar_y, bar_x + bar_w, bar_y + bar_h); \
                rdpq_set_mode_fill(RGBA32(60, 64, 80, 0xFF));                  \
                rdpq_fill_rectangle(bar_x + 1, bar_y + 1,                      \
                                    bar_x + bar_w - 1, bar_y + bar_h - 1);     \
                if (_fill > 0) {                                               \
                    rdpq_set_mode_fill(RGBA32((r_), (g_), (b_), 0xFF));        \
                    rdpq_fill_rectangle(bar_x + 1, bar_y + 1,                  \
                                        bar_x + 1 + _fill, bar_y + bar_h - 1); \
                }                                                              \
                rdpq_set_mode_standard();                                      \
                rdpq_text_printf(NULL, 1, bar_x, bar_y + bar_h + 9,            \
                                 "%s %s", (label_), (value_));                 \
                bar_y += bar_step;                                             \
            } while (0)

            // Hull
            int hp = ship_view_hull(ship), hp_max = ship_view_hull_max(ship);
            uint8_t hr, hg;
            if (hp * 2 >= hp_max)      { hr = 80;  hg = 220; }
            else if (hp * 4 >= hp_max) { hr = 230; hg = 200; }
            else                       { hr = 240; hg = 60;  }
            char hpbuf[16]; snprintf(hpbuf, sizeof hpbuf, "%d/%d", hp, hp_max);
            DRAW_BAR(hp, hp_max, hr, hg, 50, "HULL", hpbuf);

            // Shield total — sum across all six faces. Single-face HP bars
            // for each face are drawn in a separate grid below this stack,
            // so the corner status panel keeps just the summary number.
            int sh_tot = ship_view_shield_total(ship);
            int sh_cap = 0;
            for (int f = 0; f < SHIELD_FACE_COUNT; f++) {
                sh_cap += ship_view_shield_max(ship, (ShieldFace)f);
            }
            char shbuf[16]; snprintf(shbuf, sizeof shbuf, "%d/%d", sh_tot, sh_cap);
            DRAW_BAR(sh_tot, sh_cap, 80, 180, 230, "SHLD", shbuf);

            // Weapons heat — bar colour ramps the opposite way (full = bad).
            // Lockout (heat >= max) shows a dedicated red + label so the
            // gunner immediately reads "stop firing".
            float heat = ship_view_heat(ship);
            float heat_max = ship_view_heat_max(ship);
            int heat_int     = (int)heat;
            int heat_max_int = (int)heat_max;
            bool locked = ship_view_weapons_locked(ship);
            uint8_t wr, wg;
            if (locked)                     { wr = 240; wg = 50;  }
            else if (heat * 2 >= heat_max)  { wr = 230; wg = 200; }
            else                            { wr = 80;  wg = 220; }
            char heatbuf[24];
            if (locked) {
                snprintf(heatbuf, sizeof heatbuf, "OVERHEAT");
            } else {
                snprintf(heatbuf, sizeof heatbuf, "%d/%d", heat_int, heat_max_int);
            }
            DRAW_BAR(heat_int, heat_max_int, wr, wg, 50, "HEAT", heatbuf);
            #undef DRAW_BAR
        }

        // ---- Phase-5 6-face shield grid ----
        // 2 rows x 3 cols of small per-face cells under the corner viewport
        // bar stack. Each cell is a tiny fill bar tinted cyan when healthy,
        // amber when low, red when empty. The face science currently has
        // selected gets a bright outline so the science officer always
        // knows where their next hit/miss will land. Layout matches the
        // gamedesign.md table:
        //   row 0: BOW   PORT   DORS
        //   row 1: STERN STBD   VENT
        {
            // Anchor matches the bar block above: KILLS (y=4..14) + 3 bars at
            // 19px stride (y=14..71) + 4px breathing room → grid starts at 75.
            const int cx0 = HUD_X;
            const int cy0 = HUD_Y + 10 + 3 * (7 + 12) + 4;
            const int cell_w = 36;
            const int cell_h = 10;
            const int cell_gap = 2;
            // (col, row) per face index BOW/STERN/PORT/STBD/DORS/VENT.
            const int layout[SHIELD_FACE_COUNT][2] = {
                {0, 0}, {0, 1}, {1, 0}, {1, 1}, {2, 0}, {2, 1}
            };
            const char *labels[SHIELD_FACE_COUNT] = {
                "BOW", "STN", "PRT", "STB", "DRS", "VNT"
            };
            int sci_face = (sci->occupant_pid >= 0)
                         ? sci->selected_face
                         : -1;
            for (int f = 0; f < SHIELD_FACE_COUNT; f++) {
                int col = layout[f][0];
                int row = layout[f][1];
                int x = cx0 + col * (cell_w + cell_gap);
                int y = cy0 + row * (cell_h + cell_gap);
                int cur = ship_view_shield(ship, (ShieldFace)f);
                int max = ship_view_shield_max(ship, (ShieldFace)f);
                if (max <= 0) max = 1;
                int fill = (cell_w - 2) * cur / max;
                if (fill < 0) fill = 0;
                uint8_t r, g, b;
                if (cur * 2 >= max)      { r = 80;  g = 180; b = 230; }
                else if (cur * 4 >= max) { r = 230; g = 200; b = 60;  }
                else                     { r = 240; g = 60;  b = 60;  }

                // Outline color: cyan-bright if this is the science-selected
                // face, dark frame otherwise.
                bool selected = (f == sci_face);
                if (selected) {
                    rdpq_set_mode_fill(RGBA32(180, 240, 255, 0xFF));
                } else {
                    rdpq_set_mode_fill(RGBA32(20, 20, 28, 0xFF));
                }
                rdpq_fill_rectangle(x, y, x + cell_w, y + cell_h);
                rdpq_set_mode_fill(RGBA32(60, 64, 80, 0xFF));
                rdpq_fill_rectangle(x + 1, y + 1, x + cell_w - 1, y + cell_h - 1);
                if (fill > 0) {
                    rdpq_set_mode_fill(RGBA32(r, g, b, 0xFF));
                    rdpq_fill_rectangle(x + 1, y + 1,
                                        x + 1 + fill, y + cell_h - 1);
                }
                rdpq_set_mode_standard();
                rdpq_text_print(NULL, 1, x + 2, y + cell_h - 1, labels[f]);
            }
        }

        // Bottom HUD: name the action of the first player slot
        // (whichever slot the lowest-numbered controlling port is on).
        // Mostly a hint string; the new L/R bumper hint replaces this
        // when the player is walking and could swap bodies.
        int hud_slot = -1;
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (IS_PLAYER(players[i])) { hud_slot = i; break; }
        }
        if (hud_slot >= 0) {
            const char *bottom = NULL;
            switch (players[hud_slot].station) {
            case STATION_HELM:    bottom = "[B] LEAVE HELM   L/R: SWAP BODY"; break;
            case STATION_WEAPONS: bottom = "[B] LEAVE GUNNER   L/R: SWAP BODY"; break;
            case STATION_ENG:     bottom = "[B] LEAVE ENGINEERING   L/R: SWAP BODY"; break;
            case STATION_SCIENCE: bottom = "[B] LEAVE SCIENCE   L/R: SWAP BODY"; break;
            case STATION_NONE:
            default:              bottom = "L/R: SWAP BODY   A: ENGAGE/HEAL/GRAB"; break;
            }
            if (bottom) rdpq_text_print(NULL, 1, 14, 224, bottom);
        }

        // Draw any active station HUDs (engineering bars, science rhythm).
        if (eng->occupant_pid >= 0) draw_eng_hud(1, eng);
        if (sci->occupant_pid >= 0) draw_sci_hud(1, sci);

        // ---- Crew-lost game-over latch ----
        // Either game-over condition (hull destroyed OR all officers down)
        // shows the same overlay style with a different banner. crew_lost
        // is sticky once latched — even if a teammate revives a downed
        // officer, the run is already over until the START reset.
        static bool crew_lost = false;
        if (!crew_lost) {
            // Phase-9: NPCs count as crew too — losing means every
            // character on the bridge is down, players and NPCs alike.
            // present is always true post-init so the count is just
            // MAX_PLAYERS, but we keep the loop to handle future
            // dynamic-roster cases cleanly.
            int present_count = 0, down_count = 0;
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (!players[i].present) continue;
                present_count++;
                if (players[i].down) down_count++;
            }
            if (present_count > 0 && down_count == present_count) {
                crew_lost = true;
            }
        }

        // ---- Game-over overlay ----
        // Solid dark panel + red trim, banner text differs per cause.
        // P1's START press resets the run: refill hull / shields / heat,
        // respawn enemies, refill all officers, clear both latches.
        // Edge-detected on START so holding doesn't immediately re-loss.
        bool ship_dead = ship_view_is_game_over(ship);
        bool mission_won = ship_view_mission_complete(ship);
        if (ship_dead || crew_lost || mission_won) {
            const char *banner;
            uint8_t panel_r, panel_g, panel_b, trim_r, trim_g, trim_b;
            const char *prompt;
            if (mission_won) {
                banner  = "  MISSION COMPLETE";
                panel_r = 6;  panel_g = 28; panel_b = 12;
                trim_r  = 50; trim_g = 180; trim_b = 80;
                prompt  = "  PRESS START - NEXT MISSION";
            } else {
                banner  = ship_dead ? "    SHIP DESTROYED"
                                    : "  CREW INCAPACITATED";
                panel_r = 20; panel_g = 8;  panel_b = 12;
                trim_r  = 140; trim_g = 30; trim_b = 30;
                prompt  = "  PRESS START TO RETRY";
            }
            rdpq_sync_pipe();
            rdpq_set_mode_fill(RGBA32(panel_r, panel_g, panel_b, 0xFF));
            rdpq_fill_rectangle(40, 80, 280, 160);
            rdpq_set_mode_fill(RGBA32(trim_r, trim_g, trim_b, 0xFF));
            rdpq_fill_rectangle(40, 80, 280, 84);   // top accent
            rdpq_fill_rectangle(40, 156, 280, 160); // bottom accent
            rdpq_set_mode_standard();
            rdpq_text_print(NULL, 1,  92, 108, banner);
            rdpq_text_print(NULL, 1,  92, 132, prompt);

            static bool prev_start = false;
            // Phase-9: any player port can press START to retry. Walk
            // the slots and OR the start bits together so the lowest-
            // numbered port doesn't have a monopoly on the retry button.
            bool any_start = false;
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (!IS_PLAYER(players[i])) continue;
                if (inputs[i].btn.start != 0) { any_start = true; break; }
            }
            bool now_start = any_start;
            if (now_start && !prev_start) {
                if (mission_won) {
                    // Win path: bounce out to mission select, let the
                    // player pick a fresh scenario, then reseed the
                    // pool. mission_select_run blocks until A is
                    // pressed — visible "you returned to mission
                    // select" feedback for free. We commit the current
                    // frame first (so the win banner shows), let
                    // mission_select_run own the display through its
                    // own attach/detach cycles, then re-attach a fresh
                    // frame so the bottom-of-loop detach_show pairs
                    // cleanly.
                    rdpq_detach_show();
                    selected_mission = mission_select_run(1);
                    rdpq_attach(display_get(), display_get_zbuf());
                    rdpq_set_mode_fill(RGBA32(12, 14, 22, 0xFF));
                    rdpq_fill_rectangle(0, 0, 320, 240);
                    rdpq_set_mode_standard();
                }
                ship_view_reset(ship);
                fire_reset();
                extinguishers_reset();
                for (int p = 0; p < MAX_PLAYERS; p++) {
                    players[p].carrying_ext = -1;
                }
                // Phase-9: revive everyone (player + NPC) on retry.
                for (int i = 0; i < MAX_PLAYERS; i++) {
                    players[i].hp   = players[i].hp_max;
                    players[i].down = false;
                }
                crew_lost = false;
                // Reseed the enemy pool with the (possibly new) mission
                // roster so loss-retries replay the same fight, and
                // win-continues launch the new selection.
                const MissionDef *mdef = missions_get(selected_mission);
                ShipViewSpawnEntry sv_spawns[MISSION_ROSTER_MAX];
                for (int i = 0;
                     i < mdef->roster_count && i < MISSION_ROSTER_MAX; i++) {
                    sv_spawns[i].kind  = (mdef->roster[i].type == ENEMY_CAPITAL)
                                         ? ENEMY_KIND_CAPITAL
                                       : (mdef->roster[i].type == ENEMY_DUMMY)
                                         ? ENEMY_KIND_DUMMY
                                         : ENEMY_KIND_FIGHTER;
                    sv_spawns[i].count = mdef->roster[i].count;
                }
                ship_view_set_mission(ship, sv_spawns, mdef->roster_count);
            }
            prev_start = now_start;
        }

        rdpq_detach_show();
    }

    t3d_destroy();
    return 0;
}
