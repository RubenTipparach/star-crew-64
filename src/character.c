#include <math.h>

#include "character.h"
#include "character_walk_anim.h"  // baked walk-cycle keyframes (CLAUDE.md guideline)

// The model is authored so that, after t3d's Y-rotation convention, the nose
// ends up 90° off from the control code's rot_y. Add π/2 at matrix build
// time so main.c can keep using rot_y = atan2f(dx, dz) cleanly.
#define CHARACTER_YAW_OFFSET 3.1415927f  // π (model faces +Z, needs 180° flip)

// Walk cycle tuning. Phase advance + ease-out are procedural; the actual
// per-bone pitch values come from the baked character_walk clip (see
// tools/gen-character-anim.py).
#define WALK_PHASE_GAIN    0.25f  // phase radians advanced per world unit moved
#define WALK_EASE_OUT      0.12f  // per-frame decay when not moving

// Part indices — must match the order of PARTS in tools/gen-character.py.
#define IDX_HEAD   0
#define IDX_NOSE   1
#define IDX_TORSO  2
#define IDX_ARM_L  3
#define IDX_ARM_R  4
#define IDX_LEG_L  5
#define IDX_LEG_R  6

// Pivot Y in local/model space (matches scaled coords in character_model.h):
// arm box spans y=6..11 → shoulder (top) pivot at y=11.
// leg box spans y=0..6  → hip (top) pivot at y=6.
// Rotation is around model X (forward/back pitch); X-position of the pivot
// doesn't matter because X-rotation leaves X untouched.
#define ARM_PIVOT_Y  11.0f
#define LEG_PIVOT_Y   6.0f

// ---- UV layout ----------------------------------------------------------
// character.png is a 32×32 horizontal-band atlas, 4 bands of 8px tall:
//   band 0 (y  0- 7): skin (head)
//   band 1 (y  8-15): shirt (torso)
//   band 2 (y 16-23): sleeves (arms)
//   band 3 (y 24-31): pants/boots (legs)
// Each part samples the band matching its index in CHARACTER_PART_NAMES.
// Order must match PARTS in tools/gen-character.py:
//   head, nose, torso, arm_l, arm_r, leg_l, leg_r
static const int16_t PART_UV_V0[CHARACTER_NUM_PARTS] = {
    UV(0),   // head   (band 0: skin)
    UV(0),   // nose   (band 0: skin)
    UV(8),   // torso  (band 1: shirt)
    UV(16),  // arm_l  (band 2: sleeves)
    UV(16),  // arm_r  (band 2: sleeves)
    UV(24),  // leg_l  (band 3: pants)
    UV(24),  // leg_r  (band 3: pants)
};
static const int16_t PART_UV_V1[CHARACTER_NUM_PARTS] = {
    UV(8), UV(8), UV(16), UV(24), UV(24), UV(32), UV(32),
};

static Character character_instance = {0};

