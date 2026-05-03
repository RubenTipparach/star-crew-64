#ifndef SHIP_VIEW_H
#define SHIP_VIEW_H

#include "game_config.h"
#include "ship_model.h"

// Pixel size + position of the corner overlay (top-right). 50% larger than
// the original 80x60 (still 4:3) — fits the 320x240 frame with a 4px inset.
#define SHIP_VIEW_WIDTH    120
#define SHIP_VIEW_HEIGHT   90
#define SHIP_VIEW_X        (320 - SHIP_VIEW_WIDTH - 4)   // small inset from the screen edge
#define SHIP_VIEW_Y        4

// Number of background star billboards scattered around the corner-viewport
// camera. Each is a cool-stuff star_*.png sprite drawn as a small quad — the
// same approach src/stars.c uses for the bridge-exterior starfield. Bumped
// to 80 to give the 120×90 viewport a properly dense field.
#define SHIP_VIEW_STAR_COUNT  80
#define SHIP_VIEW_STAR_TYPES   4

// Projectile pool: shared between player phasers (rapid yellow), player
// photon torpedoes (slow blue), and enemy bullets (red, fired by enemy AI).
// Pool is intentionally small — N64 fillrate hates many alpha quads, and the
// gameplay doesn't need swarm fire. Bumped from 8 → 16 to give enemies room
// to fire without starving the player's shots.
#define SHIP_VIEW_PROJECTILES   16
#define PHASER_LIFETIME         60       // ~1s @ 60Hz, fast & short-lived
#define PHASER_SPEED            4.0f
#define PHASER_DAMAGE           1
#define TORPEDO_LIFETIME        150      // ~2.5s, drifts further before fading
#define TORPEDO_SPEED           1.8f
#define TORPEDO_DAMAGE          3
#define ENEMY_BULLET_LIFETIME   120      // ~2s; enough to cross the engagement zone
#define ENEMY_BULLET_SPEED      2.5f
#define ENEMY_BULLET_DAMAGE     5        // per gamedesign.md tuning table

// Player ship hit sphere — used by enemy bullets to register hull damage.
// Radius chosen to match the visible ship geometry (gen-ship.py WORLD_SCALE
// = 14, so the hull half-extent is ~12 along the long axis).
#define SHIP_HIT_RADIUS         12.0f
#define SHIP_HULL_MAX           100      // gamedesign.md tuning table

// Per-station subsystem damage. When an enemy bullet hits the hull, a
// station — and via Phase 4 its occupant — takes collateral damage based on
// the angle of impact in ship-local space. See gamedesign.md "Damage
// routing" for the cardinal mapping. Indexed by SubsystemId below; keep the
// enum values in lock-step with the StationKind values in main.c so an
// occupant's StationKind can be used directly as an index.
#define STATION_HP_MAX          50       // gamedesign.md tuning table
#define STATION_COUNT           4
typedef enum {
    SUBSYS_HELM    = 0,
    SUBSYS_WEAPONS = 1,
    SUBSYS_ENG     = 2,
    SUBSYS_SCIENCE = 3,
} SubsystemId;

// Phase-3 power / shields / heat. The engineering console already exposes a
// 3-channel allocation summing to ~100 (eng->energy[ENG_*]); main.c pumps
// those values into ship_view via ship_view_set_power each frame, and
// ship_view scales gameplay off them. The "base" tuning numbers below are
// the values at the default 33% allocation; effective values scale linearly
// with the player's allocation. POWER_* indices are aligned with the
// engineering_console.h ENG_* enum so callers can index directly with an
// EngSystem value.
#define POWER_CHANNELS               3
#define POWER_ENGINES                0   // = ENG_ENGINES (maneuverability)
#define POWER_WEAPONS                1   // = ENG_WEAPONS
#define POWER_SHIELDS                2   // = ENG_SHIELDS
#define POWER_REFERENCE_PCT       33.0f  // "default" allocation per channel

