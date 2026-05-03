#ifndef FIRE_H
#define FIRE_H

#include <stdbool.h>
#include <stdint.h>

// Phase-7 fire suppression. Per-room state machine: each subsystem hit
// inside a room logs a timestamped event in a small ring buffer. When
// FIRE_IGNITE_HITS hits accumulate within FIRE_HIT_WINDOW frames the
// room ignites. Burning rooms tick damage onto any officer standing in
// them and onto the room's station HP. Extinguishing (via the
// extinguisher pickup or an engineering vent) clears the fire and the
// hit history.
//
// fire.c owns the per-room state. main.c is the policy layer:
//   1. Calls fire_init(level->num_rooms) once after level_load.
//   2. After each damage event in ship_view collision (delta detected
//      in main.c's station-HP snapshot loop), call fire_register_hit
//      with the room id of the damaged station.
//   3. Each frame, calls fire_tick() to advance hit ages + ignite/decay.
//   4. Each frame, iterates burning rooms via fire_is_burning() to
//      apply officer + station tick damage. (fire.c does NOT touch
//      ship_view or player slots — keeps the dependency one-way.)

#define FIRE_HIT_HISTORY      4         // entries per room's ring buffer
#define FIRE_HIT_WINDOW       180       // 3 s @ 60 fps
#define FIRE_IGNITE_HITS      3         // threshold within window
#define FIRE_OFFICER_DPS      2.0f      // HP/sec to officers in burning rooms
#define FIRE_STATION_DPS      0.5f      // HP/sec to the room's station

// Allocates the fire state array; must be called once after level_load.
// num_rooms == 0 is fine (fire system effectively disabled).
void fire_init(int num_rooms);

// Forget all fires + hit history. Used by ship_view_reset's caller in
// main.c so a START-after-loss starts the next run with no carryover.
void fire_reset(void);

// Per-frame tick: ages every hit history entry, ignites rooms that
// crossed the threshold this frame, holds burning state otherwise.
void fire_tick(void);

// Logs a fresh hit in this room's ring buffer. Igniting happens in
// fire_tick rather than here so the threshold check sees the just-
// added entry plus all currently-active ones consistently.
void fire_register_hit(uint8_t room_id);

bool fire_is_burning(uint8_t room_id);

// Clears the fire and the hit history for one room. Used by both the
// extinguisher discharge and the engineering vent.
void fire_extinguish(uint8_t room_id);

#endif // FIRE_H
