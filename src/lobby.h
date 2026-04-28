#ifndef LOBBY_H
#define LOBBY_H

#include "game_config.h"

// Pre-game lobby. Players press A on their controller to ready up. Once
// every connected pad has readied, P1 holds START for `LOBBY_LAUNCH_HOLD`
// frames to launch the mission. Up to 4 players are supported (one per
// hardware port).
//
// The lobby owns its own input polling + drawing — main() runs it in a
// blocking loop until `lobby_run` returns the final ready mask. From that
// point on the game loop spawns one Character per ready slot.

#define LOBBY_MAX_PLAYERS 4
#define LOBBY_LAUNCH_HOLD 180   // 3s @ ~60fps

typedef struct {
    bool connected[LOBBY_MAX_PLAYERS];
    bool ready[LOBBY_MAX_PLAYERS];
} LobbyState;

// Run the blocking lobby loop. Returns a bitmask of which player slots are
// ready (bit i = pid i). At least bit 0 is always set (P1 is required to
// launch). Callers should treat connected-but-not-ready slots as observers
// who won't be spawned.
//
// `font_id` is the rdpq registered font id (1 in this codebase).
// `viewport` is the main camera viewport — not used for projection here, but
// tying the lobby into the existing display init keeps things simple.
int lobby_run(int font_id);

#endif // LOBBY_H
