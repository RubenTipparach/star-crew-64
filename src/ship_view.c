#include <math.h>
#include <stdlib.h>

#include "ship_view.h"
#include "meshes.h"  // mesh_create_laser_cube + mesh_draw_cube — shared projectile mesh
#include "fighter_model.h"  // FIGHTER_POS / FIGHTER_RGBA / FIGHTER_TRI_NORMAL — baked mesh

// Camera distance for the corner viewport. Pulled back from the previous
// (32, -52) framing so more of the surrounding space — stars, enemies,
// projectile trails — is visible alongside the ship. The ship is small
// (WORLD_SCALE 14 in gen-ship-c.py), so we keep the FOV moderate and rely
// on distance for the wider read.
#define SV_FOV_DEG  60.0f
#define SV_NEAR     5.0f
#define SV_FAR      600.0f
#define SV_CAM_OFF_Y  56.0f
#define SV_CAM_OFF_Z  -90.0f   // behind the ship (camera sits at ship.z - this)

// Pilot-driven motion tuning.
#define SHIP_TURN_RATE   0.02f   // radians per frame at full stick
#define SHIP_ACCEL       0.06f   // units/frame² added per impulse
#define SHIP_DAMP        0.985f  // friction multiplier per frame
#define SHIP_MAX_SPEED   1.5f

// Enemy spawn shell: where freshly (re)spawned fighters appear, measured from
// the ship. Min keeps them just outside knife-fight range so the player has
// time to track; max keeps them inside the corner camera's frustum so they
// actually read on screen.
#define ENEMY_SPAWN_R_MIN  60.0f
#define ENEMY_SPAWN_R_MAX 140.0f

// Star scatter — a long box ahead of and behind the camera so stars wrap as
// the ship drifts forward. Z range chosen so even at SV_FAR the far end is
// inside the frustum. Expanded along with the camera pull-back so the field
// still reaches comfortably past the new far plane.
#define STAR_BOX_X_HALF   280.0f
#define STAR_BOX_Y_HALF   140.0f
#define STAR_BOX_Z_HALF   280.0f
#define STAR_QUAD_HALF     10    // local-space half-extent of each star quad
                                 // (~20 units across; enlarged with the
                                 // camera pull-back so stars still read at
                                 // the new wider framing)

static ShipView sv_instance = {0};

static void build_verts(T3DVertPacked *out)
{
    uint32_t white = 0xFFFFFFFF;
    for (int t = 0; t < SHIP_NUM_TRIS; t++) {
        const float *n = SHIP_TRI_NORMAL[t];
        T3DVec3 nv = {{n[0], n[1], n[2]}};
        uint16_t pn = t3d_vert_pack_normal(&nv);

        int v0 = t * 3 + 0;
        int v1 = t * 3 + 1;
        int v2 = t * 3 + 2;

        int16_t u0 = UV(SHIP_UV[v0][0] * 32.0f);
        int16_t v0t = UV(SHIP_UV[v0][1] * 32.0f);
        int16_t u1 = UV(SHIP_UV[v1][0] * 32.0f);
        int16_t v1t = UV(SHIP_UV[v1][1] * 32.0f);
        int16_t u2 = UV(SHIP_UV[v2][0] * 32.0f);
        int16_t v2t = UV(SHIP_UV[v2][1] * 32.0f);

        out[t * 2 + 0] = (T3DVertPacked){
            .posA = {SHIP_POS[v0][0], SHIP_POS[v0][1], SHIP_POS[v0][2]},
            .rgbaA = white, .normA = pn, .stA = {u0, v0t},
            .posB = {SHIP_POS[v1][0], SHIP_POS[v1][1], SHIP_POS[v1][2]},
            .rgbaB = white, .normB = pn, .stB = {u1, v1t},
        };
        out[t * 2 + 1] = (T3DVertPacked){
            .posA = {SHIP_POS[v2][0], SHIP_POS[v2][1], SHIP_POS[v2][2]},
            .rgbaA = white, .normA = pn, .stA = {u2, v2t},
            .posB = {SHIP_POS[v0][0], SHIP_POS[v0][1], SHIP_POS[v0][2]},
            .rgbaB = white, .normB = pn, .stB = {u0, v0t},
        };
    }
}

// Bake the fighter mesh into the same 2-pack-per-tri layout the main ship
// uses. No texture path here — the SHADE-only combiner reads rgba directly.
// Per-tri flat normal is stamped onto all four packed slots so the lighting
// step shades whole faces uniformly (matches the look of the ship faces).
static void build_fighter_verts(T3DVertPacked *out)
{
    for (int t = 0; t < FIGHTER_NUM_TRIS; t++) {
        const float *n = FIGHTER_TRI_NORMAL[t];
        T3DVec3 nv = {{n[0], n[1], n[2]}};
        uint16_t pn = t3d_vert_pack_normal(&nv);

        int v0 = t * 3 + 0;
        int v1 = t * 3 + 1;
        int v2 = t * 3 + 2;

        out[t * 2 + 0] = (T3DVertPacked){
            .posA = {FIGHTER_POS[v0][0], FIGHTER_POS[v0][1], FIGHTER_POS[v0][2]},
            .rgbaA = FIGHTER_RGBA[v0], .normA = pn,
            .posB = {FIGHTER_POS[v1][0], FIGHTER_POS[v1][1], FIGHTER_POS[v1][2]},
            .rgbaB = FIGHTER_RGBA[v1], .normB = pn,
        };
        out[t * 2 + 1] = (T3DVertPacked){
            .posA = {FIGHTER_POS[v2][0], FIGHTER_POS[v2][1], FIGHTER_POS[v2][2]},
            .rgbaA = FIGHTER_RGBA[v2], .normA = pn,
            // Fourth slot is a padding dup of vert 0 — never indexed by the
            // draw call, only there because verts load in pairs.
            .posB = {FIGHTER_POS[v0][0], FIGHTER_POS[v0][1], FIGHTER_POS[v0][2]},
            .rgbaB = FIGHTER_RGBA[v0], .normB = pn,
        };
    }
}

// Deterministic LCG so the star pattern is reproducible across runs.
static uint32_t lcg = 0x13371337u;
static float frand01(void)
{
    lcg = lcg * 1664525u + 1013904223u;
    return (lcg >> 8) * (1.0f / 16777216.0f);
}

// Pick a fresh world position for an enemy and seed it into the ORBIT state
// with a random timer offset. Spawns on a shell around the ship so freshly-
// respawned enemies don't materialise on top of the player and aren't placed
// outside the camera's far plane. Velocity is initialised to the tangent of
// the orbit so the first frame already shows circling motion rather than a
// frozen pose snapping into movement.
static void enemy_respawn(ShipEnemy *e, float ship_x, float ship_y, float ship_z)
{
    float u1 = frand01();
    float u2 = frand01();
    float u3 = frand01();
    float theta = u1 * 6.2831853f;
    float cos_phi = 1.0f - 2.0f * u2;
    float sin_phi = sqrtf(1.0f - cos_phi * cos_phi);
    float r = ENEMY_SPAWN_R_MIN
            + (ENEMY_SPAWN_R_MAX - ENEMY_SPAWN_R_MIN) * u3;

    e->x = ship_x + r * sin_phi * cosf(theta);
    e->y = ship_y + r * cos_phi * 0.4f;   // squash vertically — stay near plane
    e->z = ship_z + r * sin_phi * sinf(theta);

    // Tangent on the XZ-plane — perpendicular to the radial vector, scaled
    // to ENEMY_ORBIT_SPEED so first-frame motion already reads as circling.
    float rdx = e->x - ship_x;
    float rdz = e->z - ship_z;
    float rlen = sqrtf(rdx * rdx + rdz * rdz);
    if (rlen > 0.001f) { rdx /= rlen; rdz /= rlen; } else { rdx = 1; rdz = 0; }
    e->vx = -rdz * ENEMY_ORBIT_SPEED;
    e->vy = 0.0f;
    e->vz =  rdx * ENEMY_ORBIT_SPEED;

    e->hp = ENEMY_HP_MAX;
    e->explode_timer = 0;
    e->spin = frand01() * 6.2831853f;

    // Seed the AI state machine. Start in ORBIT with a randomised timer so
    // the squadron doesn't all attack on the same frame — that's the trick
    // from the GameDev.net Tactical Circle thread for natural-looking wing
    // attacks.
    e->ai_state = AI_ORBIT;
    int orbit_span = ENEMY_ORBIT_FRAMES_MAX - ENEMY_ORBIT_FRAMES_MIN;
    e->ai_timer = ENEMY_ORBIT_FRAMES_MIN + (int)(frand01() * orbit_span);
    e->fired_this_run = false;
    e->orbit_phase = frand01() * 6.2831853f;
    // Phase-8 carry-over defaults: enemy_respawn is the fighter-spawn
    // entry point; capital ships are seeded by ship_view_set_mission
    // which overrides these. active=true marks the slot live.
    e->kind        = ENEMY_KIND_FIGHTER;
    e->hp_max      = ENEMY_HP_MAX;
    e->active      = true;
    e->no_respawn  = false;
}