// Build the 12 T3DVertPacked (6 faces × 2) for one axis-aligned box part.
// The caller passes the 8 corners laid out in the same order as box_verts()
// in gen-character.py:
//   v0 = (xmin, ymin, zmin)  v1 = (xmax, ymin, zmin)
//   v2 = (xmax, ymin, zmax)  v3 = (xmin, ymin, zmax)
//   v4 = (xmin, ymax, zmin)  v5 = (xmax, ymax, zmin)
//   v6 = (xmax, ymax, zmax)  v7 = (xmin, ymax, zmax)
// Per-face verts + normals give us flat shading (matches meshes.c style).
static void build_part(T3DVertPacked *out, const int16_t corners[8][3],
                       int16_t u0, int16_t u1, int16_t v0, int16_t v1)
{
    const int16_t (*c)[3] = corners;

    uint16_t nFront  = t3d_vert_pack_normal(&(T3DVec3){{ 0,  0,  1}});
    uint16_t nBack   = t3d_vert_pack_normal(&(T3DVec3){{ 0,  0, -1}});
    uint16_t nTop    = t3d_vert_pack_normal(&(T3DVec3){{ 0,  1,  0}});
    uint16_t nBottom = t3d_vert_pack_normal(&(T3DVec3){{ 0, -1,  0}});
    uint16_t nRight  = t3d_vert_pack_normal(&(T3DVec3){{ 1,  0,  0}});
    uint16_t nLeft   = t3d_vert_pack_normal(&(T3DVec3){{-1,  0,  0}});

    uint32_t white = 0xFFFFFFFF;

    // Front (+Z): v3, v2, v6, v7
    out[0] = (T3DVertPacked){
        .posA = {c[3][0], c[3][1], c[3][2]}, .rgbaA = white, .normA = nFront, .stA = {u0, v1},
        .posB = {c[2][0], c[2][1], c[2][2]}, .rgbaB = white, .normB = nFront, .stB = {u1, v1},
    };
    out[1] = (T3DVertPacked){
        .posA = {c[6][0], c[6][1], c[6][2]}, .rgbaA = white, .normA = nFront, .stA = {u1, v0},
        .posB = {c[7][0], c[7][1], c[7][2]}, .rgbaB = white, .normB = nFront, .stB = {u0, v0},
    };
    // Back (-Z): v1, v0, v4, v5
    out[2] = (T3DVertPacked){
        .posA = {c[1][0], c[1][1], c[1][2]}, .rgbaA = white, .normA = nBack, .stA = {u0, v1},
        .posB = {c[0][0], c[0][1], c[0][2]}, .rgbaB = white, .normB = nBack, .stB = {u1, v1},
    };
    out[3] = (T3DVertPacked){
        .posA = {c[4][0], c[4][1], c[4][2]}, .rgbaA = white, .normA = nBack, .stA = {u1, v0},
        .posB = {c[5][0], c[5][1], c[5][2]}, .rgbaB = white, .normB = nBack, .stB = {u0, v0},
    };
    // Top (+Y): v7, v6, v5, v4
    out[4] = (T3DVertPacked){
        .posA = {c[7][0], c[7][1], c[7][2]}, .rgbaA = white, .normA = nTop, .stA = {u0, v1},
        .posB = {c[6][0], c[6][1], c[6][2]}, .rgbaB = white, .normB = nTop, .stB = {u1, v1},
    };
    out[5] = (T3DVertPacked){
        .posA = {c[5][0], c[5][1], c[5][2]}, .rgbaA = white, .normA = nTop, .stA = {u1, v0},
        .posB = {c[4][0], c[4][1], c[4][2]}, .rgbaB = white, .normB = nTop, .stB = {u0, v0},
    };
    // Bottom (-Y): v0, v1, v2, v3
    out[6] = (T3DVertPacked){
        .posA = {c[0][0], c[0][1], c[0][2]}, .rgbaA = white, .normA = nBottom, .stA = {u0, v1},
        .posB = {c[1][0], c[1][1], c[1][2]}, .rgbaB = white, .normB = nBottom, .stB = {u1, v1},
    };
    out[7] = (T3DVertPacked){
        .posA = {c[2][0], c[2][1], c[2][2]}, .rgbaA = white, .normA = nBottom, .stA = {u1, v0},
        .posB = {c[3][0], c[3][1], c[3][2]}, .rgbaB = white, .normB = nBottom, .stB = {u0, v0},
    };
    // Right (+X): v2, v1, v5, v6
    out[8] = (T3DVertPacked){
        .posA = {c[2][0], c[2][1], c[2][2]}, .rgbaA = white, .normA = nRight, .stA = {u0, v1},
        .posB = {c[1][0], c[1][1], c[1][2]}, .rgbaB = white, .normB = nRight, .stB = {u1, v1},
    };
    out[9] = (T3DVertPacked){
        .posA = {c[5][0], c[5][1], c[5][2]}, .rgbaA = white, .normA = nRight, .stA = {u1, v0},
        .posB = {c[6][0], c[6][1], c[6][2]}, .rgbaB = white, .normB = nRight, .stB = {u0, v0},
    };
    // Left (-X): v0, v3, v7, v4
    out[10] = (T3DVertPacked){
        .posA = {c[0][0], c[0][1], c[0][2]}, .rgbaA = white, .normA = nLeft, .stA = {u0, v1},
        .posB = {c[3][0], c[3][1], c[3][2]}, .rgbaB = white, .normB = nLeft, .stB = {u1, v1},
    };
    out[11] = (T3DVertPacked){
        .posA = {c[7][0], c[7][1], c[7][2]}, .rgbaA = white, .normA = nLeft, .stA = {u1, v0},
        .posB = {c[4][0], c[4][1], c[4][2]}, .rgbaB = white, .normB = nLeft, .stB = {u0, v0},
    };
}

