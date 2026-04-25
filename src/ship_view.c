#include <math.h>
#include <stdlib.h>

#include "ship_view.h"
#include "ship_idle_anim.h"
#include "meshes.h"  // mesh_create_laser_cube + mesh_draw_cube — shared projectile mesh

// Camera distance for the corner viewport. The ship is small, so we frame it
// from above-and-behind with a moderate FOV. Values tuned by eye against the
// ship's WORLD_SCALE (14) in gen-ship-c.py.
#define SV_FOV_DEG  55.0f
#define SV_NEAR     5.0f
#define SV_FAR      400.0f
#define SV_CAM_OFF_Y  18.0f
#define SV_CAM_OFF_Z  -28.0f   // behind the ship (camera sits at ship.z - this)

// Pilot-driven motion tuning.
#define SHIP_TURN_RATE   0.02f   // radians per frame at full stick
#define SHIP_ACCEL       0.06f   // units/frame² added per impulse
#define SHIP_DAMP        0.985f  // friction multiplier per frame
#define SHIP_MAX_SPEED   1.5f

// Forward drift applied while idle so we visibly "fly through" the
// star field (otherwise the camera stays fixed on the ship and the
// scene reads as static).
#define IDLE_DRIFT_VEL   0.5f    // world units / frame

// Star scatter — a long box ahead of and behind the camera so stars wrap as
// the ship drifts forward. Z range chosen so even at SV_FAR the far end is
// inside the frustum.
#define STAR_BOX_X_HALF   180.0f
#define STAR_BOX_Y_HALF    90.0f
#define STAR_BOX_Z_HALF   180.0f
#define STAR_QUAD_HALF      4    // local-space half-extent of each star quad

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

// Tiny horizontal star quad lying on XZ at local origin — same convention as
// src/stars.c. Per-star matrices place each instance.
static void build_star_quad(T3DVertPacked *out)
{
    const int16_t H = STAR_QUAD_HALF;
    uint16_t nUp = t3d_vert_pack_normal(&(T3DVec3){{0, 1, 0}});
    uint32_t white = 0xFFFFFFFF;
    int16_t u0 = UV(0), v0 = UV(0), u1 = UV(8), v1 = UV(8);

    out[0] = (T3DVertPacked){
        .posA = {-H, 0, -H}, .rgbaA = white, .normA = nUp, .stA = {u0, v0},
        .posB = { H, 0, -H}, .rgbaB = white, .normB = nUp, .stB = {u1, v0},
    };
    out[1] = (T3DVertPacked){
        .posA = { H, 0,  H}, .rgbaA = white, .normA = nUp, .stA = {u1, v1},
        .posB = {-H, 0,  H}, .rgbaB = white, .normB = nUp, .stB = {u0, v1},
    };
}

// Deterministic LCG so the star pattern is reproducible (and matches across
// builds).
static uint32_t lcg = 0x13371337u;
static float frand01(void)
{
    lcg = lcg * 1664525u + 1013904223u;
    return (lcg >> 8) * (1.0f / 16777216.0f);
}

static void place_star(T3DMat4FP *m, float ship_z)
{
    // X / Y are uniform in the box centred on the ship's lateral position.
    // Z is uniform in the box centred just AHEAD of the ship (so most stars
    // are visible from the trailing camera).
    float x = (frand01() * 2.0f - 1.0f) * STAR_BOX_X_HALF;
    float y = (frand01() * 2.0f - 1.0f) * STAR_BOX_Y_HALF;
    float z = ship_z + (frand01() * 2.0f - 1.0f) * STAR_BOX_Z_HALF;
    t3d_mat4fp_from_srt_euler(m,
        (float[3]){1.0f, 1.0f, 1.0f},
        (float[3]){0.0f, 0.0f, 0.0f},
        (float[3]){x, y, z}
    );
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
    sv->star_quad    = malloc_uncached(sizeof(T3DVertPacked) * 2);
    sv->star_matrices = malloc_uncached(sizeof(T3DMat4FP) * SHIP_VIEW_STAR_COUNT);
    sv->star_tex_idx = malloc(sizeof(uint8_t) * SHIP_VIEW_STAR_COUNT);
    sv->proj_mesh    = mesh_create_laser_cube();   // tiny vertex-coloured cube
    sv->proj_matrices = malloc_uncached(
        sizeof(T3DMat4FP) * FB_COUNT * SHIP_VIEW_PROJECTILES);
    for (int i = 0; i < SHIP_VIEW_PROJECTILES; i++) {
        sv->projectiles[i].timer = 0;   // inactive
    }

    build_verts(sv->verts);
    build_star_quad(sv->star_quad);

    // Type distribution: heavy white, smaller helping of accents (matches
    // stars.c so the two scenes look like the same galactic neighborhood).
    const int per_type[SHIP_VIEW_STAR_TYPES] = {14, 5, 3, 2};  // sums to 24
    int idx = 0;
    for (int t = 0; t < SHIP_VIEW_STAR_TYPES; t++) {
        for (int i = 0; i < per_type[t]; i++) {
            sv->star_tex_idx[idx++] = (uint8_t)t;
        }
    }
    lcg = 0x13371337u;
    for (int i = 0; i < SHIP_VIEW_STAR_COUNT; i++) {
        place_star(&sv->star_matrices[i], 0.0f);
    }

    return sv;
}

