#include <math.h>
#include <string.h>

#include "extinguisher.h"
#include "fire.h"
#include "level_format.h"

static Extinguisher g_ext[EXTINGUISHER_MAX] = {0};
static int          g_count = 0;

void extinguishers_init_from_level(Level *level)
{
    g_count = 0;
    if (!level) return;
    for (int i = 0; i < level->num_entities && g_count < EXTINGUISHER_MAX; i++) {
        const LevelEntity *e = &level->entities[i];
        // group is NUL-padded; strncmp bounds it safely.
        if (strncmp(e->group, "extinguisher", LVL_GROUP_LEN) != 0) continue;

        // Convert cell coords → world coords. Mirrors level.c's
        // cell_to_world: col → (col - (W-1)/2) * TILE_SIZE, same for row/Z.
        float wx = ((float)e->x - (level->grid_w - 1) * 0.5f) * (float)TILE_SIZE;
        float wz = ((float)e->z - (level->grid_h - 1) * 0.5f) * (float)TILE_SIZE;

        Extinguisher *ex = &g_ext[g_count++];
        ex->x = wx;
        ex->y = 0.0f;
        ex->z = wz;
        ex->room_id = level_room_at(level, wx, wz);
        ex->available = true;
        ex->carried = false;
    }
}

void extinguishers_reset(void)
{
    for (int i = 0; i < g_count; i++) {
        g_ext[i].available = true;
        g_ext[i].carried = false;
    }
}

int extinguishers_count(void)
{
    return g_count;
}

const Extinguisher *extinguishers_get(int i)
{
    if (i < 0 || i >= g_count) return NULL;
    return &g_ext[i];
}

int extinguishers_try_grab(float x, float z)
{
    int best = -1;
    float best_d2 = EXTINGUISHER_GRAB_R2;
    for (int i = 0; i < g_count; i++) {
        if (!g_ext[i].available) continue;
        if (g_ext[i].carried)    continue;
        float dx = x - g_ext[i].x;
        float dz = z - g_ext[i].z;
        float d2 = dx * dx + dz * dz;
        if (d2 <= best_d2) {
            best = i;
            best_d2 = d2;
        }
    }
    if (best < 0) return -1;
    g_ext[best].carried   = true;
    g_ext[best].available = false;   // gone from the world while carried
    return best;
}

void extinguishers_drop(int idx)
{
    if (idx < 0 || idx >= g_count) return;
    g_ext[idx].carried = false;
    g_ext[idx].available = true;     // could be re-grabbed
}

void extinguishers_discharge(int idx)
{
    if (idx < 0 || idx >= g_count) return;
    Extinguisher *ex = &g_ext[idx];
    // Use the carrier's *current* room rather than the spawn room —
    // they're discharging where they stand. main.c looks the
    // discharge target room up via level_room_at(player position) and
    // calls fire_extinguish itself; we just consume the pickup here so
    // it can't be reused.
    ex->carried = false;
    ex->available = false;           // permanently consumed for the run
}
