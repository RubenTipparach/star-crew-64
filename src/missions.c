#include "missions.h"

static const MissionDef MISSIONS[MISSION_COUNT] = {
    {
        .id    = MIS_TARGET_TEST,
        .title = "TARGET TEST - LEARN/PRACTICE",
        .blurb = "Stationary dummies. No return fire. Practice your aim.",
        .roster = { { ENEMY_DUMMY, 6 } },
        .roster_count = 1,
    },
    {
        .id    = MIS_INSTANT_1F,
        .title = "INSTANT - 1 FIGHTER",
        .blurb = "Warm-up. One enemy fighter; clear at your leisure.",
        .roster = { { ENEMY_FIGHTER, 1 } },
        .roster_count = 1,
    },
    {
        .id    = MIS_INSTANT_2F,
        .title = "INSTANT - 2 FIGHTERS",
        .blurb = "Two-ship dogfight. Default tuning encounter.",
        .roster = { { ENEMY_FIGHTER, 2 } },
        .roster_count = 1,
    },
    {
        .id    = MIS_INSTANT_3F,
        .title = "INSTANT - 3 FIGHTERS",
        .blurb = "Hard-mode dogfight. Three converging fighters.",
        .roster = { { ENEMY_FIGHTER, 3 } },
        .roster_count = 1,
    },
    {
        .id    = MIS_INSTANT_CAP,
        .title = "INSTANT - 1 CAPITAL",
        .blurb = "Tank-busting drill. One capital ship, multi-emplacement.",
        .roster = { { ENEMY_CAPITAL, 1 } },
        .roster_count = 1,
    },
    {
        .id    = MIS_SIMPLE_3F,
        .title = "SIMPLE - FIGHTER PATROL",
        .blurb = "Fleet recon detected three hostile fighters in the sector.",
        .roster = { { ENEMY_FIGHTER, 3 } },
        .roster_count = 1,
    },
    {
        .id    = MIS_SIMPLE_CAP,
        .title = "SIMPLE - CAPITAL ENGAGEMENT",
        .blurb = "An enemy capital is sweeping the lane. Drive it off.",
        .roster = { { ENEMY_CAPITAL, 1 } },
        .roster_count = 1,
    },
};

const MissionDef *missions_table(void)
{
    return MISSIONS;
}

const MissionDef *missions_get(MissionId id)
{
    if (id < 0 || id >= MISSION_COUNT) id = MIS_INSTANT_2F;
    return &MISSIONS[id];
}