// Place a dummy target at a fixed offset from the ship's current position.
// `idx` is the dummy's index within the mission's spawn entry — used to
// spread N dummies evenly around a ring so the player gets practice at
// every yaw. y is anchored to the ship's plane so the bullets (which fly
// at p->y = sv->y + 1.5 with vy = 0) actually pass through the targets.
// no_respawn is left as set by ship_view_set_mission — the target test
// mission marks them no-respawn so the player can clear the ring and
// trigger mission_complete to bounce back to mission select.
#define DUMMY_RING_RADIUS  60.0f
#define DUMMY_RING_COUNT    6     // distributes evenly even if mission asks for fewer
static void enemy_dummy_spawn(ShipEnemy *e, int idx,
                              float ship_x, float ship_y, float ship_z)
{
    float angle = ((float)idx / (float)DUMMY_RING_COUNT) * 6.2831853f;
    e->x = ship_x + DUMMY_RING_RADIUS * cosf(angle);
    e->y = ship_y;
    e->z = ship_z + DUMMY_RING_RADIUS * sinf(angle);
    e->vx = 0.0f;
    e->vy = 0.0f;
    e->vz = 0.0f;
    e->hp           = ENEMY_HP_MAX;
    e->hp_max       = ENEMY_HP_MAX;
    e->explode_timer = 0;
    e->spin         = 0.0f;
    e->ai_state     = AI_ORBIT;   // unused but keep a defined value
    e->ai_timer     = 0;
    e->fired_this_run = false;
    e->orbit_phase  = 0.0f;
    e->kind         = ENEMY_KIND_DUMMY;
    e->active       = true;
    e->no_respawn   = false;       // overridden by ship_view_set_mission
}

// Shared particle-burst helper. Walks the pool linearly looking for free
// slots and seeds up to `count` particles in a random spherical spread
// at (px,py,pz). Per-particle `life`, `max_life`, and `scale0` let the
// renderer pick the right size ramp regardless of which call seeded it.
static void particle_burst(ShipView *sv, float px, float py, float pz,
                           int count, int life_frames, float speed, float scale0)
{
    int spawned = 0;
    for (int i = 0; i < HIT_PARTICLE_COUNT && spawned < count; i++) {
        HitParticle *hp = &sv->hit_particles[i];
        if (hp->life > 0) continue;
        float u1 = frand01();
        float u2 = frand01();
        float theta = u1 * 6.2831853f;
        float cos_phi = 1.0f - 2.0f * u2;
        float sin_phi = sqrtf(1.0f - cos_phi * cos_phi);
        hp->x = px;
        hp->y = py;
        hp->z = pz;
        hp->vx = sin_phi * cosf(theta) * speed;
        hp->vy = cos_phi               * speed;
        hp->vz = sin_phi * sinf(theta) * speed;
        hp->life     = life_frames;
        hp->max_life = life_frames;
        hp->scale0   = scale0;
        spawned++;
    }
}

// Hit spark — small yellow puff for each projectile-on-enemy connection.
static void hit_burst_spawn(ShipView *sv, float px, float py, float pz)
{
    particle_burst(sv, px, py, pz,
                   HIT_PARTICLES_PER_BURST, HIT_PARTICLE_LIFE,
                   HIT_PARTICLE_SPEED, 0.30f);
}

// Death blast — bigger, slower fireball when an enemy reaches zero HP.
// Wider spread, longer life, and a hefty starting scale so it reads as
// "they blew up" rather than "they took another hit."
static void death_burst_spawn(ShipView *sv, float px, float py, float pz)
{
    particle_burst(sv, px, py, pz,
                   DEATH_PARTICLES_PER_BURST, DEATH_PARTICLE_LIFE,
                   DEATH_PARTICLE_SPEED, DEATH_PARTICLE_SCALE);
}

// Phase-8: spawn a capital ship for the mission roster. Larger orbit
// radius, larger HP, no retreat phase — the ai_timer is repurposed as
// the burst-fire countdown. Position seeded on a shell at
// CAPITAL_ORBIT_RADIUS so the player has a moment to react before the
// capital opens up.
static void enemy_capital_spawn(ShipEnemy *e,
                                float ship_x, float ship_y, float ship_z)
{
    float u1 = frand01();
    float u2 = frand01();
    float theta = u1 * 6.2831853f;
    float cos_phi = 1.0f - 2.0f * u2;
    float sin_phi = sqrtf(1.0f - cos_phi * cos_phi);
    float r = CAPITAL_ORBIT_RADIUS;

    e->x = ship_x + r * sin_phi * cosf(theta);
    e->y = ship_y + r * cos_phi * 0.2f;     // mostly planar
    e->z = ship_z + r * sin_phi * sinf(theta);

    // Slow tangent drift so the capital orbits lazily rather than
    // sitting still — just enough motion to feel menacing.
    float rdx = e->x - ship_x;
    float rdz = e->z - ship_z;
    float rlen = sqrtf(rdx * rdx + rdz * rdz);
    if (rlen > 0.001f) { rdx /= rlen; rdz /= rlen; } else { rdx = 1; rdz = 0; }
    e->vx = -rdz * CAPITAL_DRIFT_SPEED;
    e->vy = 0.0f;
    e->vz =  rdx * CAPITAL_DRIFT_SPEED;

    e->hp     = CAPITAL_HP_MAX;
    e->hp_max = CAPITAL_HP_MAX;
    e->explode_timer = 0;
    e->spin = 0.0f;

    // ai_state isn't used for capital (we always run the same code
    // path), but seed it so the field has a defined value.
    e->ai_state = AI_ORBIT;
    e->ai_timer = CAPITAL_FIRE_INTERVAL;   // first burst after one full interval
    e->fired_this_run = false;
    e->orbit_phase = frand01() * 6.2831853f;

    e->kind       = ENEMY_KIND_CAPITAL;
    e->active     = true;
    e->no_respawn = false;
}

ShipView* ship_view_create(void)
{
    ShipView *sv = &sv_instance;
    sv->viewport = t3d_viewport_create_buffered(FB_COUNT);
    sv->yaw  = 0.0f;
    sv->x = 0.0f; sv->y = 0.0f; sv->z = 0.0f;
    sv->vel = 0.0f;
    sv->drift_z = 0.0f;
    sv->anim_frame = 0.0f;
    sv->pilot_active = false;

    sv->texture = sprite_load("rom:/ship.sprite");

    sv->star_textures[0] = sprite_load("rom:/star_white.sprite");
    sv->star_textures[1] = sprite_load("rom:/star_blue.sprite");
    sv->star_textures[2] = sprite_load("rom:/star_yellow.sprite");
    sv->star_textures[3] = sprite_load("rom:/star_red.sprite");

    sv->verts        = malloc_uncached(sizeof(T3DVertPacked) * SHIP_NUM_TRIS * 2);
    sv->matrices     = malloc_uncached(sizeof(T3DMat4FP) * FB_COUNT);
    sv->star_positions = malloc(sizeof(float) * 3 * SHIP_VIEW_STAR_COUNT);
    sv->star_tex_idx = malloc(sizeof(uint8_t) * SHIP_VIEW_STAR_COUNT);
    sv->proj_mesh    = mesh_create_laser_cube();   // tiny vertex-coloured cube
    sv->proj_matrices = malloc_uncached(
        sizeof(T3DMat4FP) * FB_COUNT * SHIP_VIEW_PROJECTILES);
    for (int i = 0; i < SHIP_VIEW_PROJECTILES; i++) {
        sv->projectiles[i].timer = 0;   // inactive
    }

    sv->enemy_mesh         = malloc_uncached(sizeof(T3DVertPacked) * FIGHTER_NUM_TRIS * 2);
    build_fighter_verts(sv->enemy_mesh);
    sv->enemy_mesh_capital = mesh_create_capital_cube(CAPITAL_CUBE_HALF);
    sv->enemy_matrices = malloc_uncached(
        sizeof(T3DMat4FP) * FB_COUNT * SHIP_VIEW_ENEMIES);
    sv->hit_particle_matrices = malloc_uncached(
        sizeof(T3DMat4FP) * FB_COUNT * HIT_PARTICLE_COUNT);
    for (int i = 0; i < HIT_PARTICLE_COUNT; i++) {
        sv->hit_particles[i].life = 0;
    }
    sv->mission_active   = false;
    sv->mission_complete = false;
    sv->score = 0;
    sv->hull_hp   = SHIP_HULL_MAX;
    sv->hull_max  = SHIP_HULL_MAX;
    sv->game_over = false;
    sv->station_max = STATION_HP_MAX;
    for (int s = 0; s < STATION_COUNT; s++) {
        sv->station_hp[s] = STATION_HP_MAX;
    }
    // Default power allocation: even split across all three channels (the
    // engineering console will overwrite this on its first tick anyway).
    for (int p = 0; p < POWER_CHANNELS; p++) {
        sv->power[p] = POWER_REFERENCE_PCT;
    }
    // Phase-5: every face starts at SHIELD_FACE_MAX with empty regen
    // accumulators. ship_view_update will scale shield_face_max[] by
    // power[POWER_SHIELDS] each frame.
    for (int f = 0; f < SHIELD_FACE_COUNT; f++) {
        sv->shield_face_max[f] = SHIELD_FACE_MAX;
        sv->shield_face_hp [f] = SHIELD_FACE_MAX;
        sv->shield_face_acc[f] = 0.0f;
    }
    sv->heat = 0.0f;
    // Phase-8: enemy pool starts empty; ship_view_set_mission populates
    // it from the mission roster. If no mission is configured (e.g.
    // legacy startup before mission select wires in), the pool stays
    // inactive — main.c can still call ship_view_reset to populate
    // legacy fighters via the post-Phase-1 endless mode below.
    for (int i = 0; i < SHIP_VIEW_ENEMIES; i++) {
        sv->enemies[i].active        = false;
        sv->enemies[i].hp            = 0;
        sv->enemies[i].explode_timer = 0;
    }

    build_verts(sv->verts);

    // Type distribution: heavy white, smaller helping of accents (matches
    // stars.c so the two scenes look like the same galactic neighborhood).
    const int per_type[SHIP_VIEW_STAR_TYPES] = {48, 18, 10, 4};  // sums to 80
    int idx = 0;
    for (int t = 0; t < SHIP_VIEW_STAR_TYPES; t++) {
        for (int i = 0; i < per_type[t]; i++) {
            sv->star_tex_idx[idx++] = (uint8_t)t;
        }
    }
    // Seed star world positions inside a box centred on the ship. Update()
    // wraps each star modulo (2 * BOX_HALF) every frame so the field stays
    // centred on the ship as it moves — no axis is special, the ship can fly
    // forever in any direction without leaving the field.
    lcg = 0x13371337u;
    for (int i = 0; i < SHIP_VIEW_STAR_COUNT; i++) {
        float *p = &sv->star_positions[i * 3];
        p[0] = (frand01() * 2.0f - 1.0f) * STAR_BOX_X_HALF;
        p[1] = (frand01() * 2.0f - 1.0f) * STAR_BOX_Y_HALF;
        p[2] = (frand01() * 2.0f - 1.0f) * STAR_BOX_Z_HALF;
    }
    // Matrices get filled by ship_view_update on the first frame; until then
    // they may contain garbage, but update() runs before draw() so this is
    // safe.

    return sv;
}

