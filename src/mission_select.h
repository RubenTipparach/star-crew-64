#ifndef MISSION_SELECT_H
#define MISSION_SELECT_H

#include "missions.h"

// Phase-8 mission select screen. 2D-only (no t3d frame), mirrors the
// lobby screen pattern. Player 1 cycles through the mission list with
// stick-Y / d-pad and confirms with A. Returns the chosen MissionId.
//
// Called once between lobby_run and the gameplay loop, and again after
// a mission win/loss when the player wants to pick a different
// scenario (the win/loss overlays in main.c trigger this re-entry).
//
// font_id is the rdpq_text font handle main.c registered. Use the same
// one the lobby uses so both screens look consistent.
MissionId mission_select_run(int font_id);

#endif // MISSION_SELECT_H