// Phase-5: shields are split across six independent faces, one per
// ship-local axis half. Each face has its own HP and regenerates
// independently. Damage routes to the face whose normal matches the
// dominant component of the ship-local impact direction; whatever the
// face can't absorb bleeds through to hull + station per the Phase-2
// rules. Total capacity = SHIELD_FACE_MAX * SHIELD_FACE_COUNT (240 at
// reference allocation), but only one face is exposed to any given
// hit, so the *effective* protection is ≤ SHIELD_FACE_MAX per shot.
#define SHIELD_FACE_COUNT            6
#define SHIELD_FACE_MAX              40   // per-face max at reference alloc
#define SHIELD_REGEN_PER_SEC      1.0f    // per-face passive regen at ref alloc

// Face indices. Aligned with the gamedesign.md table — ship-local +Z is
// the bow (nose), ±X is port/starboard, ±Y is dorsal/ventral.
typedef enum {
    SHIELD_BOW       = 0,   // +Z
    SHIELD_STERN     = 1,   // -Z
    SHIELD_PORT      = 2,   // +X
    SHIELD_STARBOARD = 3,   // -X
    SHIELD_DORSAL    = 4,   // +Y
    SHIELD_VENTRAL   = 5,   // -Y
} ShieldFace;

#define HEAT_MAX                100.0f
#define HEAT_PHASER               8.0f
#define HEAT_TORPEDO             25.0f
#define HEAT_DISSIPATE_PER_SEC   15.0f

// Enemy pool. Two enemy types share the same `ShipEnemy` slot; the AI
// branches on `kind`. Fighter (Phase-1): runs the four-state machine
// (orbit → attack → fire → retreat → orbit) and respawns when killed
// in pre-Phase-8 endless mode. Capital (Phase-8): slow-drift behaviour,
// 4 weapon emplacements firing burst every CAPITAL_FIRE_INTERVAL,
// no retreat phase, no respawn — bigger HP pool, bigger collider.
#define SHIP_VIEW_ENEMIES       8        // large enough for capital + escorts
#define ENEMY_HP_MAX            3        // fighter HP
// Fighter hit-sphere. Bumped to encompass the new wedge-shaped fighter mesh
// from gen-fighter.py: body ±3 wide, wings to ±7, ~14 long. Old value (7)
// was tuned for the 5-half-extent cube and made shots feel like they passed
// straight through the silhouette — see player feedback note.
#define ENEMY_HIT_RADIUS       10.0f
#define ENEMY_EXPLODE_FRAMES    20       // brief flash before respawn
#define ENEMY_DRIFT_SPEED       0.35f    // units/frame at full vector length

// Capital ship tuning. ~10× fighter HP, ~3× cube scale, 4 weapon
// emplacements firing one bullet each when the burst timer hits zero.
#define CAPITAL_HP_MAX          30
#define CAPITAL_HIT_RADIUS      20.0f
#define CAPITAL_CUBE_HALF       15
#define CAPITAL_FIRE_INTERVAL   180      // 3 s between bursts
#define CAPITAL_DRIFT_SPEED     0.10f
#define CAPITAL_WEAPON_SLOTS    4
#define CAPITAL_ORBIT_RADIUS    120.0f   // hangs further out than fighters

typedef enum {
    ENEMY_KIND_FIGHTER = 0,
    ENEMY_KIND_CAPITAL = 1,
    // Stationary practice dummy. Same fighter mesh, but skips the AI
    // state machine entirely — sits at its spawn point, takes damage
    // normally, never fires. Used by the TARGET TEST mission.
    ENEMY_KIND_DUMMY   = 2,
} EnemyKind;

// AI tuning. Per-fighter timers are seeded with a random offset so they
// don't all attack in sync. Frame-count durations assume 60 Hz.
#define ENEMY_ORBIT_RADIUS      80.0f    // target distance during ORBIT
#define ENEMY_ORBIT_FRAMES_MIN  180      // 3 s
#define ENEMY_ORBIT_FRAMES_MAX  300      // 5 s
#define ENEMY_ATTACK_RANGE      40.0f    // close enough → switch to FIRE
#define ENEMY_ATTACK_MAX_FRAMES 240      // safety cutoff if pursuit drags
#define ENEMY_FIRE_FRAMES       30       // 0.5 s window — fires once at midpoint
#define ENEMY_RETREAT_FRAMES    600      // 10 s flee per gamedesign.md
#define ENEMY_ATTACK_SPEED      0.6f     // units/frame closing-in speed
#define ENEMY_RETREAT_SPEED     0.7f     // bit faster than attack so they get clear
#define ENEMY_ORBIT_SPEED       0.45f    // tangential speed while circling
#define ENEMY_ORBIT_SPRING      0.02f    // radial pull-toward-target-radius gain