void ship_view_update(ShipView *sv, int frameIdx,
                      bool pilot_active, float steer, float impulse)
{
    sv->pilot_active = pilot_active;

    float pitch = 0.0f, roll = 0.0f;

    // The console drives the thrusters; it does NOT teleport the ship. So
    // pilot input only writes to yaw / vel here. Damping, clamp, and
    // position integration run every frame regardless of who's at the
    // console: the ship has inertia, and leaving the helm at speed must
    // leave the ship coasting through space rather than snapping back to
    // the origin.
    // Maneuverability scales with the engines power channel: at the
    // reference allocation the ship behaves as before; pumping power into
    // engines makes turns + impulse snappier; starving engines makes the
    // ship sluggish. Linear scale relative to POWER_REFERENCE_PCT.
    float maneuver_mul = sv->power[POWER_ENGINES] / POWER_REFERENCE_PCT;
    if (pilot_active) {
        sv->yaw += steer * SHIP_TURN_RATE * maneuver_mul;
        sv->vel += impulse * SHIP_ACCEL    * maneuver_mul;
        roll = -steer * 0.25f;  // bank into turns
    }

    sv->vel *= SHIP_DAMP;
    if (sv->vel >  SHIP_MAX_SPEED) sv->vel =  SHIP_MAX_SPEED;
    if (sv->vel < -SHIP_MAX_SPEED) sv->vel = -SHIP_MAX_SPEED;

    // Move along the model's nose. Nose is at local +Z (see gen-ship.py).
    // tiny3d's t3d_mat4_from_srt_euler with row-vector convention maps
    // local +Z to world (-sin yaw, 0, cos yaw) — see t3dmath.c:241. So
    // forward in world is (-sin yaw, 0, cos yaw), NOT (sin yaw, 0, cos yaw)
    // (the right-handed-math formula). The same convention is what makes
    // CHARACTER_YAW_OFFSET = π work in character.c.
    float fx = -sinf(sv->yaw);
    float fz =  cosf(sv->yaw);
    sv->x += fx * sv->vel;
    sv->z += fz * sv->vel;

    if (pilot_active) {
        // Debug: dump heading, forward basis, velocity once a second so the
        // motion direction is provable from the log without a debugger.
        static int debug_tick = 0;
        if ((debug_tick++ % 60) == 0) {
            debugf("[ship] yaw=%.3f fwd=(%.3f,%.3f) vel=%.3f pos=(%.2f,%.2f) "
                   "steer=%.2f impulse=%.2f\n",
                   sv->yaw, fx, fz, sv->vel,
                   sv->x, sv->z, steer, impulse);
        }
    }

    // Wrap every star modulo (2 * BOX_HALF) on each axis around the ship.
    // This keeps a uniform-density cloud centered on the ship in any
    // direction it flies — no need to special-case forward motion. The wrap
    // distance (BOX_HALF) is well outside the corner-camera frustum on every
    // axis, so teleports happen off-screen and the player just sees an
    // infinite parallax field.
    //
    // Stars are now drawn as 2D sprite blits (see ship_view_draw), so we no
    // longer need to maintain a billboard matrix per star — just keep the
    // world positions wrapped into the box around the ship for parallax.
    const float TWO_X = 2.0f * STAR_BOX_X_HALF;
    const float TWO_Y = 2.0f * STAR_BOX_Y_HALF;
    const float TWO_Z = 2.0f * STAR_BOX_Z_HALF;
    for (int i = 0; i < SHIP_VIEW_STAR_COUNT; i++) {
        float *p = &sv->star_positions[i * 3];

        float dx = p[0] - sv->x;
        float dy = p[1] - sv->y;
        float dz = p[2] - sv->z;
        if (dx >  STAR_BOX_X_HALF) dx -= TWO_X;
        if (dx < -STAR_BOX_X_HALF) dx += TWO_X;
        if (dy >  STAR_BOX_Y_HALF) dy -= TWO_Y;
        if (dy < -STAR_BOX_Y_HALF) dy += TWO_Y;
        if (dz >  STAR_BOX_Z_HALF) dz -= TWO_Z;
        if (dz < -STAR_BOX_Z_HALF) dz += TWO_Z;
        p[0] = sv->x + dx;
        p[1] = sv->y + dy;
        p[2] = sv->z + dz;
    }

    t3d_mat4fp_from_srt_euler(&sv->matrices[frameIdx],
        (float[3]){1.0f, 1.0f, 1.0f},
        (float[3]){pitch, sv->yaw, roll},
        (float[3]){sv->x, sv->y, sv->z}
    );

    // ---- Phase-3 / Phase-5: shields per-face max + regen, heat dissipation
    // ----
    // Each shield face scales independently with the shields power
    // channel, so power into shields raises every face's ceiling at
    // once. Per-face passive regen is fractional (1/sec at the
    // reference allocation, so ~0.017/frame); we accumulate per face
    // and bump HP whenever the carry crosses 1. Faster, targeted regen
    // comes from the science console feeding the selected face via
    // ship_view_shield_add — that's a separate code path. Heat
    // dissipation is similarly per-frame and floored at zero. All of
    // this is gated when game_over so the destroyed ship doesn't
    // appear to be self-repairing.
    if (!sv->game_over) {
        float shield_share  = sv->power[POWER_SHIELDS] / POWER_REFERENCE_PCT;
        float weapons_share = sv->power[POWER_WEAPONS] / POWER_REFERENCE_PCT;
        for (int f = 0; f < SHIELD_FACE_COUNT; f++) {
            sv->shield_face_max[f] = (int)((float)SHIELD_FACE_MAX * shield_share);
            if (sv->shield_face_max[f] < 0) sv->shield_face_max[f] = 0;
            if (sv->shield_face_hp[f] > sv->shield_face_max[f]) {
                sv->shield_face_hp[f] = sv->shield_face_max[f];
            }
            sv->shield_face_acc[f] += SHIELD_REGEN_PER_SEC * shield_share / 60.0f;
            if (sv->shield_face_acc[f] >= 1.0f) {
                int whole = (int)sv->shield_face_acc[f];
                sv->shield_face_acc[f] -= (float)whole;
                sv->shield_face_hp[f] += whole;
                if (sv->shield_face_hp[f] > sv->shield_face_max[f]) {
                    sv->shield_face_hp[f] = sv->shield_face_max[f];
                }
            }
        }

        sv->heat -= HEAT_DISSIPATE_PER_SEC * weapons_share / 60.0f;
        if (sv->heat < 0.0f) sv->heat = 0.0f;
    }

    // Tick projectiles: advance position, expire when timer hits zero.
    for (int i = 0; i < SHIP_VIEW_PROJECTILES; i++) {
        ShipProjectile *p = &sv->projectiles[i];
        if (p->timer > 0) {
            p->x += p->vx;
            p->y += p->vy;
            p->z += p->vz;
            p->timer--;
        }
    }

    // Tick hit-impact sparks. Each particle drifts on its seeded velocity
    // and counts down to zero, then becomes free in the pool again.
    for (int i = 0; i < HIT_PARTICLE_COUNT; i++) {
        HitParticle *hp = &sv->hit_particles[i];
        if (hp->life <= 0) continue;
        hp->x += hp->vx;
        hp->y += hp->vy;
        hp->z += hp->vz;
        hp->life--;
    }

    // Tick enemies. Each runs an independent AI state machine
    // (ORBIT → ATTACK_RUN → FIRE → RETREAT → ORBIT). Steering is layered:
    // every state computes a desired velocity from a primitive (tangent,
    // pursue, flee, or radial spring) and the position is integrated from
    // that. An enemy in `explode_timer` mode is invulnerable and counts
    // down to a respawn. Game-over freezes the AI entirely so the
    // destroyed-ship overlay sits on a static scene.
    const float HIT_R2            = ENEMY_HIT_RADIUS * ENEMY_HIT_RADIUS;
    const float SHIP_HIT_R2       = SHIP_HIT_RADIUS  * SHIP_HIT_RADIUS;
    for (int i = 0; i < SHIP_VIEW_ENEMIES && !sv->game_over; i++) {
        ShipEnemy *e = &sv->enemies[i];

        if (!e->active) continue;   // unused pool slot

        if (e->explode_timer > 0) {
            e->explode_timer--;
            if (e->explode_timer == 0) {
                if (e->no_respawn) {
                    e->active = false;
                } else {
                    enemy_respawn(e, sv->x, sv->y, sv->z);
                }
            }
            continue;
        }

        // Vector from enemy → ship and its length on the XZ plane (combat
        // is essentially planar; the squash on Y in respawn keeps fighters
        // near the ship's altitude).
        float dx = sv->x - e->x;
        float dy = sv->y - e->y;
        float dz = sv->z - e->z;
        float dist2 = dx*dx + dy*dy + dz*dz;
        float dist  = sqrtf(dist2);
        float inv   = (dist > 0.001f) ? 1.0f / dist : 0.0f;
        float ndx   = dx * inv;
        float ndy   = dy * inv;
        float ndz   = dz * inv;

        // ---- AI tick branches on enemy kind. Capital ships skip the
        // four-state machine entirely: lazy drift around their orbit
        // radius + a burst-fire countdown. Fighters run the full
        // ORBIT/ATTACK_RUN/FIRE/RETREAT loop below.
        if (e->kind == ENEMY_KIND_CAPITAL) {
            // Maintain orbit radius via the same radial-spring trick as
            // the fighter's ORBIT state, but slower. Tangent is the same
            // direction as the spawn-seeded velocity (already perpendicular).
            float tdx = -ndz;
            float tdz =  ndx;
            float radial_err = dist - CAPITAL_ORBIT_RADIUS;
            float rad_gain = radial_err * 0.005f;
            e->vx = tdx * CAPITAL_DRIFT_SPEED + ndx * rad_gain;
            e->vy = ndy * 0.02f;
            e->vz = tdz * CAPITAL_DRIFT_SPEED + ndz * rad_gain;

            // Burst fire: one bullet per CAPITAL_WEAPON_SLOTS, all aimed
            // at the ship, slightly spread by emplacement angle.
            if (--e->ai_timer <= 0) {
                e->ai_timer = CAPITAL_FIRE_INTERVAL;
                int spawned = 0;
                for (int j = 0; j < SHIP_VIEW_PROJECTILES &&
                                 spawned < CAPITAL_WEAPON_SLOTS; j++) {
                    ShipProjectile *p = &sv->projectiles[j];
                    if (p->timer > 0) continue;
                    float spread = (spawned - (CAPITAL_WEAPON_SLOTS - 1) * 0.5f)
                                 * 0.08f;   // ~4.5° per slot
                    float c_s = cosf(spread), s_s = sinf(spread);
                    float aim_x =  c_s * ndx - s_s * ndz;
                    float aim_z =  s_s * ndx + c_s * ndz;
                    p->x = e->x;
                    p->y = e->y;
                    p->z = e->z;
                    p->vx = aim_x * ENEMY_BULLET_SPEED;
                    p->vy = ndy   * ENEMY_BULLET_SPEED;
                    p->vz = aim_z * ENEMY_BULLET_SPEED;
                    p->timer = ENEMY_BULLET_LIFETIME;
                    p->type  = PROJ_ENEMY_BULLET;
                    spawned++;
                }
            }

            e->x += e->vx;
            e->y += e->vy;
            e->z += e->vz;
            e->spin = atan2f(ndx, ndz);

            // Capital still takes player-projectile damage. Same
            // collision check as the fighter loop below — duplicated
            // because the loop body is broad and a shared helper would
            // be more friction than payoff at this scale.
            float weapons_dmg_mul =
                sv->power[POWER_WEAPONS] / POWER_REFERENCE_PCT;
            float CAP_HIT_R2 = CAPITAL_HIT_RADIUS * CAPITAL_HIT_RADIUS;
            for (int j = 0; j < SHIP_VIEW_PROJECTILES; j++) {
                ShipProjectile *p = &sv->projectiles[j];
                if (p->timer <= 0) continue;
                if (p->type == PROJ_ENEMY_BULLET) continue;
                // Same 3-step swept check as the fighter loop below.
                bool hit = false;
                float hx = 0, hy = 0, hz = 0;
                for (int s = 0; s < 3 && !hit; s++) {
                    float back = (s == 0) ? 1.0f : (s == 1) ? 0.5f : 0.0f;
                    float sx = p->x - p->vx * back;
                    float sy = p->y - p->vy * back;
                    float sz = p->z - p->vz * back;
                    float pdx = sx - e->x;
                    float pdy = sy - e->y;
                    float pdz = sz - e->z;
                    if (pdx*pdx + pdy*pdy + pdz*pdz <= CAP_HIT_R2) {
                        hit = true;
                        hx = sx; hy = sy; hz = sz;
                    }
                }
                if (!hit) continue;
                int base = (p->type == PROJ_TORPEDO)
                         ? TORPEDO_DAMAGE : PHASER_DAMAGE;
                int dmg  = (int)((float)base * weapons_dmg_mul + 0.5f);
                if (dmg < 1 && weapons_dmg_mul > 0.05f) dmg = 1;
                e->hp -= dmg;
                hit_burst_spawn(sv, hx, hy, hz);
                p->timer = 0;
                if (e->hp <= 0) {
                    e->explode_timer = ENEMY_EXPLODE_FRAMES;
                    death_burst_spawn(sv, e->x, e->y, e->z);
                    sv->score++;
                    break;
                }
            }
            continue;   // skip the fighter state machine + collision below
        }

        // ---- Dummy: stationary practice target. No state machine, no
        // ---- firing. Velocity is forced to zero so the position-integrate
        // ---- below is a no-op; the shared collision check still applies
        // ---- so player projectiles damage the dummy normally.
        if (e->kind == ENEMY_KIND_DUMMY) {
            e->vx = 0.0f;
            e->vy = 0.0f;
            e->vz = 0.0f;
        } else
        // ---- Fighter state machine ----
        switch (e->ai_state) {
        case AI_ORBIT: {
            // Tangent steering on the XZ plane (perpendicular to the radial
            // vector) plus a radial spring that pulls the fighter toward
            // ENEMY_ORBIT_RADIUS. Tangent direction is fixed per-fighter via
            // orbit_phase so half the squadron circles clockwise and half
            // counter-clockwise — looks less rehearsed.
            float tdx = -ndz;
            float tdz =  ndx;
            if (sinf(e->orbit_phase) < 0.0f) { tdx = -tdx; tdz = -tdz; }
            float radial_err = dist - ENEMY_ORBIT_RADIUS;
            // Positive error → too far → steer inward (along ndx,ndz).
            // Negative → too close → steer outward.
            float rad_gain = radial_err * ENEMY_ORBIT_SPRING;
            e->vx = tdx * ENEMY_ORBIT_SPEED + ndx * rad_gain;
            e->vy = ndy * 0.05f;             // gentle Y centering
            e->vz = tdz * ENEMY_ORBIT_SPEED + ndz * rad_gain;
            e->orbit_phase += 0.01f;

            if (--e->ai_timer <= 0) {
                e->ai_state = AI_ATTACK_RUN;
                e->ai_timer = ENEMY_ATTACK_MAX_FRAMES;
            }
            break;
        }
        case AI_ATTACK_RUN: {
            // Pursue: velocity points at the ship at full closing speed.
            e->vx = ndx * ENEMY_ATTACK_SPEED;
            e->vy = ndy * ENEMY_ATTACK_SPEED * 0.4f;
            e->vz = ndz * ENEMY_ATTACK_SPEED;

            // Transition into FIRE when we're inside firing range, or punt
            // out to RETREAT if the chase drags past the safety cutoff.
            if (dist <= ENEMY_ATTACK_RANGE) {
                e->ai_state = AI_FIRE;
                e->ai_timer = ENEMY_FIRE_FRAMES;
                e->fired_this_run = false;
            } else if (--e->ai_timer <= 0) {
                e->ai_state = AI_RETREAT;
                e->ai_timer = ENEMY_RETREAT_FRAMES;
            }
            break;
        }
        case AI_FIRE: {
            // Continue forward through the player at attack speed (so the
            // fighter visibly punches through, doesn't hover). Spawn one
            // bullet at roughly the midpoint of the FIRE window — gives the
            // player a fraction of a second of visual telegraph before the
            // shot leaves.
            e->vx = ndx * ENEMY_ATTACK_SPEED;
            e->vy = ndy * ENEMY_ATTACK_SPEED * 0.4f;
            e->vz = ndz * ENEMY_ATTACK_SPEED;

            if (!e->fired_this_run && e->ai_timer <= ENEMY_FIRE_FRAMES / 2) {
                // Find a free projectile slot for the enemy bullet. Aim
                // straight at the ship's *current* position; no lead, the
                // ship is slow relative to ENEMY_BULLET_SPEED.
                for (int j = 0; j < SHIP_VIEW_PROJECTILES; j++) {
                    ShipProjectile *p = &sv->projectiles[j];
                    if (p->timer > 0) continue;
                    p->x = e->x;
                    p->y = e->y;
                    p->z = e->z;
                    p->vx = ndx * ENEMY_BULLET_SPEED;
                    p->vy = ndy * ENEMY_BULLET_SPEED;
                    p->vz = ndz * ENEMY_BULLET_SPEED;
                    p->timer = ENEMY_BULLET_LIFETIME;
                    p->type = PROJ_ENEMY_BULLET;
                    e->fired_this_run = true;
                    break;
                }
            }

            if (--e->ai_timer <= 0) {
                e->ai_state = AI_RETREAT;
                e->ai_timer = ENEMY_RETREAT_FRAMES;
            }
            break;
        }
        case AI_RETREAT: {
            // Flee: velocity opposite the ship-direction at retreat speed.
            e->vx = -ndx * ENEMY_RETREAT_SPEED;
            e->vy = -ndy * ENEMY_RETREAT_SPEED * 0.3f;
            e->vz = -ndz * ENEMY_RETREAT_SPEED;
            if (--e->ai_timer <= 0) {
                e->ai_state = AI_ORBIT;
                int span = ENEMY_ORBIT_FRAMES_MAX - ENEMY_ORBIT_FRAMES_MIN;
                e->ai_timer = ENEMY_ORBIT_FRAMES_MIN + (int)(frand01() * span);
            }
            break;
        }
        }

        e->x += e->vx;
        e->y += e->vy;
        e->z += e->vz;
        // Visual yaw spin always points the cube toward the ship so it
        // reads as "aimed at me", regardless of state.
        e->spin = atan2f(ndx, ndz);

        // ---- Player-projectile collision (phasers + torpedoes hit enemy) ----
        // Only player-fired projectiles damage enemies; an enemy bullet
        // landing on its own enemy is ignored. Damage scales with the
        // weapons power channel — at 0% allocation the shot is harmless,
        // at 100% it does ~3× the base damage. Capped at 1 minimum so a
        // hit is always *something* visible.
        //
        // Swept check: phasers move 4 units/frame which is over half the
        // hit radius, so a fast shot grazing the edge can tunnel between
        // ticks. We sample three points along the bullet's last-frame
        // path (start / mid / end) so the closest point still registers
        // even if neither endpoint is inside the sphere.
        float weapons_dmg_mul = sv->power[POWER_WEAPONS] / POWER_REFERENCE_PCT;
        for (int j = 0; j < SHIP_VIEW_PROJECTILES; j++) {
            ShipProjectile *p = &sv->projectiles[j];
            if (p->timer <= 0) continue;
            if (p->type == PROJ_ENEMY_BULLET) continue;
            bool hit = false;
            float hx = 0, hy = 0, hz = 0;
            for (int s = 0; s < 3 && !hit; s++) {
                float back = (s == 0) ? 1.0f : (s == 1) ? 0.5f : 0.0f;
                float sx = p->x - p->vx * back;
                float sy = p->y - p->vy * back;
                float sz = p->z - p->vz * back;
                float pdx = sx - e->x;
                float pdy = sy - e->y;
                float pdz = sz - e->z;
                if (pdx*pdx + pdy*pdy + pdz*pdz <= HIT_R2) {
                    hit = true;
                    hx = sx; hy = sy; hz = sz;
                }
            }
            if (!hit) continue;

            int base = (p->type == PROJ_TORPEDO) ? TORPEDO_DAMAGE : PHASER_DAMAGE;
            int dmg  = (int)((float)base * weapons_dmg_mul + 0.5f);
            if (dmg < 1 && weapons_dmg_mul > 0.05f) dmg = 1;
            e->hp -= dmg;
            hit_burst_spawn(sv, hx, hy, hz);
            p->timer = 0;   // projectile is consumed on impact
            if (e->hp <= 0) {
                e->explode_timer = ENEMY_EXPLODE_FRAMES;
                death_burst_spawn(sv, e->x, e->y, e->z);
                sv->score++;
                break;       // no more projectiles can hit this corpse
            }
        }
    }

    // Phase-8: latch mission_complete when the roster is exhausted.
    // Pool slot is "exhausted" when it's both inactive (so no further
    // ticks would happen) and not mid-explosion (so the player still
    // sees the death frame). Doesn't override game_over — losing
    // beats winning if both happen on the same frame.
    if (sv->mission_active && !sv->mission_complete && !sv->game_over) {
        bool any_alive = false;
        for (int i = 0; i < SHIP_VIEW_ENEMIES; i++) {
            ShipEnemy *e = &sv->enemies[i];
            if (e->active || e->explode_timer > 0) {
                any_alive = true;
                break;
            }
        }
        if (!any_alive) sv->mission_complete = true;
    }

    // ---- Enemy-bullet vs ship hull collision (Phase 2 routing) ----
    // Walk every active enemy bullet and test its position against the
    // ship's hit sphere. On contact:
    //   1) hull takes ENEMY_BULLET_DAMAGE,
    //   2) one station takes the same amount based on the impact angle in
    //      ship-local space (computed by rotating the world-space hit offset
    //      by -yaw),
    //   3) consume the bullet and latch game_over if hull hits zero.
    //
    // Local-space angle mapping (gamedesign.md "Damage routing"):
    //   front cone (|atan2(local_x, local_z)| <= 45°) → SUBSYS_WEAPONS
    //   port  side (atan2 > +45°)                     → SUBSYS_HELM
    //   starboard  (atan2 < -45°)                     → SUBSYS_ENG
    //   aft cone   (|atan2| >= 135°)                  → SUBSYS_SCIENCE
    // The aft check has to come BEFORE the side checks since |atan2| > 135°
    // is also > 45° — order matters.
    if (!sv->game_over) {
        const float QUARTER_PI    =  0.78539816f;   // 45°
        const float THREE_QPI     =  2.35619449f;   // 135°
        float cy = cosf(sv->yaw);
        float sy = sinf(sv->yaw);

        for (int j = 0; j < SHIP_VIEW_PROJECTILES; j++) {
            ShipProjectile *p = &sv->projectiles[j];
            if (p->timer <= 0) continue;
            if (p->type != PROJ_ENEMY_BULLET) continue;
            float bdx = p->x - sv->x;
            float bdy = p->y - sv->y;
            float bdz = p->z - sv->z;
            if (bdx*bdx + bdy*bdy + bdz*bdz > SHIP_HIT_R2) continue;

            // Inverse rotation: ship_yaw rotates +Z to (-sin yaw, 0, cos yaw)
            // (matches the convention used by ship_view_update + fire). The
            // inverse rotation maps the world hit offset back into
            // ship-local axes so we can classify which face/subsystem was
            // struck. Y is unchanged by yaw rotation, so bdy is already
            // in ship-local space.
            float lx =  cy * bdx - sy * bdz;
            float ly =  bdy;
            float lz =  sy * bdx + cy * bdz;

            // Phase-2 station routing: the 4-cardinal angle map on the
            // XZ-plane decides which station takes collateral damage.
            float ang = atan2f(lx, lz);   // 0 = nose, +π/2 = port, -π/2 = stbd
            float aabs = fabsf(ang);
            SubsystemId hit_subsys;
            if (aabs <= QUARTER_PI)        hit_subsys = SUBSYS_WEAPONS;
            else if (aabs >= THREE_QPI)    hit_subsys = SUBSYS_SCIENCE;
            else if (ang > 0.0f)           hit_subsys = SUBSYS_HELM;
            else                           hit_subsys = SUBSYS_ENG;

            // Phase-5 face routing: pick the shield face whose normal
            // matches the dominant axis of the local-space impact offset.
            // Independent of the station map above — a ventral hit can
            // still route station damage to e.g. helm.
            float ax = fabsf(lx);
            float ay = fabsf(ly);
            float az = fabsf(lz);
            ShieldFace face;
            if (az >= ax && az >= ay)  face = (lz > 0.0f) ? SHIELD_BOW    : SHIELD_STERN;
            else if (ax >= ay)         face = (lx > 0.0f) ? SHIELD_PORT   : SHIELD_STARBOARD;
            else                       face = (ly > 0.0f) ? SHIELD_DORSAL : SHIELD_VENTRAL;

            // Shields absorb damage before hull/station. The hit face is
            // the only buffer that protects against this shot — overflow
            // bleeds through even if other faces are full.
            int incoming = ENEMY_BULLET_DAMAGE;
            int overflow = 0;
            if (sv->shield_face_hp[face] >= incoming) {
                sv->shield_face_hp[face] -= incoming;
            } else {
                overflow = incoming - sv->shield_face_hp[face];
                sv->shield_face_hp[face] = 0;
            }
            if (overflow > 0) {
                sv->hull_hp -= overflow;
                sv->station_hp[hit_subsys] -= overflow;
                if (sv->station_hp[hit_subsys] < 0) sv->station_hp[hit_subsys] = 0;
            }
            p->timer = 0;
            if (sv->hull_hp <= 0) {
                sv->hull_hp = 0;
                sv->game_over = true;
            }
        }
    }
}