Character* character_create(void)
{
    Character *c = &character_instance;
    c->position = (T3DVec3){{0, 0, 0}};
    c->rot_y = 0.0f;
    c->walk_phase = 0.0f;

    c->texture = sprite_load("rom:/character.sprite");
    c->verts = malloc_uncached(sizeof(T3DVertPacked) * CHARACTER_PACKED_TOTAL);
    c->matrices = malloc_uncached(sizeof(T3DMat4FP) * FB_COUNT);
    c->part_matrices = malloc_uncached(
        sizeof(T3DMat4FP) * FB_COUNT * CHARACTER_NUM_PARTS);

    for (int p = 0; p < CHARACTER_NUM_PARTS; p++) {
        build_part(
            c->verts + p * CHARACTER_PACKED_PER_PART,
            CHARACTER_PART_POS[p],
            UV(0), UV(32),
            PART_UV_V0[p], PART_UV_V1[p]
        );
    }

    return c;
}

void character_set_position(Character *c, float x, float y, float z)
{
    c->position = (T3DVec3){{x, y, z}};
}

void character_face_direction(Character *c, float target_yaw, float smoothing)
{
    const float pi    = 3.1415927f;
    const float twoPi = 6.2831853f;
    float delta = target_yaw - c->rot_y;
    // Wrap to [-π, π] so we always take the shorter way around the circle.
    while (delta >  pi) delta -= twoPi;
    while (delta < -pi) delta += twoPi;
    c->rot_y += delta * smoothing;
    while (c->rot_y >  pi) c->rot_y -= twoPi;
    while (c->rot_y < -pi) c->rot_y += twoPi;
}

void character_animate(Character *c, float speed)
{
    if (speed > 0.01f) {
        c->walk_phase += speed * WALK_PHASE_GAIN;
        // Keep phase bounded to avoid FP loss over long sessions.
        const float twoPi = 6.2831853f;
        if (c->walk_phase > twoPi) c->walk_phase -= twoPi;
    } else if (c->walk_phase != 0.0f) {
        // Ease back toward rest (phase = 0 snapped to a multiple of π so legs
        // end straight). Cheap approach: shrink the amplitude by decaying the
        // phase toward the nearest zero-crossing.
        const float pi = 3.1415927f;
        float target = roundf(c->walk_phase / pi) * pi;
        c->walk_phase += (target - c->walk_phase) * WALK_EASE_OUT;
        if (fabsf(c->walk_phase - target) < 0.001f) c->walk_phase = 0.0f;
    }
}

// Build X-axis rotation of theta around pivot (0, pivotY, 0).
// t3d uses row-vector convention (v' = v · M, translation in M's bottom row),
// and its R_x sends (0, py, 0) to (0, py·cos, py·sin) — so the displacement
// needed to keep the pivot fixed is (I − R_x)·pivot = (0, py(1−cosθ), +py·sinθ).
// NOTE the +z sign: a -py·sinθ here (standard column-vector math) flings the
// limb off the body because t3d's rotation handedness is opposite.
static void build_swing_matrix(T3DMat4FP *out, float pivotY, float theta)
{
    float c = cosf(theta);
    float s = sinf(theta);
    t3d_mat4fp_from_srt_euler(out,
        (float[3]){1.0f, 1.0f, 1.0f},
        (float[3]){theta, 0.0f, 0.0f},
        (float[3]){0.0f, pivotY * (1.0f - c), pivotY * s}
    );
}