typedef enum {
    PROJ_PHASER       = 0,
    PROJ_TORPEDO      = 1,
    PROJ_ENEMY_BULLET = 2,
} ProjectileType;

// Tiny "spark" pool used for the hit-impact FX. When a player projectile
// connects with an enemy, we burst a handful of these at the hit point;
// each ticks down over HIT_PARTICLE_LIFE frames, drifting on its seeded
// velocity, and renders as a small bright cube. Pool is fixed-size and
// scanned linearly on spawn — same pattern as the projectile pool.
#define HIT_PARTICLE_COUNT       32
#define HIT_PARTICLE_LIFE        18      // ~0.3 s at 60 Hz
#define HIT_PARTICLES_PER_BURST   6
#define HIT_PARTICLE_SPEED        0.9f
typedef struct {
    float x, y, z;
    float vx, vy, vz;
    int   life;       // > 0 = active; counts down each frame
} HitParticle;

// Enemy AI state machine. See gamedesign.md "Enemy AI" section. State
// transitions happen when ai_timer hits zero (or on state-specific
// triggers like reaching attack range).
typedef enum {
    AI_ORBIT      = 0,   // circle the player at ENEMY_ORBIT_RADIUS
    AI_ATTACK_RUN = 1,   // close in on the player, headed for FIRE
    AI_FIRE       = 2,   // brief window — spawns one bullet at midpoint
    AI_RETREAT    = 3,   // flee for ENEMY_RETREAT_FRAMES, then back to ORBIT
} EnemyAIState;

typedef struct {
    float          x, y, z;
    float          vx, vy, vz;
    int            timer;        // remaining frames; <=0 means inactive
    ProjectileType type;
} ShipProjectile;

typedef struct {
    float        x, y, z;
    float        vx, vy, vz;
    int          hp;            // > 0 = alive; 0 = exploding; counts down
    int          hp_max;        // for HUD bars (capital ships read big)
    int          explode_timer; // > 0 while flashing the death frame
    float        spin;          // accumulated yaw used for visual rotation
    EnemyAIState ai_state;
    int          ai_timer;      // frames remaining in the current state
    bool         fired_this_run;// true once FIRE has spawned its bullet
    float        orbit_phase;   // accumulator for tangential motion in ORBIT
    EnemyKind    kind;          // ENEMY_KIND_*
    bool         active;        // false for unused pool slots
    bool         no_respawn;    // mission mode: true → don't respawn on death
} ShipEnemy;

