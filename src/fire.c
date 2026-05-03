#include <stdlib.h>
#include <string.h>

#include "fire.h"

typedef struct {
    int  hit_age[FIRE_HIT_HISTORY];
    bool burning;
} FireState;

static FireState *g_fires = NULL;
static int        g_room_count = 0;

void fire_init(int num_rooms)
{
    if (g_fires) free(g_fires);
    g_room_count = num_rooms;
    if (num_rooms <= 0) {
        g_fires = NULL;
        return;
    }
    g_fires = (FireState*)calloc(num_rooms, sizeof(FireState));
}

void fire_reset(void)
{
    if (!g_fires) return;
    memset(g_fires, 0, sizeof(FireState) * g_room_count);
}

void fire_tick(void)
{
    if (!g_fires) return;
    for (int r = 0; r < g_room_count; r++) {
        FireState *f = &g_fires[r];
        int active = 0;
        for (int i = 0; i < FIRE_HIT_HISTORY; i++) {
            if (f->hit_age[i] > 0) {
                f->hit_age[i]--;
                if (f->hit_age[i] > 0) active++;
            }
        }
        // Ignite once enough hits coexist in the window. Once burning,
        // stay burning until extinguish/vent clears it — the hit
        // history can decay, but the fire doesn't go out on its own.
        if (!f->burning && active >= FIRE_IGNITE_HITS) {
            f->burning = true;
        }
    }
}

void fire_register_hit(uint8_t room_id)
{
    if (!g_fires) return;
    if (room_id >= g_room_count) return;
    FireState *f = &g_fires[room_id];
    // Find an empty slot. If all are full, overwrite the oldest
    // (smallest age — about to expire anyway).
    int slot = -1;
    int oldest = FIRE_HIT_WINDOW + 1;
    for (int i = 0; i < FIRE_HIT_HISTORY; i++) {
        if (f->hit_age[i] == 0) { slot = i; break; }
        if (f->hit_age[i] < oldest) { oldest = f->hit_age[i]; slot = i; }
    }
    if (slot >= 0) f->hit_age[slot] = FIRE_HIT_WINDOW;
}

bool fire_is_burning(uint8_t room_id)
{
    if (!g_fires) return false;
    if (room_id >= g_room_count) return false;
    return g_fires[room_id].burning;
}

void fire_extinguish(uint8_t room_id)
{
    if (!g_fires) return;
    if (room_id >= g_room_count) return;
    FireState *f = &g_fires[room_id];
    f->burning = false;
    for (int i = 0; i < FIRE_HIT_HISTORY; i++) f->hit_age[i] = 0;
}
