#ifndef PROMPTS_H
#define PROMPTS_H

#include "game_config.h"

// Tiny billboard module for the floating button-prompt icons that hover
// above a player when they're near a console. Each prompt is a 16-pixel
// square sprite drawn as a camera-facing quad in world space.
//
// The main scene's camera offset is fixed (see camera.c), so the quad's
// orientation is precomputed once at create time and we only need a
// translation matrix per draw.

typedef enum {
    PROMPT_A     = 0,
    PROMPT_B     = 1,
    PROMPT_Z     = 2,
    PROMPT_STICK = 3,
    PROMPT_START = 4,
    PROMPT_COUNT
} PromptId;

typedef struct {
    sprite_t      *textures[PROMPT_COUNT];
    T3DVertPacked *quad;          // 2 packed structs, oriented toward the main camera
    T3DMat4FP     *scratch;       // single per-frame translation matrix
} Prompts;

Prompts* prompts_create(void);

// Draw one prompt billboard centered on (x, y, z) world coords. `id` selects
// which icon. Caller is responsible for batching synchronization.
void prompts_draw(Prompts *p, PromptId id, float x, float y, float z);

// Convenience for drawing two prompts side-by-side (used when a console
// shows both a primary + alternate weapon button, etc).
void prompts_draw_pair(Prompts *p, PromptId left, PromptId right,
                       float x, float y, float z, float spacing);

#endif // PROMPTS_H
