#ifndef EXTINGUISHER_H
#define EXTINGUISHER_H

#include <stdbool.h>
#include <stdint.h>

#include "game_config.h"
#include "level.h"

// Phase-7 fire extinguisher pickups. Loaded from level entities tagged
// with group="extinguisher" — one per room, placed by the level
// designer. Each spawn becomes a small red cylinder prop on the floor.
// An officer walks up + presses A to grab it; pressing A again while
// carrying + standing in a burning room discharges it (clears the
// fire, consumes the pickup so it does NOT respawn during the run).
//
// Extinguishers are cooperative — once one is grabbed the prop visibly
// disappears from the world, and only the carrier can use it. Multiple
// extinguishers per ship; each can clear a different fire.

#define EXTINGUISHER_MAX        16     // hard cap, plenty for the bridge level
#define EXTINGUISHER_GRAB_R2    400.0f // (~20 units)² — pickup proximity squared

typedef struct {
    float    x, y, z;     // world position (y is the floor height for now)
    uint8_t  room_id;     // cached at load time so we don't keep re-querying
    bool     available;   // false if already grabbed and consumed
    bool     carried;     // true while in a player's hands (still un-respawned)
} Extinguisher;

// Init from the level data. Walks level entities, picks the first
// EXTINGUISHER_MAX with group "extinguisher", caches each one's
// world XZ + room id. Idempotent — calling again wipes prior state.
void extinguishers_init_from_level(Level *level);

// Reset all pickups back to available (used by ship_view_reset's caller
// in main.c so a START-after-loss starts the next run with a fresh
// extinguisher inventory).
void extinguishers_reset(void);

// Iterate. n() returns the count; get(i) returns a const pointer.
int extinguishers_count(void);
const Extinguisher *extinguishers_get(int i);

// Try to pick up the closest available extinguisher within
// EXTINGUISHER_GRAB_R2 of (x,z). Returns the index on success (and
// sets carried=true, available=false), or -1 if nothing in range.
int  extinguishers_try_grab(float x, float z);

// Drop a carried extinguisher (used by handover or game-over reset).
void extinguishers_drop(int idx);

// Discharge a carried extinguisher: clears the fire in the given
// room id (caller has already verified the carrier is standing there)
// and marks the pickup permanently consumed for this run.
void extinguishers_discharge(int idx);

#endif // EXTINGUISHER_H