void ship_view_fire(ShipView *sv, ProjectileType type, float aim_yaw_offset)
{
    // Heat lockout: the cannon refuses to fire while heat is at or above
    // HEAT_MAX. The bar has to dissipate (rate scaled by weapons alloc)
    // back below the threshold before fire is re-enabled. Caller doesn't
    // need to check ship_view_weapons_locked first — the no-op here is
    // safe.
    if (sv->heat >= HEAT_MAX) return;
    if (sv->game_over) return;

    // Find a free slot.
    int slot = -1;
    for (int i = 0; i < SHIP_VIEW_PROJECTILES; i++) {
        if (sv->projectiles[i].timer <= 0) { slot = i; break; }
    }
    if (slot < 0) return;

    // Add heat for this shot. Phasers are cheap-and-frequent; torpedoes
    // are heavy. If the shot tips us over HEAT_MAX, lockout takes effect
    // *next* call — we don't refund this one.
    sv->heat += (type == PROJ_TORPEDO) ? HEAT_TORPEDO : HEAT_PHASER;
    if (sv->heat > HEAT_MAX) sv->heat = HEAT_MAX;

    // Direction = ship heading + gunner's aim offset. Nose is at local +Z,
    // and tiny3d's Y-rotation maps local +Z to world (-sin yaw, 0, cos yaw)
    // (see ship_view_update for the same derivation). Use the same forward
    // basis here so off-axis fire actually leaves the nose along the visible
    // heading instead of mirroring across the X axis.
    float yaw = sv->yaw + aim_yaw_offset;
    float fx = -sinf(yaw);
    float fz =  cosf(yaw);
    // Spawn a short distance ahead of the hull along the SHIP heading (not
    // the aim direction) so torpedoes/phasers always emerge from the nose.
    const float NOSE_OFFSET = 12.0f;
    float nx = -sinf(sv->yaw);
    float nz =  cosf(sv->yaw);

    float speed    = (type == PROJ_TORPEDO) ? TORPEDO_SPEED    : PHASER_SPEED;
    int   lifetime = (type == PROJ_TORPEDO) ? TORPEDO_LIFETIME : PHASER_LIFETIME;

    ShipProjectile *p = &sv->projectiles[slot];
    p->x = sv->x + nx * NOSE_OFFSET;
    p->y = sv->y + 1.5f;
    p->z = sv->z + nz * NOSE_OFFSET;
    p->vx = fx * speed;
    p->vy = 0.0f;
    p->vz = fz * speed;
    p->timer = lifetime;
    p->type = type;
}

