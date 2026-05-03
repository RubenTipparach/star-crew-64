#include "meshes.h"

#define CUBE_VERTS 12  // 6 faces, 4 verts each, packed 2 per struct

T3DVertPacked* mesh_create_cube(int texWidth, int texHeight)
{
    T3DVertPacked* verts = malloc_uncached(sizeof(T3DVertPacked) * CUBE_VERTS);
    const int16_t S = CUBE_HALF_SIZE;

    // UV coordinates for texture
    int16_t u0 = UV(0), v0 = UV(0);
    int16_t u1 = UV(texWidth), v1 = UV(texHeight);

    uint16_t normFront  = t3d_vert_pack_normal(&(T3DVec3){{ 0,  0,  1}});
    uint16_t normBack   = t3d_vert_pack_normal(&(T3DVec3){{ 0,  0, -1}});
    uint16_t normTop    = t3d_vert_pack_normal(&(T3DVec3){{ 0,  1,  0}});
    uint16_t normBottom = t3d_vert_pack_normal(&(T3DVec3){{ 0, -1,  0}});
    uint16_t normRight  = t3d_vert_pack_normal(&(T3DVec3){{ 1,  0,  0}});
    uint16_t normLeft   = t3d_vert_pack_normal(&(T3DVec3){{-1,  0,  0}});

    uint32_t white = 0xFFFFFFFF;

    // Front face (+Z)
    verts[0] = (T3DVertPacked){
        .posA = {-S, -S,  S}, .rgbaA = white, .normA = normFront, .stA = {u0, v1},
        .posB = { S, -S,  S}, .rgbaB = white, .normB = normFront, .stB = {u1, v1},
    };
    verts[1] = (T3DVertPacked){
        .posA = { S,  S,  S}, .rgbaA = white, .normA = normFront, .stA = {u1, v0},
        .posB = {-S,  S,  S}, .rgbaB = white, .normB = normFront, .stB = {u0, v0},
    };

    // Back face (-Z)
    verts[2] = (T3DVertPacked){
        .posA = { S, -S, -S}, .rgbaA = white, .normA = normBack, .stA = {u0, v1},
        .posB = {-S, -S, -S}, .rgbaB = white, .normB = normBack, .stB = {u1, v1},
    };
    verts[3] = (T3DVertPacked){
        .posA = {-S,  S, -S}, .rgbaA = white, .normA = normBack, .stA = {u1, v0},
        .posB = { S,  S, -S}, .rgbaB = white, .normB = normBack, .stB = {u0, v0},
    };

    // Top face (+Y)
    verts[4] = (T3DVertPacked){
        .posA = {-S,  S,  S}, .rgbaA = white, .normA = normTop, .stA = {u0, v1},
        .posB = { S,  S,  S}, .rgbaB = white, .normB = normTop, .stB = {u1, v1},
    };
    verts[5] = (T3DVertPacked){
        .posA = { S,  S, -S}, .rgbaA = white, .normA = normTop, .stA = {u1, v0},
        .posB = {-S,  S, -S}, .rgbaB = white, .normB = normTop, .stB = {u0, v0},
    };

    // Bottom face (-Y)
    verts[6] = (T3DVertPacked){
        .posA = {-S, -S, -S}, .rgbaA = white, .normA = normBottom, .stA = {u0, v1},
        .posB = { S, -S, -S}, .rgbaB = white, .normB = normBottom, .stB = {u1, v1},
    };
    verts[7] = (T3DVertPacked){
        .posA = { S, -S,  S}, .rgbaA = white, .normA = normBottom, .stA = {u1, v0},
        .posB = {-S, -S,  S}, .rgbaB = white, .normB = normBottom, .stB = {u0, v0},
    };

    // Right face (+X)
    verts[8] = (T3DVertPacked){
        .posA = { S, -S,  S}, .rgbaA = white, .normA = normRight, .stA = {u0, v1},
        .posB = { S, -S, -S}, .rgbaB = white, .normB = normRight, .stB = {u1, v1},
    };
    verts[9] = (T3DVertPacked){
        .posA = { S,  S, -S}, .rgbaA = white, .normA = normRight, .stA = {u1, v0},
        .posB = { S,  S,  S}, .rgbaB = white, .normB = normRight, .stB = {u0, v0},
    };

    // Left face (-X)
    verts[10] = (T3DVertPacked){
        .posA = {-S, -S, -S}, .rgbaA = white, .normA = normLeft, .stA = {u0, v1},
        .posB = {-S, -S,  S}, .rgbaB = white, .normB = normLeft, .stB = {u1, v1},
    };
    verts[11] = (T3DVertPacked){
        .posA = {-S,  S,  S}, .rgbaA = white, .normA = normLeft, .stA = {u1, v0},
        .posB = {-S,  S, -S}, .rgbaB = white, .normB = normLeft, .stB = {u0, v0},
    };

    return verts;
}

