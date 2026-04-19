#ifndef CHARACTER_H
#define CHARACTER_H

#include "game_config.h"
#include "character_model.h"

// 7 parts × 6 faces × 2 packed verts per face = 84 T3DVertPacked (168 individual verts).
#define CHARACTER_PACKED_PER_PART  12
#define CHARACTER_PACKED_TOTAL     (CHARACTER_NUM_PARTS * CHARACTER_PACKED_PER_PART)

typedef struct {
    T3DVertPacked *verts;                                          // CHARACTER_PACKED_TOTAL entries, uncached
    T3DMat4FP     *matrices;                                       // FB_COUNT root matrices
    T3DMat4FP     *part_matrices;                                  // FB_COUNT * CHARACTER_NUM_PARTS, per-frame per-part transforms
    sprite_t      *texture;
    T3DVec3        position;
    float          rot_y;        // world yaw (radians)
    float          walk_phase;   // 0..2π, advances while moving
} Character;

Character* character_create(void);
void character_set_position(Character *c, float x, float y, float z);

// Rotate toward target_yaw (radians) via shortest-arc slerp.
// `smoothing` is a 0..1 per-frame factor: 1.0 snaps, ~0.2 is a smooth turn.
void character_face_direction(Character *c, float target_yaw, float smoothing);

// Advance the walk cycle proportionally to `speed` (world units/frame). Pass 0
// when idle and the cycle gently eases back to rest.
void character_animate(Character *c, float speed);

void character_update_matrix(Character *c, int frameIdx);
void character_draw(Character *c, int frameIdx);

#endif // CHARACTER_H