typedef struct {
    T3DViewport    viewport;       // its own viewport, sub-region of the framebuffer
    T3DVertPacked *verts;          // SHIP_NUM_TRIS * 2 packed structs
    T3DMat4FP     *matrices;       // FB_COUNT — animated each frame
    sprite_t      *texture;

    // Background star billboards. We allocate ALL star textures (white/blue/
    // yellow/red, sourced from cool-stuff's gen-textures.py). World positions
    // are kept wrapped into a box around the ship every frame for parallax;
    // each star is rendered as a 2D rdpq_sprite_blit at its projected screen
    // position (see ship_view_draw), not a 3D textured quad — same approach
    // as stars.c, and same reason: cutout transparency through the t3d 3D
    // pipeline isn't reliable on this libdragon build.
    sprite_t      *star_textures[SHIP_VIEW_STAR_TYPES];
    float         *star_positions; // 3 * SHIP_VIEW_STAR_COUNT — XYZ in world
    uint8_t       *star_tex_idx;   // SHIP_VIEW_STAR_COUNT entries, into star_textures

    // World-space ship state. While "captive" (no pilot) the ship rides the
    // baked SHIP_IDLE animation; while a pilot drives it, steer rotates yaw
    // and impulse pushes it forward.
    float          yaw;            // radians, world-space heading
    float          x, y, z;        // world-space position (drift in +X+Z plane)
    float          vel;            // forward speed, units/frame
    float          drift_z;        // procedural forward drift integrated while idle

    // Idle clip playback (used only when pilot_active is false). Frame index
    // is float so we can sample fractional times for smooth playback.
    float          anim_frame;
    bool           pilot_active;   // mirrored from BridgePanel.player_active each frame

    // Projectile pool (fired by the weapons console). Inactive entries have
    // timer <= 0; we scan linearly for a free slot on spawn.
    ShipProjectile projectiles[SHIP_VIEW_PROJECTILES];
    T3DVertPacked *proj_mesh;            // small textureless cube, shared
    T3DMat4FP     *proj_matrices;        // FB_COUNT * SHIP_VIEW_PROJECTILES

    // Enemy pool. Same draw-pool pattern as projectiles — fixed-size array
    // walked linearly each frame. Phase-8: two meshes, one per kind. The
    // fighter is a multi-tri wedge baked from assets/models/fighter.obj
    // (FIGHTER_NUM_TRIS triangles, packed 2 verts per struct + 1 padding
    // dup per tri — same packing the main ship uses); the capital uses
    // its own cube mesh. Per-enemy matrices are rebuilt from position +
    // spin each frame.
    ShipEnemy      enemies[SHIP_VIEW_ENEMIES];
    T3DVertPacked *enemy_mesh;            // fighter (FIGHTER_NUM_TRIS * 2 packs)
    T3DVertPacked *enemy_mesh_capital;    // capital (Phase-8)
    T3DMat4FP     *enemy_matrices;       // FB_COUNT * SHIP_VIEW_ENEMIES

    // Hit-impact FX pool. Bullets that connect with an enemy seed a small
    // burst here; the pool ticks each frame and renders as tiny bright
    // sparks until life hits zero. hit_particle_matrices is sized
    // FB_COUNT * HIT_PARTICLE_COUNT for the same back-buffer reasons as
    // proj_matrices — keeps the RDP from reading mid-write per-frame
    // matrix data.
    HitParticle    hit_particles[HIT_PARTICLE_COUNT];
    T3DMat4FP     *hit_particle_matrices;

    // Phase-8 mission state. mission_active locks the enemy pool to
    // the configured roster (no auto-respawn). mission_complete
    // latches when all roster enemies are dead. score still ticks for
    // legacy display.
    bool           mission_active;
    bool           mission_complete;

    // Player score: enemies destroyed since boot. Surfaced via the public
    // accessor below so main.c can paint it on the HUD without poking at
    // ShipView internals.
    int            score;

    // Phase-1 combat state. hull_hp ticks down when an enemy bullet hits
    // the ship; at zero, game_over latches true and main.c renders the
    // "ship destroyed" overlay until the player presses START to reset.
    int            hull_hp;
    int            hull_max;
    bool           game_over;

    // Phase-2 subsystem damage. Each enemy bullet hit drops both hull_hp
    // and one station's HP based on the impact angle in ship-local space.
    // Indexed by SubsystemId. station_max is the same for all four; kept
    // as a single field rather than a per-station max because the gameplay
    // doesn't need asymmetric station ceilings.
    int            station_hp[STATION_COUNT];
    int            station_max;

    // Phase-3: power allocation + heat (single shield replaced in Phase 5).
    //   power[POWER_*] — engineer's allocation in [0,100], summing ~100.
    //   heat — fills on fire (HEAT_PHASER / HEAT_TORPEDO), dissipates over
    //     time (rate scaled by power[WEAPONS]). At HEAT_MAX firing locks
    //     out until the bar drops back below the threshold.
    float          power[POWER_CHANNELS];
    float          heat;

    // Phase-5: six independent shield faces. Each has its own HP and max,
    // and regenerates on its own accumulator so fractional regen doesn't
    // get truncated. Damage routes to the face matching the dominant
    // ship-local axis component of the bullet's impact offset.
    int            shield_face_hp [SHIELD_FACE_COUNT];
    int            shield_face_max[SHIELD_FACE_COUNT];
    float          shield_face_acc[SHIELD_FACE_COUNT];   // fractional regen carry
} ShipView;

ShipView* ship_view_create(void);