static void build_identity_matrix(T3DMat4FP *out)
{
    t3d_mat4fp_from_srt_euler(out,
        (float[3]){1.0f, 1.0f, 1.0f},
        (float[3]){0.0f, 0.0f, 0.0f},
        (float[3]){0.0f, 0.0f, 0.0f}
    );
}

void character_update_matrix(Character *c, int frameIdx)
{
    t3d_mat4fp_from_srt_euler(&c->matrices[frameIdx],
        (float[3]){1.0f, 1.0f, 1.0f},
        (float[3]){0.0f, c->rot_y + CHARACTER_YAW_OFFSET, 0.0f},
        (float[3]){c->position.v[0], c->position.v[1], c->position.v[2]}
    );

    // Sample the baked walk clip at the current phase. Phase is in [0, 2π);
    // map that onto the clip's frame index. We linearly interpolate between
    // adjacent frames so the result is smooth even when phase advances
    // between clip samples.
    const float twoPi = 6.2831853f;
    float frame_pos = (c->walk_phase / twoPi) * (float)CHARACTER_WALK_NUM_FRAMES;
    if (frame_pos < 0.0f) frame_pos += (float)CHARACTER_WALK_NUM_FRAMES;
    int   f0 = ((int)frame_pos) % CHARACTER_WALK_NUM_FRAMES;
    int   f1 = (f0 + 1)         % CHARACTER_WALK_NUM_FRAMES;
    float t  = frame_pos - (float)((int)frame_pos);
    if (t < 0.0f) t = 0.0f;
    const float *a = CHARACTER_WALK_KEYFRAMES[f0];
    const float *b = CHARACTER_WALK_KEYFRAMES[f1];
    float leg_l_theta = a[0] + (b[0] - a[0]) * t;
    float leg_r_theta = a[1] + (b[1] - a[1]) * t;
    float arm_l_theta = a[2] + (b[2] - a[2]) * t;
    float arm_r_theta = a[3] + (b[3] - a[3]) * t;

    T3DMat4FP *pm = c->part_matrices + frameIdx * CHARACTER_NUM_PARTS;
    build_identity_matrix(&pm[IDX_HEAD]);
    build_identity_matrix(&pm[IDX_NOSE]);
    build_identity_matrix(&pm[IDX_TORSO]);
    build_swing_matrix(&pm[IDX_ARM_L], ARM_PIVOT_Y, arm_l_theta);
    build_swing_matrix(&pm[IDX_ARM_R], ARM_PIVOT_Y, arm_r_theta);
    build_swing_matrix(&pm[IDX_LEG_L], LEG_PIVOT_Y, leg_l_theta);
    build_swing_matrix(&pm[IDX_LEG_R], LEG_PIVOT_Y, leg_r_theta);
}

void character_draw(Character *c, int frameIdx)
{
    rdpq_sync_pipe();
    rdpq_sync_tile();
    rdpq_sprite_upload(TILE0, c->texture, NULL);

    rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
    rdpq_mode_filter(FILTER_BILINEAR);
    t3d_state_set_drawflags(T3D_FLAG_TEXTURED | T3D_FLAG_SHADED | T3D_FLAG_DEPTH);

    t3d_matrix_push(&c->matrices[frameIdx]);
    T3DMat4FP *pm = c->part_matrices + frameIdx * CHARACTER_NUM_PARTS;
    for (int p = 0; p < CHARACTER_NUM_PARTS; p++) {
        t3d_matrix_push(&pm[p]);
        T3DVertPacked *partVerts = c->verts + p * CHARACTER_PACKED_PER_PART;
        // 12 packed structs = 24 individual verts per part.
        t3d_vert_load(partVerts, 0, 24);
        for (int face = 0; face < 6; face++) {
            int base = face * 4;
            t3d_tri_draw(base + 0, base + 1, base + 2);
            t3d_tri_draw(base + 0, base + 2, base + 3);
        }
        t3d_tri_sync();
        t3d_matrix_pop(1);
    }
    t3d_matrix_pop(1);
}
