#include <math.h>
#include <stdlib.h>

#include "prompts.h"

// Half-extent of the billboard quad in world units (so the visible icon is
// ~16 units across). Tuned by eye against the character's ~16-unit height.
#define PROMPT_HALF  9

static Prompts prompts_instance = {0};

// Build the camera-facing quad with vertex positions precomputed from the
// main scene's fixed camera basis. The main camera sits at
// (target + (CAMERA_OFFSET_X, CAMERA_OFFSET_Y, CAMERA_OFFSET_Z)) and looks
// at the target; the camera offset is constant world-space so we can bake
// the orientation right into the mesh.
//
// Right axis = world_up × normalize(camera_offset)
// Up    axis = normalize(camera_offset) × right
//
// With offsets (85, 120, 85), |o| ≈ 169.85, and the precomputed normalized
// basis is:
//   right ≈ ( 0.7071, 0,      -0.7071)
//   up    ≈ (-0.4998, 0.7077, -0.4998)
// We hard-code these so this function has no runtime dependency on the
// camera module's #defines.
static void build_quad(T3DVertPacked *out)
{
    const float Rx =  0.7071f, Ry = 0.0f,    Rz = -0.7071f;
    const float Ux = -0.4998f, Uy = 0.7077f, Uz = -0.4998f;
    const float S = (float)PROMPT_HALF;

    int16_t v0p[3] = { (int16_t)(-S*Rx + -S*Ux), (int16_t)(-S*Ry + -S*Uy), (int16_t)(-S*Rz + -S*Uz) };
    int16_t v1p[3] = { (int16_t)( S*Rx + -S*Ux), (int16_t)( S*Ry + -S*Uy), (int16_t)( S*Rz + -S*Uz) };
    int16_t v2p[3] = { (int16_t)( S*Rx +  S*Ux), (int16_t)( S*Ry +  S*Uy), (int16_t)( S*Rz +  S*Uz) };
    int16_t v3p[3] = { (int16_t)(-S*Rx +  S*Ux), (int16_t)(-S*Ry +  S*Uy), (int16_t)(-S*Rz +  S*Uz) };

    uint16_t n = t3d_vert_pack_normal(&(T3DVec3){{0.5004f, 0.7065f, 0.5004f}});
    uint32_t white = 0xFFFFFFFF;
    int16_t u0 = UV(0), uvtop = UV(0), u1 = UV(16), uvbot = UV(16);

    // Vertex order matches the texture's (0,0)=top-left layout: v3 = top-left,
    // v2 = top-right, v1 = bottom-right, v0 = bottom-left.
    out[0] = (T3DVertPacked){
        .posA = {v0p[0], v0p[1], v0p[2]}, .rgbaA = white, .normA = n, .stA = {u0, uvbot},
        .posB = {v1p[0], v1p[1], v1p[2]}, .rgbaB = white, .normB = n, .stB = {u1, uvbot},
    };
    out[1] = (T3DVertPacked){
        .posA = {v2p[0], v2p[1], v2p[2]}, .rgbaA = white, .normA = n, .stA = {u1, uvtop},
        .posB = {v3p[0], v3p[1], v3p[2]}, .rgbaB = white, .normB = n, .stB = {u0, uvtop},
    };
}

Prompts* prompts_create(void)
{
    Prompts *p = &prompts_instance;
    p->textures[PROMPT_A]     = sprite_load("rom:/prompt_a.sprite");
    p->textures[PROMPT_B]     = sprite_load("rom:/prompt_b.sprite");
    p->textures[PROMPT_Z]     = sprite_load("rom:/prompt_z.sprite");
    p->textures[PROMPT_STICK] = sprite_load("rom:/prompt_stick.sprite");
    p->textures[PROMPT_START] = sprite_load("rom:/prompt_start.sprite");

    p->quad    = malloc_uncached(sizeof(T3DVertPacked) * 2);
    p->scratch = malloc_uncached(sizeof(T3DMat4FP));
    build_quad(p->quad);
    return p;
}

void prompts_draw(Prompts *p, PromptId id, float x, float y, float z)
{
    rdpq_sync_pipe();
    rdpq_sync_tile();
    rdpq_sprite_upload(TILE0, p->textures[id], NULL);

    // Unlit textured quad with alpha-test so the sprite's transparent
    // background punches through to the scene behind it.
    rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
    rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_alphacompare(1);
    t3d_state_set_drawflags(T3D_FLAG_TEXTURED | T3D_FLAG_DEPTH);

    t3d_mat4fp_from_srt_euler(p->scratch,
        (float[3]){1.0f, 1.0f, 1.0f},
        (float[3]){0.0f, 0.0f, 0.0f},
        (float[3]){x, y, z}
    );

    t3d_matrix_push(p->scratch);
    t3d_vert_load(p->quad, 0, 4);
    t3d_tri_draw(0, 1, 2);
    t3d_tri_draw(0, 2, 3);
    t3d_tri_sync();
    t3d_matrix_pop(1);

    // Restore so subsequent draws don't unexpectedly clip on alpha.
    rdpq_mode_alphacompare(0);
}

void prompts_draw_pair(Prompts *p, PromptId left, PromptId right,
                       float x, float y, float z, float spacing)
{
    // Side-by-side along the camera's "right" axis (which is the same fixed
    // basis used in build_quad). Left icon is shifted by -spacing/2, right
    // by +spacing/2 along that axis.
    const float Rx = 0.7071f, Rz = -0.7071f;  // see build_quad comment
    float half = spacing * 0.5f;
    prompts_draw(p, left,  x - Rx * half, y, z - Rz * half);
    prompts_draw(p, right, x + Rx * half, y, z + Rz * half);
}