// Step the ship state for the current frame and update its model matrix.
// `steer` is in [-1,+1], `impulse` is in [0,1]. When `pilot_active` is false,
// steer/impulse are ignored and the baked idle clip drives the motion.
void ship_view_update(ShipView *sv, int frameIdx,
                      bool pilot_active, float steer, float impulse);

// Spawn a projectile from the ship's nose. `aim_yaw_offset` is added to the
// ship's heading so the gunner can shoot off-axis from where the ship is
// pointed. Phaser vs. torpedo controls speed / lifetime / draw color. No-op
// if the pool is full.
void ship_view_fire(ShipView *sv, ProjectileType type, float aim_yaw_offset);

// Draw the ship + starfield into the corner of the current framebuffer. Must
// be called *after* the main scene has been rendered for this frame; this
// function temporarily swaps the active viewport / scissor.
void ship_view_draw(ShipView *sv, int frameIdx, T3DViewport *main_viewport);

// Number of enemies destroyed since the run started. main.c can render this
// as a small HUD counter so the player has feedback for combat.
int  ship_view_score(const ShipView *sv);

// Hull HP accessors for the HUD bar in main.c. cur is clamped to [0, max];
// when cur reaches 0, ship_view_is_game_over returns true.
int  ship_view_hull(const ShipView *sv);
int  ship_view_hull_max(const ShipView *sv);
bool ship_view_is_game_over(const ShipView *sv);

// Per-station HP accessors. `id` is a SubsystemId (or any of the matching
// StationId values from main.c, since the enums are aligned). HP at 0
// means the station is disabled; main.c gates input + fire on this.
int  ship_view_station_hp(const ShipView *sv, SubsystemId id);
int  ship_view_station_max(const ShipView *sv);

// Phase-6: bump a station's HP back up by `amount` (positive = repair).
// Clamped to station_max. No-op if game_over or amount <= 0. Used by
// engineering's hold-Z repair mode.
void ship_view_repair_station(ShipView *sv, SubsystemId id, int amount);

// Phase-7: apply environmental damage (fire) to a station. Floors at 0,
// no game_over latch from this path — fire only damages stations and
// crew, never the hull directly.
void ship_view_damage_station(ShipView *sv, SubsystemId id, int amount);

// Phase-3 accessors for the HUD bars + lockout gating.
// Phase-5 shield accessors. Each face is queried by index; total is the
// sum across all six faces (used for the corner status panel summary).
// ship_view_shield_add lets callers (the science console minigame) add
// or subtract HP from a single face — clamped to that face's max and
// floored at zero.
int   ship_view_shield(const ShipView *sv, ShieldFace face);
int   ship_view_shield_max(const ShipView *sv, ShieldFace face);
int   ship_view_shield_total(const ShipView *sv);
void  ship_view_shield_add(ShipView *sv, ShieldFace face, int amount);
float ship_view_heat(const ShipView *sv);
float ship_view_heat_max(const ShipView *sv);
bool  ship_view_weapons_locked(const ShipView *sv);

// Set the engineer's 3-channel power allocation. Values are percentages
// (0..100) from the engineering console's eng->energy[]. Internally
// re-normalised so callers don't have to enforce sum-100; the scaling
// math uses each channel's share of the total.
void  ship_view_set_power(ShipView *sv,
                          float engines, float weapons, float shields);

// Reset the combat run after a game-over: refill hull, clear projectiles,
// respawn enemies in a fresh shell. Called by main.c when START is pressed
// on the destroyed-ship overlay.
void ship_view_reset(ShipView *sv);

// Phase-8: configure the enemy roster from a mission spec. Spawns
// `count` enemies of each spawn entry, deactivates the rest, sets
// mission_active so killed enemies don't respawn, and clears
// mission_complete. Caller passes EnemySpawn entries directly so this
// header doesn't need to depend on missions.h.
typedef struct ShipViewSpawnEntry {
    int kind;     // EnemyKind value (cast at the call site)
    int count;
} ShipViewSpawnEntry;
void ship_view_set_mission(ShipView *sv,
                           const ShipViewSpawnEntry *spawns, int spawn_count);

// Phase-8 win-state accessor. Returns true when all roster enemies
// have been destroyed under mission_active.
bool ship_view_mission_complete(const ShipView *sv);

#endif // SHIP_VIEW_H
