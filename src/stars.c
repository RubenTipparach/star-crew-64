#include <math.h>

#include "stars.h"

// Shell geometry: stars are scattered between these two radii. The ship's
// bounding radius is roughly 300 (30 tiles × 20 units / 2), so 350..500 puts
// the stars comfortably outside the hull.
#define STAR_R_MIN    300.0f
#define STAR_R_MAX    700.0f
#define STAR_QUAD_SZ  6       // world units per star quad (small → dots on screen)

static Stars stars_instance = {0};

// Tiny horizontal quad lying on the XZ plane, centered on local origin so
// the per-star translation matrix can place it directly at the world point.
// UVs span 0..8 — matches the 8×8 star sprites exactly, no stretching.
static void build_star_quad(T3DVertPacked *out)
{
    const int16_t H = STAR_QUAD_SZ / 2;
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

// Deterministic LCG so regens of the starfield are reproducible. Seeding is
// fixed so the positions don't shuffle between builds.
static uint32_t lcg_state = 0x6a09e667u;
static float frand01(void)
{
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return (lcg_state >> 8) * (1.0f / 16777216.0f);  // 24-bit mantissa-ish
}

Stars* stars_create(void)
{
    Stars *s = &stars_instance;
    s->num_stars = STAR_COUNT;

    s->textures[0] = sprite_load("rom:/star_white.sprite");
    s->textures[1] = sprite_load("rom:/star_blue.sprite");
    s->textures[2] = sprite_load("rom:/star_yellow.sprite");
    s->textures[3] = sprite_load("rom:/star_red.sprite");

    s->quad = malloc_uncached(sizeof(T3DVertPacked) * 2);
    build_star_quad(s->quad);

    s->matrices    = malloc_uncached(sizeof(T3DMat4FP) * STAR_COUNT);
    s->tex_indices = malloc(sizeof(uint8_t) * STAR_COUNT);

    // Assign each star a texture type. Distribution favours white (more
    // common visually), then a handful of blue / yellow / red accents. We
    // lay the types out sorted so draw() can group sprite uploads without a
    // separate sort pass.
    int idx = 0;
    const int per_type[STAR_TYPE_COUNT] = {90, 38, 22, 10};  // sums to 160
    for (int t = 0; t < STAR_TYPE_COUNT; t++) {
        for (int i = 0; i < per_type[t]; i++) {
            s->tex_indices[idx++] = (uint8_t)t;
        }
    }

    // Scatter the stars on a spherical shell around origin. Fibonacci-ish
    // distribution via random polar coords is fine for a low-count
    // decorative field — no clumping issues at this density.
    lcg_state = 0x6a09e667u;
    for (int i = 0; i < STAR_COUNT; i++) {
        float u1 = frand01();
        float u2 = frand01();
        float u3 = frand01();

        float theta = u1 * 6.2831853f;                // 0..2π azimuth
        float cos_phi = 1.0f - 2.0f * u2;              // -1..1 uniform in z
        float sin_phi = sqrtf(1.0f - cos_phi * cos_phi);
        float r = STAR_R_MIN + (STAR_R_MAX - STAR_R_MIN) * u3;

        float x = r * sin_phi * cosf(theta);
        float z = r * sin_phi * sinf(theta);
        float y = r * cos_phi;

        t3d_mat4fp_from_srt_euler(
            &s->matrices[i],
            (float[3]){1.0f, 1.0f, 1.0f},
            (float[3]){0.0f, 0.0f, 0.0f},
            (float[3]){x, y, z}
        );
    }

    return s;
}

void stars_draw(Stars *s)
{
    // TEX_FLAT + white prim → unlit textured quads (scene lights don't wash
    // out the starfield). Point filtering keeps the 1-pixel star cores crisp,
    // and alpha-compare discards the sprite's transparent pixels so each
    // billboard reads as a dot rather than an 8×8 black square. Depth is on
    // so ship geometry properly occludes the stars behind it.
    rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
    rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
    rdpq_mode_filter(FILTER_POINT);
    rdpq_mode_alphacompare(1);
    t3d_state_set_drawflags(T3D_FLAG_TEXTURED | T3D_FLAG_DEPTH);

    uint8_t last_tex = 0xFF;
    for (int i = 0; i < s->num_stars; i++) {
        uint8_t t = s->tex_indices[i];
        if (t != last_tex) {
            rdpq_sync_pipe();
            rdpq_sync_tile();
            rdpq_sprite_upload(TILE0, s->textures[t], NULL);
            last_tex = t;
        }
        t3d_matrix_push(&s->matrices[i]);
        t3d_vert_load(s->quad, 0, 4);
        t3d_tri_draw(0, 1, 2);
        t3d_tri_draw(0, 2, 3);
        t3d_tri_sync();
        t3d_matrix_pop(1);
    }

    // Turn alpha-compare back off so later draws (level, character) don't
    // unexpectedly clip their own pixels.
    rdpq_mode_alphacompare(0);
}