T3DVertPacked* mesh_create_laser_cube(void)
{
    T3DVertPacked* verts = malloc_uncached(sizeof(T3DVertPacked) * CUBE_VERTS);
    const int16_t S = LASER_CUBE_HALF_SIZE;

    uint32_t yellow = 0xFFFF00FF;
    uint32_t white  = 0xFFFFFFFF;

    uint16_t normFront  = t3d_vert_pack_normal(&(T3DVec3){{ 0,  0,  1}});
    uint16_t normBack   = t3d_vert_pack_normal(&(T3DVec3){{ 0,  0, -1}});
    uint16_t normTop    = t3d_vert_pack_normal(&(T3DVec3){{ 0,  1,  0}});
    uint16_t normBottom = t3d_vert_pack_normal(&(T3DVec3){{ 0, -1,  0}});
    uint16_t normRight  = t3d_vert_pack_normal(&(T3DVec3){{ 1,  0,  0}});
    uint16_t normLeft   = t3d_vert_pack_normal(&(T3DVec3){{-1,  0,  0}});

    // Front (+Z)
    verts[0] = (T3DVertPacked){
        .posA = {-S, -S,  S}, .rgbaA = yellow, .normA = normFront,
        .posB = { S, -S,  S}, .rgbaB = white,  .normB = normFront,
    };
    verts[1] = (T3DVertPacked){
        .posA = { S,  S,  S}, .rgbaA = yellow, .normA = normFront,
        .posB = {-S,  S,  S}, .rgbaB = white,  .normB = normFront,
    };
    // Back (-Z)
    verts[2] = (T3DVertPacked){
        .posA = { S, -S, -S}, .rgbaA = white,  .normA = normBack,
        .posB = {-S, -S, -S}, .rgbaB = yellow, .normB = normBack,
    };
    verts[3] = (T3DVertPacked){
        .posA = {-S,  S, -S}, .rgbaA = white,  .normA = normBack,
        .posB = { S,  S, -S}, .rgbaB = yellow, .normB = normBack,
    };
    // Top (+Y)
    verts[4] = (T3DVertPacked){
        .posA = {-S,  S,  S}, .rgbaA = yellow, .normA = normTop,
        .posB = { S,  S,  S}, .rgbaB = white,  .normB = normTop,
    };
    verts[5] = (T3DVertPacked){
        .posA = { S,  S, -S}, .rgbaA = yellow, .normA = normTop,
        .posB = {-S,  S, -S}, .rgbaB = white,  .normB = normTop,
    };
    // Bottom (-Y)
    verts[6] = (T3DVertPacked){
        .posA = {-S, -S, -S}, .rgbaA = white,  .normA = normBottom,
        .posB = { S, -S, -S}, .rgbaB = yellow, .normB = normBottom,
    };
    verts[7] = (T3DVertPacked){
        .posA = { S, -S,  S}, .rgbaA = white,  .normA = normBottom,
        .posB = {-S, -S,  S}, .rgbaB = yellow, .normB = normBottom,
    };
    // Right (+X)
    verts[8] = (T3DVertPacked){
        .posA = { S, -S,  S}, .rgbaA = yellow, .normA = normRight,
        .posB = { S, -S, -S}, .rgbaB = white,  .normB = normRight,
    };
    verts[9] = (T3DVertPacked){
        .posA = { S,  S, -S}, .rgbaA = yellow, .normA = normRight,
        .posB = { S,  S,  S}, .rgbaB = white,  .normB = normRight,
    };
    // Left (-X)
    verts[10] = (T3DVertPacked){
        .posA = {-S, -S, -S}, .rgbaA = white,  .normA = normLeft,
        .posB = {-S, -S,  S}, .rgbaB = yellow, .normB = normLeft,
    };
    verts[11] = (T3DVertPacked){
        .posA = {-S,  S,  S}, .rgbaA = white,  .normA = normLeft,
        .posB = {-S,  S, -S}, .rgbaB = yellow, .normB = normLeft,
    };

    return verts;
}

