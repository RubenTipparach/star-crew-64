#include <math.h>

#include "stars.h"

// Shell geometry: stars are scattered between these two radii. Each star is
// drawn as a 2D sprite blit at its projected screen position, so the radius
// only affects perspective foreshortening (parallax as the camera moves) —
// nothing actually rendered uses the world-space size.
#define STAR_R_MIN  300.0f
#define STAR_R_MAX  700.0f
#define STAR_HALF     4   // half-extent of the 8×8 sprite, used to center it
                          // on the projected screen point

static Stars stars_instance = {0};

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

    s->positions   = malloc(sizeof(T3DVec3) * STAR_COUNT);
    s->tex_indices = malloc(sizeof(uint8_t) * STAR_COUNT);

    // Assign each star a texture type. Distribution favours white (more
    // common visually), then a handful of blue / yellow / red accents.
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

        float theta = u1 * 6.2831853f;                 // 0..2π azimuth
        float cos_phi = 1.0f - 2.0f * u2;              // -1..1 uniform in z
        float sin_phi = sqrtf(1.0f - cos_phi * cos_phi);
        float r = STAR_R_MIN + (STAR_R_MAX - STAR_R_MIN) * u3;

        s->positions[i].v[0] = r * sin_phi * cosf(theta);
        s->positions[i].v[1] = r * cos_phi;
        s->positions[i].v[2] = r * sin_phi * sinf(theta);
    }

    return s;
}

void stars_draw(Stars *s, T3DViewport *viewport)
{
    // Stars are drawn as 2D sprite blits, not 3D textured quads. Going
    // through the t3d 3D pipeline for cutout sprites was unreliable: the
    // combination of TEX_FLAT + alphacompare + edge AA + textured-shaded
    // baseline state from t3d_frame_start kept producing 8×8 squares
    // instead of clean dots, no matter which mode bit we toggled. The 2D
    // path (rdpq_set_mode_standard + rdpq_sprite_blit) handles transparency
    // natively the way prompts/HUD already do, and projecting each star's
    // 3D world position to screen with t3d_viewport_calc_viewspace_pos
    // keeps the parallax-as-the-camera-moves look. Pattern lifted from
    // tiny3d/examples/23_hdr/skydome.c.
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_TEX);
    rdpq_mode_alphacompare(1);
    rdpq_set_prim_color(RGBA32(255, 255, 255, 255));

    int sw = viewport->size[0];
    int sh = viewport->size[1];
    int ox = viewport->offset[0];
    int oy = viewport->offset[1];

    for (int i = 0; i < s->num_stars; i++) {
        // Manual perspective project: clip = matCamProj * worldPos. We need
        // the homogeneous w too (t3d_viewport_calc_viewspace_pos throws it
        // away), so skip stars behind the camera before dividing.
        T3DVec4 clip;
        t3d_mat4_mul_vec3(&clip, &viewport->matCamProj, &s->positions[i]);
        if (clip.v[3] <= 0.001f) continue;

        float ndc_x = clip.v[0] / clip.v[3];
        float ndc_y = clip.v[1] / clip.v[3];
        if (ndc_x < -1.0f || ndc_x > 1.0f) continue;
        if (ndc_y < -1.0f || ndc_y > 1.0f) continue;

        // NDC → pixel: x maps [-1,1] → [0,sw], y is flipped (NDC up = +y,
        // screen y = down). Center the 8×8 sprite on the projected point.
        int sx = (int)((ndc_x * 0.5f + 0.5f) * sw) + ox - STAR_HALF;
        int sy = (int)((-ndc_y * 0.5f + 0.5f) * sh) + oy - STAR_HALF;

        rdpq_sprite_blit(s->textures[s->tex_indices[i]], sx, sy, NULL);
    }

    // The 2D sprite path clobbers the t3d-required RDP mode (cycle, blender,
    // perspective, depth, AA). Re-run t3d_frame_start so the level / panels /
    // character that draw next get their pipeline back. This only resets RDP
    // mode bits — the camera/viewport attachment is untouched.
    t3d_frame_start();
}