void ship_view_draw(ShipView *sv, int frameIdx, T3DViewport *main_viewport)
{
    // World-fixed chase camera: follows the ship's POSITION but not its yaw.
    // When the player turns, the ship visibly rotates against the static
    // camera (and the surrounding starfield) instead of the camera spinning
    // with the hull and erasing the visual feedback.
    T3DVec3 cam_pos = {{
        sv->x,
        sv->y + SV_CAM_OFF_Y,
        sv->z + SV_CAM_OFF_Z,    // SV_CAM_OFF_Z is negative — camera trails in world -Z
    }};
    T3DVec3 cam_tgt = {{sv->x, sv->y, sv->z}};
    T3DVec3 cam_up  = {{0, 1, 0}};

    // Confine drawing to the corner rect. tiny3d's set_area also re-derives
    // the projection matrix's aspect ratio off the sub-region.
    t3d_viewport_set_area(&sv->viewport, SHIP_VIEW_X, SHIP_VIEW_Y,
                          SHIP_VIEW_WIDTH, SHIP_VIEW_HEIGHT);
    t3d_viewport_set_projection(&sv->viewport,
        T3D_DEG_TO_RAD(SV_FOV_DEG), SV_NEAR, SV_FAR);
    t3d_viewport_look_at(&sv->viewport, &cam_pos, &cam_tgt, &cam_up);
    t3d_viewport_attach(&sv->viewport);

    // Constrain the rdpq scissor so the inner clear + draws don't bleed
    // over the main scene that we already rendered.
    rdpq_set_scissor(SHIP_VIEW_X, SHIP_VIEW_Y,
                     SHIP_VIEW_X + SHIP_VIEW_WIDTH,
                     SHIP_VIEW_Y + SHIP_VIEW_HEIGHT);

    // Solid space-black fill + cleared depth, isolated to the sub-rect.
    t3d_screen_clear_color(RGBA32(8, 10, 22, 0xFF));
    t3d_screen_clear_depth();

    // Lighting: a single directional star + cool ambient, distinct from the
    // bridge interior's lighting so the corner reads as a separate scene.
    uint8_t amb[4]   = {0x18, 0x1A, 0x28, 0xFF};
    uint8_t key[4]   = {0xC0, 0xC8, 0xE0, 0xFF};
    T3DVec3 keyDir   = {{-0.4f, -0.7f, 0.6f}};
    t3d_vec3_norm(&keyDir);
    t3d_light_set_ambient(amb);
    t3d_light_set_directional(0, key, &keyDir);
    t3d_light_set_count(1);

    // ---- Background stars: 2D rdpq_sprite_blit, not 3D textured quads.
    // Same reasoning as src/stars.c — the t3d 3D pipeline + cutout sprites +
    // alpha-compare combo isn't reliably transparent on this libdragon
    // build, so we project each star's world position to the corner-
    // viewport's screen space and use the libdragon 2D sprite path that
    // prompts/HUD already use. The rdpq_set_scissor above clips blits to
    // the corner rect, so a star projected slightly outside the viewport
    // simply won't draw any of its 8×8 footprint where it shouldn't.
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_TEX);
    rdpq_mode_alphacompare(1);
    rdpq_set_prim_color(RGBA32(255, 255, 255, 255));

    int sw = sv->viewport.size[0];
    int sh = sv->viewport.size[1];
    int ox = sv->viewport.offset[0];
    int oy = sv->viewport.offset[1];

    for (int i = 0; i < SHIP_VIEW_STAR_COUNT; i++) {
        T3DVec3 wp = {{
            sv->star_positions[i * 3 + 0],
            sv->star_positions[i * 3 + 1],
            sv->star_positions[i * 3 + 2],
        }};
        T3DVec4 clip;
        t3d_mat4_mul_vec3(&clip, &sv->viewport.matCamProj, &wp);
        if (clip.v[3] <= 0.001f) continue;

        float ndc_x = clip.v[0] / clip.v[3];
        float ndc_y = clip.v[1] / clip.v[3];
        if (ndc_x < -1.0f || ndc_x > 1.0f) continue;
        if (ndc_y < -1.0f || ndc_y > 1.0f) continue;

        int sx = (int)((ndc_x * 0.5f + 0.5f) * sw) + ox - 4;
        int sy = (int)((-ndc_y * 0.5f + 0.5f) * sh) + oy - 4;

        rdpq_sprite_blit(sv->star_textures[sv->star_tex_idx[i]], sx, sy, NULL);
    }

    // The 2D path clobbers t3d's RDP mode (cycle, blender, persp, depth, AA).
    // Restore it before drawing the lit ship + enemies.
    t3d_frame_start();

    // ---- Ship: lit + textured.
    rdpq_sync_pipe();
    rdpq_sync_tile();
    rdpq_sprite_upload(TILE0, sv->texture, NULL);
    rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
    rdpq_mode_filter(FILTER_BILINEAR);
    t3d_state_set_drawflags(T3D_FLAG_TEXTURED | T3D_FLAG_SHADED | T3D_FLAG_DEPTH);

    t3d_matrix_push(&sv->matrices[frameIdx]);
    // Chunk loads so we stay under tiny3d's internal vertex buffer cap. Each
    // tri contributes 2 packed structs = 4 individual verts (slot 3 is a
    // duplicate of slot 0 — never indexed, just padding for the pair).
    const int TRIS_PER_LOAD = 6;  // 6 tris × 4 verts = 24 verts per load
    for (int tri = 0; tri < SHIP_NUM_TRIS; tri += TRIS_PER_LOAD) {
        int chunk = SHIP_NUM_TRIS - tri;
        if (chunk > TRIS_PER_LOAD) chunk = TRIS_PER_LOAD;
        t3d_vert_load(sv->verts + tri * 2, 0, chunk * 4);
        for (int i = 0; i < chunk; i++) {
            int base = i * 4;
            t3d_tri_draw(base + 0, base + 1, base + 2);
        }
        t3d_tri_sync();
    }
    t3d_matrix_pop(1);

    // ---- Enemies: lit + shaded. Fighters use the multi-tri wedge baked
    // from fighter_model.h; capitals still use the legacy cube. Both
    // share the same SHADE combiner — no texture, vertex colours
    // modulated by the directional light, same lighting setup as the
    // main ship just minus the texture lookup. Drawn before projectiles
    // so the projectile combiner swap below doesn't fight this state.
    rdpq_sync_pipe();
    rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
    t3d_state_set_drawflags(T3D_FLAG_SHADED | T3D_FLAG_DEPTH);
    for (int i = 0; i < SHIP_VIEW_ENEMIES; i++) {
        ShipEnemy *e = &sv->enemies[i];
        if (!e->active && e->explode_timer == 0) continue;

        // While exploding: shrink the cube and tint white-hot via a quick
        // combiner swap. Past explode_timer == 0 the enemy will respawn on
        // the next update tick.
        float scale = 1.0f;
        if (e->explode_timer > 0) {
            float t = (float)e->explode_timer / (float)ENEMY_EXPLODE_FRAMES;
            scale = 0.4f + 0.6f * t;     // collapses inward as t → 0
        }

        int matIdx = frameIdx * SHIP_VIEW_ENEMIES + i;
        t3d_mat4fp_from_srt_euler(&sv->enemy_matrices[matIdx],
            (float[3]){scale, scale, scale},
            (float[3]){0.0f, e->spin, 0.0f},
            (float[3]){e->x, e->y, e->z}
        );
        t3d_matrix_push(&sv->enemy_matrices[matIdx]);
        if (e->kind == ENEMY_KIND_CAPITAL) {
            mesh_draw_cube(sv->enemy_mesh_capital);
        } else {
            // Fighter: chunked tri-list draw, same pattern + same chunk
            // size as the main ship body in the block above.
            const int TRIS_PER_LOAD = 6;
            for (int tri = 0; tri < FIGHTER_NUM_TRIS; tri += TRIS_PER_LOAD) {
                int chunk = FIGHTER_NUM_TRIS - tri;
                if (chunk > TRIS_PER_LOAD) chunk = TRIS_PER_LOAD;
                t3d_vert_load(sv->enemy_mesh + tri * 2, 0, chunk * 4);
                for (int k = 0; k < chunk; k++) {
                    int base = k * 4;
                    t3d_tri_draw(base + 0, base + 1, base + 2);
                }
                t3d_tri_sync();
            }
        }
        t3d_matrix_pop(1);
    }

    // ---- Projectiles: tiny unlit cubes, depth-tested. Colour + scale per
    // type so phasers (yellow, small, fast) are visually distinct from
    // photon torpedoes (cyan, larger, slow).
    rdpq_sync_pipe();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    t3d_state_set_drawflags(T3D_FLAG_DEPTH);
    for (int i = 0; i < SHIP_VIEW_PROJECTILES; i++) {
        ShipProjectile *p = &sv->projectiles[i];
        if (p->timer <= 0) continue;

        if (p->type == PROJ_TORPEDO) {
            rdpq_set_prim_color(RGBA32(120, 220, 255, 255));   // bright cyan
        } else if (p->type == PROJ_ENEMY_BULLET) {
            rdpq_set_prim_color(RGBA32(255,  80,  80, 255));   // hostile red
        } else {
            rdpq_set_prim_color(RGBA32(255, 230, 100, 255));   // hot yellow
        }
        // Player projectiles shrunk to ~1/4 of the original visual; the old
        // sizes read as solid bricks against the small radar inset and
        // obscured the enemies behind them. Enemy bullets stay closer to
        // their original size so they're still readable as a threat.
        float scale = (p->type == PROJ_TORPEDO)      ? 0.42f
                    : (p->type == PROJ_ENEMY_BULLET) ? 0.30f
                    : 0.25f;

        int matIdx = frameIdx * SHIP_VIEW_PROJECTILES + i;
        t3d_mat4fp_from_srt_euler(&sv->proj_matrices[matIdx],
            (float[3]){scale, scale, scale},
            (float[3]){0.0f, 0.0f, 0.0f},
            (float[3]){p->x, p->y, p->z}
        );
        t3d_matrix_push(&sv->proj_matrices[matIdx]);
        mesh_draw_cube(sv->proj_mesh);
        t3d_matrix_pop(1);
    }

    // ---- Hit-impact sparks + death-blast fireballs share the same pool.
    // Per-particle max_life + scale0 let big death particles fade over a
    // longer ramp than small hit sparks even though both run through this
    // single render pass. Same FLAT combiner the projectiles use.
    for (int i = 0; i < HIT_PARTICLE_COUNT; i++) {
        HitParticle *hp = &sv->hit_particles[i];
        if (hp->life <= 0) continue;
        int max_life = hp->max_life > 0 ? hp->max_life : HIT_PARTICLE_LIFE;
        float t = (float)hp->life / (float)max_life;
        // White-hot at spawn, fading through yellow, orange, deep red.
        // Bigger particles (death burst) read as fire because their longer
        // life keeps them in the orange band for more frames.
        uint8_t r = 255;
        uint8_t g = (uint8_t)(50.0f + 200.0f * t);
        uint8_t b = (uint8_t)(30.0f * t * t);     // squared so it darkens fast
        rdpq_set_prim_color(RGBA32(r, g, b, 255));
        // Shrink from scale0 down to ~0 over the particle's life.
        float scale = hp->scale0 * t;
        if (scale < 0.05f) scale = 0.05f;

        int matIdx = frameIdx * HIT_PARTICLE_COUNT + i;
        t3d_mat4fp_from_srt_euler(&sv->hit_particle_matrices[matIdx],
            (float[3]){scale, scale, scale},
            (float[3]){0.0f, 0.0f, 0.0f},
            (float[3]){hp->x, hp->y, hp->z}
        );
        t3d_matrix_push(&sv->hit_particle_matrices[matIdx]);
        mesh_draw_cube(sv->proj_mesh);
        t3d_matrix_pop(1);
    }

    // Restore the main viewport + full-screen scissor for any subsequent
    // 2D overlays (e.g. the title text in main.c).
    rdpq_set_scissor(0, 0, 320, 240);
    if (main_viewport) {
        t3d_viewport_attach(main_viewport);
    }

    // Draw a 2-px steel-blue frame around the corner viewport. Done in fill
    // mode (2D), independent of the t3d viewport. Four rectangles forming a
    // hollow ring; the inner viewport content is already committed.
    rdpq_sync_pipe();
    rdpq_set_mode_fill(RGBA32(160, 180, 210, 255));
    const int B = 2;
    int x0 = SHIP_VIEW_X - B;
    int y0 = SHIP_VIEW_Y - B;
    int x1 = SHIP_VIEW_X + SHIP_VIEW_WIDTH  + B;
    int y1 = SHIP_VIEW_Y + SHIP_VIEW_HEIGHT + B;
    rdpq_fill_rectangle(x0,     y0,     x1,     y0 + B);   // top
    rdpq_fill_rectangle(x0,     y1 - B, x1,     y1);       // bottom
    rdpq_fill_rectangle(x0,     y0 + B, x0 + B, y1 - B);   // left
    rdpq_fill_rectangle(x1 - B, y0 + B, x1,     y1 - B);   // right

    // ---- Off-screen enemy arrows --------------------------------------
    // Project each active enemy through the corner viewport's matCamProj
    // (still populated from the draw above). Anything outside [-1,1]² in
    // NDC, or behind the camera (w<=0), gets a small red triangle on the
    // radar's inner edge pointing toward it. Helps the gunner pivot the
    // ship toward threats they can't see in the corner inset.
    rdpq_sync_pipe();
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_set_prim_color(RGBA32(255, 80, 80, 255));

    const float ARROW_LEN  = 6.0f;
    const float ARROW_HALF = 3.5f;
    const float MARGIN     = 5.0f;
    const float cx     = SHIP_VIEW_X + SHIP_VIEW_WIDTH  * 0.5f;
    const float cy     = SHIP_VIEW_Y + SHIP_VIEW_HEIGHT * 0.5f;
    const float half_w = SHIP_VIEW_WIDTH  * 0.5f - MARGIN;
    const float half_h = SHIP_VIEW_HEIGHT * 0.5f - MARGIN;

    for (int i = 0; i < SHIP_VIEW_ENEMIES; i++) {
        ShipEnemy *e = &sv->enemies[i];
        if (!e->active) continue;     // skip dead/exploding enemies

        T3DVec3 wp = {{e->x, e->y, e->z}};
        T3DVec4 clip;
        t3d_mat4_mul_vec3(&clip, &sv->viewport.matCamProj, &wp);

        float w = clip.v[3];
        float ndc_x, ndc_y;
        bool off_screen;
        if (w <= 0.001f) {
            // Behind the camera: project the world horizontal delta onto a
            // bottom-edge indicator (chase-cam orientation: "behind ship"
            // reads as "below the radar"). Pick the side from sign(dx).
            float dx_world = e->x - sv->x;
            float adx = fabsf(dx_world);
            ndc_x = (adx > 0.001f) ? (dx_world / adx) * 0.6f : 0.0f;
            ndc_y = -1.0f;     // bottom of the radar in NDC
            off_screen = true;
        } else {
            ndc_x = clip.v[0] / w;
            ndc_y = clip.v[1] / w;
            off_screen = (ndc_x < -1.0f || ndc_x > 1.0f ||
                          ndc_y < -1.0f || ndc_y > 1.0f);
        }
        if (!off_screen) continue;

        // NDC +y is up, screen +y is down — flip Y.
        float scr_dx = ndc_x;
        float scr_dy = -ndc_y;
        float len = sqrtf(scr_dx * scr_dx + scr_dy * scr_dy);
        if (len < 0.001f) continue;
        scr_dx /= len;
        scr_dy /= len;

        // Walk the ray (cx,cy) + t*(scr_dx,scr_dy) to the first edge of the
        // inset rect. Pick the smaller of the per-axis hit distances.
        float tx = (scr_dx >  0.001f) ?  half_w / scr_dx
                 : (scr_dx < -0.001f) ? -half_w / scr_dx
                 : 1e6f;
        float ty = (scr_dy >  0.001f) ?  half_h / scr_dy
                 : (scr_dy < -0.001f) ? -half_h / scr_dy
                 : 1e6f;
        float t = (tx < ty) ? tx : ty;
        float tip_x = cx + scr_dx * t;
        float tip_y = cy + scr_dy * t;

        // Triangle pointing along (scr_dx, scr_dy): tip on the edge, base
        // pulled inward by ARROW_LEN, perpendicular spread by ARROW_HALF.
        float bx = tip_x - scr_dx * ARROW_LEN;
        float by = tip_y - scr_dy * ARROW_LEN;
        float perp_x = -scr_dy;
        float perp_y =  scr_dx;
        float v_tip[2] = { tip_x, tip_y };
        float v_l[2]   = { bx + perp_x * ARROW_HALF, by + perp_y * ARROW_HALF };
        float v_r[2]   = { bx - perp_x * ARROW_HALF, by - perp_y * ARROW_HALF };
        rdpq_triangle(&TRIFMT_FILL, v_tip, v_l, v_r);
    }
}