T3DVertPacked* mesh_create_capital_cube(int16_t half)
{
    T3DVertPacked* verts = malloc_uncached(sizeof(T3DVertPacked) * CUBE_VERTS);
    const int16_t S = half;

    // Capital ship colour theme: deeper red than the fighter, with
    // grey-purple highlights so the larger silhouette doesn't blend
    // into the fighters at a glance. Heavier dark/light contrast on
    // top/bottom so the player can read which way it's facing.
    uint32_t cap_red    = 0xB02020FF;
    uint32_t cap_dark   = 0x501010FF;
    uint32_t cap_grey   = 0x806080FF;
    uint32_t cap_white  = 0xC080A0FF;

    uint16_t normFront  = t3d_vert_pack_normal(&(T3DVec3){{ 0,  0,  1}});
    uint16_t normBack   = t3d_vert_pack_normal(&(T3DVec3){{ 0,  0, -1}});
    uint16_t normTop    = t3d_vert_pack_normal(&(T3DVec3){{ 0,  1,  0}});
    uint16_t normBottom = t3d_vert_pack_normal(&(T3DVec3){{ 0, -1,  0}});
    uint16_t normRight  = t3d_vert_pack_normal(&(T3DVec3){{ 1,  0,  0}});
    uint16_t normLeft   = t3d_vert_pack_normal(&(T3DVec3){{-1,  0,  0}});

    verts[0]  = (T3DVertPacked){
        .posA = {-S, -S,  S}, .rgbaA = cap_dark,  .normA = normFront,
        .posB = { S, -S,  S}, .rgbaB = cap_dark,  .normB = normFront,
    };
    verts[1]  = (T3DVertPacked){
        .posA = { S,  S,  S}, .rgbaA = cap_grey,  .normA = normFront,
        .posB = {-S,  S,  S}, .rgbaB = cap_grey,  .normB = normFront,
    };
    verts[2]  = (T3DVertPacked){
        .posA = { S, -S, -S}, .rgbaA = cap_dark,  .normA = normBack,
        .posB = {-S, -S, -S}, .rgbaB = cap_dark,  .normB = normBack,
    };
    verts[3]  = (T3DVertPacked){
        .posA = {-S,  S, -S}, .rgbaA = cap_red,   .normA = normBack,
        .posB = { S,  S, -S}, .rgbaB = cap_red,   .normB = normBack,
    };
    verts[4]  = (T3DVertPacked){
        .posA = {-S,  S,  S}, .rgbaA = cap_white, .normA = normTop,
        .posB = { S,  S,  S}, .rgbaB = cap_white, .normB = normTop,
    };
    verts[5]  = (T3DVertPacked){
        .posA = { S,  S, -S}, .rgbaA = cap_white, .normA = normTop,
        .posB = {-S,  S, -S}, .rgbaB = cap_white, .normB = normTop,
    };
    verts[6]  = (T3DVertPacked){
        .posA = {-S, -S, -S}, .rgbaA = cap_dark,  .normA = normBottom,
        .posB = { S, -S, -S}, .rgbaB = cap_dark,  .normB = normBottom,
    };
    verts[7]  = (T3DVertPacked){
        .posA = { S, -S,  S}, .rgbaA = cap_dark,  .normA = normBottom,
        .posB = {-S, -S,  S}, .rgbaB = cap_dark,  .normB = normBottom,
    };
    verts[8]  = (T3DVertPacked){
        .posA = { S, -S,  S}, .rgbaA = cap_red,   .normA = normRight,
        .posB = { S, -S, -S}, .rgbaB = cap_dark,  .normB = normRight,
    };
    verts[9]  = (T3DVertPacked){
        .posA = { S,  S, -S}, .rgbaA = cap_grey,  .normA = normRight,
        .posB = { S,  S,  S}, .rgbaB = cap_grey,  .normB = normRight,
    };
    verts[10] = (T3DVertPacked){
        .posA = {-S, -S, -S}, .rgbaA = cap_dark,  .normA = normLeft,
        .posB = {-S, -S,  S}, .rgbaB = cap_red,   .normB = normLeft,
    };
    verts[11] = (T3DVertPacked){
        .posA = {-S,  S,  S}, .rgbaA = cap_grey,  .normA = normLeft,
        .posB = {-S,  S, -S}, .rgbaB = cap_grey,  .normB = normLeft,
    };

    return verts;
}

