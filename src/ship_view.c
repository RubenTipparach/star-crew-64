#include <math.h>
#include <stdlib.h>

#include "ship_view.h"
#include "meshes.h"  // mesh_create_laser_cube + mesh_draw_cube — shared projectile mesh

// Camera distance for the corner viewport. The ship is small, so we frame it
// from above-and-behind with a moderate FOV. Values tuned by eye against the
// ship's WORLD_SCALE (14) in gen-ship-c.py.
#define SV_FOV_DEG  55.0f
#define SV_NEAR     5.0f
#define SV_FAR      400.0f
#define SV_CAM_OFF_Y  32.0f
#define SV_CAM_OFF_Z  -52.0f   // behind the ship (camera sits at ship.z - this)

// Pilot-driven motion tuning.
#define SHIP_TURN_RATE   0.02f   // radians per frame at full stick
#define SHIP_ACCEL       0.06f   // units/frame² added per impulse
#define SHIP_DAMP        0.985f  // friction multiplier per frame
#define SHIP_MAX_SPEED   1.5f

// Star scatter — a long box ahead of and behind the camera so stars wrap as
// the ship drifts forward. Z range chosen so even at SV_FAR the far end is
// inside the frustum.
#define STAR_BOX_X_HALF   180.0f
#define STAR_BOX_Y_HALF    90.0f
#define STAR_BOX_Z_HALF   180.0f
#define STAR_QUAD_HALF      7    // local-space half-extent of each star quad
                                 // (~14 units across; enlarged so stars still
                                 // read at the bigger 120x90 viewport size)

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

// Star quad in the camera-facing local frame: lies in the XY plane with its
// normal at +Z. The per-star matrix orients this quad so that local +Z aims
// at the chase camera, giving every star a true spherical billboard look
// (rather than the old XZ-plane horizontal slab that read as edge-on lines
// from the corner camera's downward tilt).
static void build_star_quad(T3DVertPacked *out)
{
    const int16_t H = STAR_QUAD_HALF;
    uint16_t nFwd = t3d_vert_pack_normal(&(T3DVec3){{0, 0, 1}});
    uint32_t white = 0xFFFFFFFF;
    int16_t u0 = UV(0), v0 = UV(0), u1 = UV(8), v1 = UV(8);

    // Vertex order: bot-left, bot-right, top-right, top-left. UVs follow the
    // same orientation so the texture's "up" lines up with local +Y.
    out[0] = (T3DVertPacked){
        .posA = {-H, -H, 0}, .rgbaA = white, .normA = nFwd, .stA = {u0, v1},
        .posB = { H, -H, 0}, .rgbaB = white, .normB = nFwd, .stB = {u1, v1},
    };
    out[1] = (T3DVertPacked){
        .posA = { H,  H, 0}, .rgbaA = white, .normA = nFwd, .stA = {u1, v0},
        .posB = {-H,  H, 0}, .rgbaB = white, .normB = nFwd, .stB = {u0, v0},
    };
}

// Deterministic LCG so the star pattern is reproducible across runs.
static uint32_t lcg = 0x13371337u;
static float frand01(void)
{
    lcg = lcg * 1664525u + 1013904223u;
    return (lcg >> 8) * (1.0f / 16777216.0f);
}

// Build a 3x4 affine that places a star at `pos` and orients its local +Z
// toward the camera (true spherical billboard). Local +X aligns with
// (world_up × to_cam) so the texture's horizontal sits screen-horizontal,
// and local +Y completes the right-handed basis.
static void build_star_matrix(T3DMat4FP *out, const float pos[3],
                              const T3DVec3 *cam)
{
    T3DVec3 to_cam = {{
        cam->v[0] - pos[0],
        cam->v[1] - pos[1],
        cam->v[2] - pos[2],
    }};
    t3d_vec3_norm(&to_cam);

    T3DVec3 world_up = {{0.0f, 1.0f, 0.0f}};
    T3DVec3 right;
    t3d_vec3_cross(&right, &world_up, &to_cam);
    // Degenerate case: star directly above/below camera. Fall back to world
    // +X so we at least produce a valid basis.
    float rlen2 = right.v[0]*right.v[0] + right.v[1]*right.v[1]
                + right.v[2]*right.v[2];
    if (rlen2 < 1e-6f) {
        right = (T3DVec3){{1.0f, 0.0f, 0.0f}};
    } else {
        t3d_vec3_norm(&right);
    }

    T3DVec3 up;
    t3d_vec3_cross(&up, &to_cam, &right);

    // Row-vector convention: row i = local axis i mapped into world.
    T3DMat4 m = {{
        { right.v[0],  right.v[1],  right.v[2],  0.0f },
        { up.v[0],     up.v[1],     up.v[2],     0.0f },
        { to_cam.v[0], to_cam.v[1], to_cam.v[2], 0.0f },
        { pos[0],      pos[1],      pos[2],      1.0f },
    }};
    t3d_mat4_to_fixed_3x4(out, &m);
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
    sv->star_positions = malloc(sizeof(float) * 3 * SHIP_VIEW_STAR_COUNT);
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
    if (pilot_active) {
        sv->yaw += steer * SHIP_TURN_RATE;
        sv->vel += impulse * SHIP_ACCEL;
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
    // After wrapping, rebuild each star's matrix as a true spherical
    // billboard aimed at the chase camera (which lives at ship + camera
    // offset, see ship_view_draw). Cost: ~80 norm/cross/fixed-point converts
    // per frame — well under 1ms on the N64.
    const float TWO_X = 2.0f * STAR_BOX_X_HALF;
    const float TWO_Y = 2.0f * STAR_BOX_Y_HALF;
    const float TWO_Z = 2.0f * STAR_BOX_Z_HALF;
    T3DVec3 cam = {{
        sv->x,
        sv->y + SV_CAM_OFF_Y,
        sv->z + SV_CAM_OFF_Z,
    }};
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

        build_star_matrix(&sv->star_matrices[i], p, &cam);
    }

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

void ship_view_fire(ShipView *sv, ProjectileType type, float aim_yaw_offset)
{
    // Find a free slot.
    int slot = -1;
    for (int i = 0; i < SHIP_VIEW_PROJECTILES; i++) {
        if (sv->projectiles[i].timer <= 0) { slot = i; break; }
    }
    if (slot < 0) return;

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

    // ---- Background star billboards: unlit textured quads with alpha-test
    // so the sprites' transparent pixels punch through to the cleared color.
    // Same draw style as src/stars.c (TEX_FLAT + alphacompare), but with NO
    // depth flag — stars are a skybox-style background. If we let them write
    // depth, any star scattered between camera and ship would punch its
    // cross-shaped pixels into the depth buffer, and the ship's wings drawn
    // afterward would fail the depth test in those pixels (looks like
    // "stars-on-wings" with a wing-shaped halo around each cross).
    // Drawing them first without depth means the ship always overdraws
    // them cleanly.
    rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
    rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
    rdpq_mode_filter(FILTER_POINT);
    rdpq_mode_alphacompare(1);
    t3d_state_set_drawflags(T3D_FLAG_TEXTURED);

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
        } else {
            rdpq_set_prim_color(RGBA32(255, 230, 100, 255));   // hot yellow
        }
        float scale = (p->type == PROJ_TORPEDO) ? 1.7f : 1.0f;

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
}
