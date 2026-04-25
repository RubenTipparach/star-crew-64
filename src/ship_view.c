#include <math.h>
#include <stdlib.h>

#include "ship_view.h"
#include "ship_idle_anim.h"

// Camera distance for the corner viewport. The ship is small, so we frame it
// from above-and-behind with a moderate FOV. Values tuned by eye against the
// ship's WORLD_SCALE (14) in gen-ship-c.py.
#define SV_FOV_DEG  55.0f
#define SV_NEAR     5.0f
#define SV_FAR      300.0f
#define SV_CAM_OFF_X  0.0f
#define SV_CAM_OFF_Y  18.0f
#define SV_CAM_OFF_Z  -28.0f   // behind the ship along its forward axis (-Z is behind)

// Pilot-driven motion tuning.
#define SHIP_TURN_RATE   0.02f   // radians per frame at full stick
#define SHIP_ACCEL       0.06f   // units/frame² added per impulse
#define SHIP_DAMP        0.985f  // friction multiplier per frame
#define SHIP_MAX_SPEED   1.5f

// Starfield backdrop quad — sized to fully cover the viewport at SV_FAR-ish
// depth. Drawn unlit, no z-write.
#define BG_HALF  120

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

// Big star backdrop quad facing +Z (camera looks down -Z toward the ship,
// so this is *behind* the ship from the viewport camera's POV).
static void build_bg_quad(T3DVertPacked *out)
{
    const int16_t S = BG_HALF;
    uint16_t n = t3d_vert_pack_normal(&(T3DVec3){{0, 0, 1}});
    uint32_t white = 0xFFFFFFFF;
    int16_t u0 = UV(0), v0 = UV(0);
    int16_t u1 = UV(64), v1 = UV(64);  // 2x tile so the starfield repeats nicely

    out[0] = (T3DVertPacked){
        .posA = {-S, -S, 0}, .rgbaA = white, .normA = n, .stA = {u0, v1},
        .posB = { S, -S, 0}, .rgbaB = white, .normB = n, .stB = {u1, v1},
    };
    out[1] = (T3DVertPacked){
        .posA = { S,  S, 0}, .rgbaA = white, .normA = n, .stA = {u1, v0},
        .posB = {-S,  S, 0}, .rgbaB = white, .normB = n, .stB = {u0, v0},
    };
}

ShipView* ship_view_create(void)
{
    ShipView *sv = &sv_instance;
    sv->viewport = t3d_viewport_create_buffered(FB_COUNT);
    sv->yaw  = 0.0f;
    sv->x = 0.0f; sv->y = 0.0f; sv->z = 0.0f;
    sv->vel = 0.0f;
    sv->anim_frame = 0.0f;
    sv->pilot_active = false;

    sv->texture    = sprite_load("rom:/ship.sprite");
    sv->bg_texture = sprite_load("rom:/starfield.sprite");

    sv->verts     = malloc_uncached(sizeof(T3DVertPacked) * SHIP_NUM_TRIS * 2);
    sv->matrices  = malloc_uncached(sizeof(T3DMat4FP) * FB_COUNT);
    sv->bg_matrix = malloc_uncached(sizeof(T3DMat4FP));
    sv->bg_verts  = malloc_uncached(sizeof(T3DVertPacked) * 2);

    build_verts(sv->verts);
    build_bg_quad(sv->bg_verts);

    // Backdrop sits far behind the ship so the camera frames it.
    t3d_mat4fp_from_srt_euler(sv->bg_matrix,
        (float[3]){1.0f, 1.0f, 1.0f},
        (float[3]){0.0f, 0.0f, 0.0f},
        (float[3]){0.0f, 0.0f, 80.0f}
    );

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
        // Idle: baked clip drives position + heading. We *replace* state
        // with the clip values (rather than integrate) so the loop stays in
        // sync regardless of how long pilot mode was active.
        sv->anim_frame += 1.0f;
        if (sv->anim_frame >= (float)SHIP_IDLE_NUM_FRAMES) {
            sv->anim_frame -= (float)SHIP_IDLE_NUM_FRAMES;
        }
        float fx, fy, fz, fpitch, fyaw, froll;
        sample_idle(sv->anim_frame, &fx, &fy, &fz, &fpitch, &fyaw, &froll);
        // The animation is in local "drift space"; multiply by ~14 to match
        // the ship's world scale (see gen-ship-c.py).
        const float A = 14.0f;
        sv->x = fx * A;
        sv->y = fy * A;
        sv->z = fz * A;
        sv->yaw = fyaw;
        pitch = fpitch;
        roll  = froll;
        sv->vel = 0.0f;
    }

    t3d_mat4fp_from_srt_euler(&sv->matrices[frameIdx],
        (float[3]){1.0f, 1.0f, 1.0f},
        (float[3]){pitch, sv->yaw, roll},
        (float[3]){sv->x, sv->y, sv->z}
    );
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

    // ---- Backdrop: starfield quad, drawn first with depth disabled.
    rdpq_sync_pipe();
    rdpq_sync_tile();
    rdpq_sprite_upload(TILE0, sv->bg_texture, NULL);
    rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
    rdpq_mode_filter(FILTER_BILINEAR);
    t3d_state_set_drawflags(T3D_FLAG_TEXTURED | T3D_FLAG_SHADED);
    t3d_matrix_push(sv->bg_matrix);
    t3d_vert_load(sv->bg_verts, 0, 4);
    t3d_tri_draw(0, 1, 2);
    t3d_tri_draw(0, 2, 3);
    t3d_tri_sync();
    t3d_matrix_pop(1);

    // ---- Ship: depth-tested + lit.
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

    // Restore the main viewport + full-screen scissor for any subsequent
    // 2D overlays (e.g. the title text in main.c).
    rdpq_set_scissor(0, 0, 320, 240);
    if (main_viewport) {
        t3d_viewport_attach(main_viewport);
    }
}