// Sample SHIP_IDLE_KEYFRAMES at fractional frame `f` (linearly interpolating
// between the floor + ceil frames for smoothness). Wraps modulo NUM_FRAMES
// since the clip loops.
static void sample_idle(float f, float *px, float *py, float *pz,
                        float *pitch, float *yaw, float *roll)
{
    if (f < 0.0f) f = 0.0f;
    int n = SHIP_IDLE_NUM_FRAMES;
    int i0 = ((int)f) % n;
    int i1 = (i0 + 1) % n;
    float t = f - (float)((int)f);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    const float *a = SHIP_IDLE_KEYFRAMES[i0];
    const float *b = SHIP_IDLE_KEYFRAMES[i1];
    *px    = a[0] + (b[0] - a[0]) * t;
    *py    = a[1] + (b[1] - a[1]) * t;
    *pz    = a[2] + (b[2] - a[2]) * t;
    *pitch = a[3] + (b[3] - a[3]) * t;
    *yaw   = a[4] + (b[4] - a[4]) * t;
    *roll  = a[5] + (b[5] - a[5]) * t;
}

void ship_view_update(ShipView *sv, int frameIdx,
                      bool pilot_active, float steer, float impulse)
{
    sv->pilot_active = pilot_active;

    float pitch = 0.0f, roll = 0.0f;

    if (pilot_active) {
        // Pilot drives: yaw integrates steer, position integrates velocity.
        sv->yaw += steer * SHIP_TURN_RATE;
        sv->vel += impulse * SHIP_ACCEL;
        sv->vel *= SHIP_DAMP;
        if (sv->vel >  SHIP_MAX_SPEED) sv->vel =  SHIP_MAX_SPEED;
        if (sv->vel < -SHIP_MAX_SPEED) sv->vel = -SHIP_MAX_SPEED;
        // Move along forward direction (+Z when yaw=0 due to nose at +Z).
        sv->x += sinf(sv->yaw) * sv->vel;
        sv->z += cosf(sv->yaw) * sv->vel;
        roll = -steer * 0.25f;  // bank into turns
    } else {
        // Idle: baked clip drives bob/sway/wobble; we layer constant forward
        // drift on top so the camera advances through the star field.
        sv->anim_frame += 1.0f;
        if (sv->anim_frame >= (float)SHIP_IDLE_NUM_FRAMES) {
            sv->anim_frame -= (float)SHIP_IDLE_NUM_FRAMES;
        }
        float fx, fy, fz, fpitch, fyaw, froll;
        sample_idle(sv->anim_frame, &fx, &fy, &fz, &fpitch, &fyaw, &froll);
        // Anim is in local "drift space"; multiply by ~14 to match the ship's
        // world scale (see gen-ship-c.py).
        const float A = 14.0f;
        sv->drift_z += IDLE_DRIFT_VEL;
        sv->x = fx * A;
        sv->y = fy * A;
        sv->z = sv->drift_z + fz * A;
        sv->yaw = fyaw;   // baked clip yaw is 0
        pitch   = fpitch;
        roll    = froll;
        sv->vel = 0.0f;
    }

    // Cycle one star per frame to a fresh spot ahead of the ship — keeps
    // the field "infinite" without needing thousands of stars or a per-frame
    // bounds-check loop. With SHIP_VIEW_STAR_COUNT=24 the whole field
    // refreshes about every 24 frames (~0.4s), fast enough that the player
    // never sees an empty pocket.
    static int next_recycle = 0;
    place_star(&sv->star_matrices[next_recycle],
               sv->z + STAR_BOX_Z_HALF);
    next_recycle = (next_recycle + 1) % SHIP_VIEW_STAR_COUNT;

    t3d_mat4fp_from_srt_euler(&sv->matrices[frameIdx],
        (float[3]){1.0f, 1.0f, 1.0f},
        (float[3]){pitch, sv->yaw, roll},
        (float[3]){sv->x, sv->y, sv->z}
    );

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
}