int ship_view_score(const ShipView *sv)
{
    return sv->score;
}

int ship_view_hull(const ShipView *sv)
{
    return sv->hull_hp;
}

int ship_view_hull_max(const ShipView *sv)
{
    return sv->hull_max;
}

bool ship_view_is_game_over(const ShipView *sv)
{
    return sv->game_over;
}

int ship_view_station_hp(const ShipView *sv, SubsystemId id)
{
    if (id < 0 || id >= STATION_COUNT) return 0;
    return sv->station_hp[id];
}

int ship_view_station_max(const ShipView *sv)
{
    return sv->station_max;
}

void ship_view_repair_station(ShipView *sv, SubsystemId id, int amount)
{
    if (sv->game_over) return;
    if (id < 0 || id >= STATION_COUNT) return;
    if (amount <= 0) return;
    sv->station_hp[id] += amount;
    if (sv->station_hp[id] > sv->station_max) sv->station_hp[id] = sv->station_max;
}

void ship_view_damage_station(ShipView *sv, SubsystemId id, int amount)
{
    if (sv->game_over) return;
    if (id < 0 || id >= STATION_COUNT) return;
    if (amount <= 0) return;
    sv->station_hp[id] -= amount;
    if (sv->station_hp[id] < 0) sv->station_hp[id] = 0;
}