T3DVertPacked* mesh_create_extinguisher(int16_t half_xz, int16_t half_y)
{
    T3DVertPacked* verts = malloc_uncached(sizeof(T3DVertPacked) * CUBE_VERTS);
    const int16_t SX = half_xz;
    const int16_t SY = half_y;

    // Two-tone palette: bright red body, black cap on the +Y face. The
    // small +Y cap reads as the cylindrical extinguisher's nozzle when
    // viewed from the 3/4 camera.
    uint32_t red       = 0xE03020FF;
    uint32_t red_hl    = 0xFF6050FF;
    uint32_t red_dk    = 0x901810FF;
    uint32_t cap_black = 0x202020FF;

    uint16_t normFront  = t3d_vert_pack_normal(&(T3DVec3){{ 0,  0,  1}});
    uint16_t normBack   = t3d_vert_pack_normal(&(T3DVec3){{ 0,  0, -1}});
    uint16_t normTop    = t3d_vert_pack_normal(&(T3DVec3){{ 0,  1,  0}});
    uint16_t normBottom = t3d_vert_pack_normal(&(T3DVec3){{ 0, -1,  0}});
    uint16_t normRight  = t3d_vert_pack_normal(&(T3DVec3){{ 1,  0,  0}});
    uint16_t normLeft   = t3d_vert_pack_normal(&(T3DVec3){{-1,  0,  0}});

    // Front (+Z)
    verts[0] = (T3DVertPacked){
        .posA = {-SX, -SY,  SX}, .rgbaA = red_dk,  .normA = normFront,
        .posB = { SX, -SY,  SX}, .rgbaB = red_dk,  .normB = normFront,
    };
    verts[1] = (T3DVertPacked){
        .posA = { SX,  SY,  SX}, .rgbaA = red_hl, .normA = normFront,
        .posB = {-SX,  SY,  SX}, .rgbaB = red_hl, .normB = normFront,
    };
    // Back (-Z)
    verts[2] = (T3DVertPacked){
        .posA = { SX, -SY, -SX}, .rgbaA = red_dk,  .normA = normBack,
        .posB = {-SX, -SY, -SX}, .rgbaB = red_dk,  .normB = normBack,
    };
    verts[3] = (T3DVertPacked){
        .posA = {-SX,  SY, -SX}, .rgbaA = red,     .normA = normBack,
        .posB = { SX,  SY, -SX}, .rgbaB = red,     .normB = normBack,
    };
    // Top (+Y) — black cap
    verts[4] = (T3DVertPacked){
        .posA = {-SX,  SY,  SX}, .rgbaA = cap_black, .normA = normTop,
        .posB = { SX,  SY,  SX}, .rgbaB = cap_black, .normB = normTop,
    };
    verts[5] = (T3DVertPacked){
        .posA = { SX,  SY, -SX}, .rgbaA = cap_black, .normA = normTop,
        .posB = {-SX,  SY, -SX}, .rgbaB = cap_black, .normB = normTop,
    };
    // Bottom (-Y)
    verts[6] = (T3DVertPacked){
        .posA = {-SX, -SY, -SX}, .rgbaA = red_dk,  .normA = normBottom,
        .posB = { SX, -SY, -SX}, .rgbaB = red_dk,  .normB = normBottom,
    };
    verts[7] = (T3DVertPacked){
        .posA = { SX, -SY,  SX}, .rgbaA = red_dk,  .normA = normBottom,
        .posB = {-SX, -SY,  SX}, .rgbaB = red_dk,  .normB = normBottom,
    };
    // Right (+X)
    verts[8] = (T3DVertPacked){
        .posA = { SX, -SY,  SX}, .rgbaA = red,    .normA = normRight,
        .posB = { SX, -SY, -SX}, .rgbaB = red_dk, .normB = normRight,
    };
    verts[9] = (T3DVertPacked){
        .posA = { SX,  SY, -SX}, .rgbaA = red_hl, .normA = normRight,
        .posB = { SX,  SY,  SX}, .rgbaB = red_hl, .normB = normRight,
    };
    // Left (-X)
    verts[10] = (T3DVertPacked){
        .posA = {-SX, -SY, -SX}, .rgbaA = red_dk, .normA = normLeft,
        .posB = {-SX, -SY,  SX}, .rgbaB = red,    .normB = normLeft,
    };
    verts[11] = (T3DVertPacked){
        .posA = {-SX,  SY,  SX}, .rgbaA = red_hl, .normA = normLeft,
        .posB = {-SX,  SY, -SX}, .rgbaB = red,    .normB = normLeft,
    };

    return verts;
}

void mesh_draw_cube(T3DVertPacked *verts)
{
    for (int face = 0; face < 6; face++) {
        t3d_vert_load(verts + face * 2, 0, 4);
        t3d_tri_draw(0, 1, 2);
        t3d_tri_draw(2, 3, 0);
        t3d_tri_sync();
    }
}