void ship_view_fire(ShipView *sv)
{
    // Find a free slot.
    int slot = -1;
    for (int i = 0; i < SHIP_VIEW_PROJECTILES; i++) {
        if (sv->projectiles[i].timer <= 0) { slot = i; break; }
    }
    if (slot < 0) return;

    ShipProjectile *p = &sv->projectiles[slot];
    // Forward direction: ship's yaw determines the heading, nose at +Z.
    float fx = sinf(sv->yaw);
    float fz = cosf(sv->yaw);
    // Spawn a short distance ahead of the hull so the projectile clears
    // the model on its first frame.
    const float NOSE_OFFSET = 12.0f;
    p->x = sv->x + fx * NOSE_OFFSET;
    p->y = sv->y + 1.5f;
    p->z = sv->z + fz * NOSE_OFFSET;
    p->vx = fx * SHIP_VIEW_PROJ_SPEED;
    p->vy = 0.0f;
    p->vz = fz * SHIP_VIEW_PROJ_SPEED;
    p->timer = SHIP_VIEW_PROJ_LIFETIME;
}

void ship_view_draw(ShipView *sv, int frameIdx, T3DViewport *main_viewport)
{
    // Camera that follows the ship from behind, in the ship's local frame
    // (rotated by yaw so we always see the back of the hull).
    float cx = sv->x - sinf(sv->yaw) * SV_CAM_OFF_Z;
    float cz = sv->z - cosf(sv->yaw) * SV_CAM_OFF_Z;
    T3DVec3 cam_pos = {{cx, sv->y + SV_CAM_OFF_Y, cz}};
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

    // ---- Background star billboards: unlit textured quads with alpha-test
    // so the sprites' transparent pixels punch through to the cleared color.
    // Same draw style as src/stars.c (TEX_FLAT + alphacompare).
    rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
    rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
    rdpq_mode_filter(FILTER_POINT);
    rdpq_mode_alphacompare(1);
    t3d_state_set_drawflags(T3D_FLAG_TEXTURED | T3D_FLAG_DEPTH);

    uint8_t last_tex = 0xFF;
    for (int i = 0; i < SHIP_VIEW_STAR_COUNT; i++) {
        uint8_t t = sv->star_tex_idx[i];
        if (t != last_tex) {
            rdpq_sync_pipe();
            rdpq_sync_tile();
            rdpq_sprite_upload(TILE0, sv->star_textures[t], NULL);
            last_tex = t;
        }
        t3d_matrix_push(&sv->star_matrices[i]);
        t3d_vert_load(sv->star_quad, 0, 4);
        t3d_tri_draw(0, 1, 2);
        t3d_tri_draw(0, 2, 3);
        t3d_tri_sync();
        t3d_matrix_pop(1);
    }
    rdpq_mode_alphacompare(0);

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

    // ---- Projectiles: tiny unlit yellow cubes, depth-tested.
    rdpq_sync_pipe();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_set_prim_color(RGBA32(255, 230, 100, 255));
    t3d_state_set_drawflags(T3D_FLAG_DEPTH);
    for (int i = 0; i < SHIP_VIEW_PROJECTILES; i++) {
        ShipProjectile *p = &sv->projectiles[i];
        if (p->timer <= 0) continue;
        int matIdx = frameIdx * SHIP_VIEW_PROJECTILES + i;
        t3d_mat4fp_from_srt_euler(&sv->proj_matrices[matIdx],
            (float[3]){1.0f, 1.0f, 1.0f},
            (float[3]){0.0f, 0.0f, 0.0f},
            (float[3]){p->x, p->y, p->z}
        );
        t3d_matrix_push(&sv->proj_matrices[matIdx]);
        mesh_draw_cube(sv->proj_mesh);
        t3d_matrix_pop(1);
    }

    // Restore the main viewport + full-screen scissor for any subsequent
    // 2D overlays (e.g. the title text in main.c).
    rdpq_set_scissor(0, 0, 320, 240);
    if (main_viewport) {
        t3d_viewport_attach(main_viewport);
    }
}