int ship_view_shield(const ShipView *sv, ShieldFace face)
{
    if (face < 0 || face >= SHIELD_FACE_COUNT) return 0;
    return sv->shield_face_hp[face];
}

int ship_view_shield_max(const ShipView *sv, ShieldFace face)
{
    if (face < 0 || face >= SHIELD_FACE_COUNT) return 0;
    return sv->shield_face_max[face];
}

int ship_view_shield_total(const ShipView *sv)
{
    int sum = 0;
    for (int f = 0; f < SHIELD_FACE_COUNT; f++) sum += sv->shield_face_hp[f];
    return sum;
}

void ship_view_shield_add(ShipView *sv, ShieldFace face, int amount)
{
    if (face < 0 || face >= SHIELD_FACE_COUNT) return;
    int v = sv->shield_face_hp[face] + amount;
    if (v < 0) v = 0;
    if (v > sv->shield_face_max[face]) v = sv->shield_face_max[face];
    sv->shield_face_hp[face] = v;
}

float ship_view_heat(const ShipView *sv)
{
    return sv->heat;
}

float ship_view_heat_max(const ShipView *sv)
{
    (void)sv;
    return HEAT_MAX;
}

bool ship_view_weapons_locked(const ShipView *sv)
{
    return sv->heat >= HEAT_MAX;
}

