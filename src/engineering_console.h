#ifndef ENGINEERING_CONSOLE_H
#define ENGINEERING_CONSOLE_H

#include "game_config.h"
#include "weapons_panel_model.h"

// Engineering console. The engineer allocates a fixed pool of energy
// across three systems (engines, weapons, shields), and can issue a
// "repair pulse" (A) that buffs teammates anywhere on the ship for a few
// seconds. Reuses the weapons-panel mesh with a different texture
// (engineering_panel.sprite) so the player can tell the stations apart at
// a glance.
//
// Energy values are in [0, 100] and always sum to ~100. While active, the
// stick selects a slot (X) and pumps power into / out of it (Y); the other
// two slots auto-balance so the total stays constant.
#define ENG_NUM_SYSTEMS 3
typedef enum {
    ENG_ENGINES = 0,
    ENG_WEAPONS = 1,
    ENG_SHIELDS = 2,
} EngSystem;

#define ENG_REPAIR_BUFF_FRAMES 240   // 4s @ ~60fps
#define ENG_REPAIR_COOLDOWN    300   // 5s before another pulse can be issued

// Phase-7: vent. C-down vents the room of the currently-targeted
// station (repair_target). Cooldown locks out repeated venting so it's
// not free emergency relief.
#define ENG_VENT_COOLDOWN      600   // 10s

typedef struct {
    T3DVertPacked *verts;
    T3DMat4FP     *matrix;
    sprite_t      *texture;
    T3DVec3        position;
    float          rot_y;

    float          seat_x;
    float          seat_z;
    float          seat_yaw;
    float          half_x;
    float          half_z;

    int            occupant_pid;
    bool           player_in_range_any;
    bool           prev_a[4];
    bool           prev_b[4];

    // Energy distribution: each entry in [0,100], sum tracked to ~100.
    float          energy[ENG_NUM_SYSTEMS];
    int            selected;          // active system index for stick Y

    // Repair-pulse state. While `repair_buff_frames > 0` teammates get the
    // "BUFFED" indicator; `repair_cooldown` prevents spamming.
    int            repair_buff_frames;
    int            repair_cooldown;

    // Tracks stick X edge for slot navigation so a flick = single step.
    float          stick_x_prev;

    // Phase-6: subsystem repair. Hold Z to enter repair mode; stick-X
    // flicks cycle the targeted station; HP fills while held. Energy
    // redistribution is suspended while repairing so the engineer
    // commits to one job at a time. repair_acc is the fractional carry
    // for sub-1-HP-per-frame healing (similar to shield_face_acc in
    // ship_view).
    int            repair_target;   // SubsystemId (0..3); default 0
    bool           repairing;
    float          repair_acc;
    float          repair_stick_x_prev;

    // Phase-7 vent. The vent_request flag latches when C-down is pressed
    // and the cooldown is clear; main.c drains it after drive() and
    // applies the consequences (clear fire, damage officers in room).
    int            vent_cooldown;
    bool           vent_request;
    bool           prev_c_down[4];
} EngineeringConsole;

EngineeringConsole* engineering_console_create(float x, float z, float facing_yaw);

bool engineering_console_try_engage(EngineeringConsole *e, int pid,
                                    float player_x, float player_z,
                                    joypad_inputs_t inputs);
bool engineering_console_drive(EngineeringConsole *e, int pid,
                               joypad_inputs_t inputs);
void engineering_console_update_proximity(EngineeringConsole *e,
                                          const float (*positions)[2],
                                          const bool *present, int max_pids);
bool engineering_console_blocks(const EngineeringConsole *e, float wx, float wz);

// Returns true while the repair pulse is active for any teammate.
bool engineering_console_repair_active(const EngineeringConsole *e);

// Phase-6: drains the queued station-repair amount for this frame.
// main.c calls this after engineering_console_drive and forwards the
// integer to ship_view_repair_station(repair_target, amount). Uses an
// internal accumulator so 5 HP/sec at the reference allocation is
// faithfully delivered as integer ticks.
int  engineering_console_consume_repair(EngineeringConsole *e,
                                        float engineer_hp_share);

// Phase-7: returns true and clears `vent_request` if the engineer
// triggered a vent this frame. main.c reads this, applies vent damage
// + fire_extinguish on the corresponding room. The room id itself is
// derived from repair_target by the caller (which already has the
// station→room cache).
bool engineering_console_consume_vent(EngineeringConsole *e);

void engineering_console_draw(EngineeringConsole *e);

#endif // ENGINEERING_CONSOLE_H
