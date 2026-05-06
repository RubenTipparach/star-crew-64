#ifndef MISSIONS_H
#define MISSIONS_H

#include <stdint.h>

// Phase-8 missions. Replaces the hardcoded "always 5 fighters" enemy
// roster with a per-mission spawn list. Win condition is implicit:
// kill the entire roster. Failure conditions are unchanged (hull
// destroyed or all officers incapacitated, both already in main.c).
//
// Two mission flavours today:
//   Instant Action — pure combat sims. 1, 2, or 3 fighters, OR 1
//     capital ship. Quick repeatable encounters for tuning + warm-up.
//   Simple Mission — first-run scripted encounter. Fight either 3
//     fighters or 1 capital ship (player picks); same rules as the
//     equivalent Instant Action presets, but tagged as "story" so the
//     win text and music can differ later.

#define MISSION_ROSTER_MAX 4    // distinct enemy-type entries per mission

typedef enum {
    MIS_TARGET_TEST  = 0,   // tutorial / practice — stationary dummies
    MIS_INSTANT_1F   = 1,
    MIS_INSTANT_2F   = 2,
    MIS_INSTANT_3F   = 3,
    MIS_INSTANT_CAP  = 4,
    MIS_SIMPLE_3F    = 5,
    MIS_SIMPLE_CAP   = 6,
    MISSION_COUNT
} MissionId;

typedef enum {
    ENEMY_FIGHTER = 0,
    ENEMY_CAPITAL = 1,
    // Dummy target. Renders as a fighter but doesn't move and doesn't
    // fire — used by the TARGET TEST mission so a player can practice
    // aiming + firing without taking return fire. ship_view_set_mission
    // maps this to ENEMY_KIND_DUMMY.
    ENEMY_DUMMY   = 2,
} EnemyType;

typedef struct {
    EnemyType type;
    int       count;
} EnemySpawn;

typedef struct {
    MissionId   id;
    const char *title;
    const char *blurb;
    EnemySpawn  roster[MISSION_ROSTER_MAX];
    int         roster_count;
} MissionDef;

// Returns the static mission table (never null). Indexed by MissionId
// in 0..MISSION_COUNT-1.
const MissionDef *missions_table(void);

// Convenience getter; clamps to a valid id and returns the matching
// def. NULL is never returned.
const MissionDef *missions_get(MissionId id);

#endif // MISSIONS_H