void ship_view_set_power(ShipView *sv,
                         float engines, float weapons, float shields)
{
    // Caller passes raw eng->energy[] percentages. We just stash them;
    // the per-frame scaling math derives effective values from each
    // channel's share relative to POWER_REFERENCE_PCT, which is robust to
    // the engineering console drifting slightly off a perfect 100 sum.
    if (engines < 0.0f) engines = 0.0f;
    if (weapons < 0.0f) weapons = 0.0f;
    if (shields < 0.0f) shields = 0.0f;
    sv->power[POWER_ENGINES] = engines;
    sv->power[POWER_WEAPONS] = weapons;
    sv->power[POWER_SHIELDS] = shields;
}

void ship_view_set_mission(ShipView *sv,
                           const ShipViewSpawnEntry *spawns, int spawn_count)
{
    // Tear down the current pool, then walk the roster spawning enemies
    // of the requested kinds until we run out of slots or roster
    // entries. Excess slots stay inactive.
    for (int i = 0; i < SHIP_VIEW_ENEMIES; i++) {
        sv->enemies[i].active = false;
        sv->enemies[i].hp     = 0;
        sv->enemies[i].explode_timer = 0;
    }
    int slot = 0;
    for (int s = 0; s < spawn_count && slot < SHIP_VIEW_ENEMIES; s++) {
        for (int n = 0; n < spawns[s].count && slot < SHIP_VIEW_ENEMIES; n++) {
            ShipEnemy *e = &sv->enemies[slot++];
            if (spawns[s].kind == ENEMY_KIND_CAPITAL) {
                enemy_capital_spawn(e, sv->x, sv->y, sv->z);
            } else if (spawns[s].kind == ENEMY_KIND_DUMMY) {
                enemy_dummy_spawn(e, n, sv->x, sv->y, sv->z);
            } else {
                enemy_respawn(e, sv->x, sv->y, sv->z);
            }
            e->no_respawn = true;
        }
    }
    sv->mission_active   = (slot > 0);
    sv->mission_complete = false;
}

bool ship_view_mission_complete(const ShipView *sv)
{
    return sv->mission_complete;
}

void ship_view_reset(ShipView *sv)
{
    // Clear hull damage and the game-over latch. mission_complete is
    // cleared so a freshly-retried mission can re-trigger the win
    // overlay; mission_active stays whatever it was, and main.c is
    // responsible for re-calling ship_view_set_mission to repopulate
    // the enemy roster after the reset.
    sv->hull_hp          = sv->hull_max;
    sv->game_over        = false;
    sv->mission_complete = false;

    // Refill every station so disabled subsystems come back online.
    for (int s = 0; s < STATION_COUNT; s++) {
        sv->station_hp[s] = sv->station_max;
    }

    // Phase-3/5 combat-state reset: every shield face refilled to its
    // ceiling at the current power allocation, regen accumulators
    // cleared, heat drained.
    float shield_share = sv->power[POWER_SHIELDS] / POWER_REFERENCE_PCT;
    int   face_ceiling = (int)((float)SHIELD_FACE_MAX * shield_share);
    if (face_ceiling < 0) face_ceiling = 0;
    for (int f = 0; f < SHIELD_FACE_COUNT; f++) {
        sv->shield_face_max[f] = face_ceiling;
        sv->shield_face_hp [f] = face_ceiling;
        sv->shield_face_acc[f] = 0.0f;
    }
    sv->heat = 0.0f;

    // Drain every projectile in the pool — both player shots in flight and
    // any enemy bullets that were heading for the hull when we died.
    for (int i = 0; i < SHIP_VIEW_PROJECTILES; i++) {
        sv->projectiles[i].timer = 0;
    }
    for (int i = 0; i < HIT_PARTICLE_COUNT; i++) {
        sv->hit_particles[i].life = 0;
    }

    // Respawn the squadron in a fresh shell around the (current) ship pos.
    for (int i = 0; i < SHIP_VIEW_ENEMIES; i++) {
        enemy_respawn(&sv->enemies[i], sv->x, sv->y, sv->z);
    }
}
