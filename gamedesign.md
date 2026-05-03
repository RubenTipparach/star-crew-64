# star-crew-64 — game design

Working design doc for the combat / damage / station-management loop. Living
document — update as decisions land. Status markers: `[ ]` not started,
`[~]` in progress, `[x]` done.

## Core loop

Up to 4 players man a starship bridge (helm, weapons, engineering, science).
Enemy fighters attack the ship. Each console contributes to the ship's
combat survivability:

- **Helm** drives the ship (turn + impulse). Also rotates the ship to
  reorient damaged shield faces away from incoming fire.
- **Weapons** fires phasers + photon torpedoes through a forward firing
  arc — the gunner can't shoot what the helm hasn't pointed at.
- **Engineering** balances power between shields, weapons, and maneuverability.
  Also runs subsystem repairs and the fire-suppression toolkit.
- **Science** manages the 6-face shield grid: redistributes shield energy
  across faces and prioritises which face takes the next absorption.

Players can leave their stations to heal injured crew. Game ends when the
ship's hull is destroyed OR all four officers are incapacitated.

## Enemy AI

Each fighter runs an independent state machine with a randomized timer
offset so wing attacks don't sync up. Standard space-fighter pattern from
the GameDev.net references — state machine over steering primitives.

States:

| State        | Duration       | Behavior                                                   |
|--------------|----------------|------------------------------------------------------------|
| `ORBIT`      | 3–5 s          | Circle player at fixed radius. Steering: tangent + radial spring. |
| `ATTACK_RUN` | until in range | Pursue: velocity points at player, accelerate.            |
| `FIRE`       | ~0.5 s         | Spawn one enemy bullet aimed at the ship; continue through. |
| `RETREAT`    | 10 s           | Flee: velocity away from player, full throttle.           |
| → `ORBIT`    |                | Repeat.                                                    |

Per-fighter parameters: orbit radius, attack run trigger distance, fire
cadence, retreat duration. Tune by hand; no learning / GA.

Steering primitives (used inside states): seek, flee, pursue, tangent
(perpendicular to radial), radial spring (target-radius keeper).

Enemies have HP (already present) and an aabb / sphere collider for player
projectiles to hit (already present as `HIT_R2` sphere check in
`ship_view.c`).

## Player ship — health model

Three top-level bars rendered in the HUD:

- **Hull HP** (ship integrity). 0 → game over.
- **Shields** — absorbs damage before hull. Recharges based on engineering
  energy allocation.
- **Weapons heat** — fills when firing, dissipates over time. Above
  threshold → firing locked out.

Plus per-station HP (4 bars):

- Helm HP, Weapons HP, Engineering HP, Science HP. A station at 0 HP is
  *disabled* — its function stops working until repaired (TBD: repair
  mechanic, maybe heal-on-station).

Plus per-officer HP (one per player, MAX_PLAYERS). At 0 HP an officer is
*incapacitated* and lies on the floor; another player can revive by
walking up + mashing A.

## Damage routing

Enemy bullets spawn from the fighter's current position aimed at the
player's ship. Collision: sphere check against the ship's bounding radius.

When a bullet hits:

1. **Shields > 0** → shields absorb full damage, hull untouched, no station hit.
2. **Shields == 0** → hull takes damage, AND a station + its occupant take
   collateral damage based on the hit angle in ship-local space:

   | Hit angle (local) | Station damaged   |
   |-------------------|-------------------|
   | Front cone        | Weapons           |
   | Port              | Helm              |
   | Starboard         | Engineering       |
   | Aft               | Science           |

   *(Subject to adjustment once we know the bridge layout exactly.)*

   The occupant of the damaged station takes the same damage. Empty
   station → only subsystem HP drops, no officer hit.

## Engineering — energy allocation

Zero-sum 3-way slider summing to 100%. Default: 33/33/34.

| Channel           | Effect                                          |
|-------------------|-------------------------------------------------|
| Shields           | Shield max + recharge rate.                     |
| Weapons           | Damage per shot + heat dissipation rate.        |
| Maneuverability   | Turn rate + impulse acceleration on the helm.   |

Engineering console UI: three vertical bars + the active player can shift
allocation between adjacent bars (A = boost shields, B = boost weapons, Z
= boost maneuverability, or some equivalent two-axis stick scheme).

## HUD

Drawn over the main bridge view + corner viewport:

- Hull / Shields / Weapons-heat bars (top-left or top-right).
- Per-station HP bars rendered as small bars above each console mesh in
  the bridge interior (so players can see at a glance which station is
  hurting).
- Per-officer HP bar above each character.

Style: match the existing 2D rdpq fill-rectangle approach used by the lobby
screen (`src/lobby.c`) — small panels of solid colour, no fancy textures.

## Officer healing

- Walk within range of a downed officer (squared-distance check, same
  pattern as the existing station-prompt logic in `main.c`).
- Mash A. Each press adds a small chunk of HP. Holding doesn't work
  (avoids accidental heals).
- Healing puts the healer in an animation lockout for the duration; they
  can't operate any console until they walk away.
- Their station goes unoccupied while they heal, so the ship suffers in
  real time (helm freezes, weapons can't fire, etc.).

## Game over conditions

1. Hull HP == 0 → "ship destroyed".
2. All 4 officers incapacitated simultaneously → "crew incapacitated".

Either way: full-screen lose state + "press start to retry" lobby return.

## Phased implementation plan

### Phase 1 — combat foundations  `[x]`
- Enemy AI state machine (`ORBIT` / `ATTACK_RUN` / `FIRE` / `RETREAT`).
- Enemy bullet projectile type, spawned in `FIRE`, aimed at ship.
- Player ship collider sphere + bullet-vs-ship test.
- Hull HP value + bar in the HUD.
- Game over on hull = 0 (overlay + START to retry — no full lobby return yet).

### Phase 2 — subsystems & damage routing  `[x]`
- 4 station HP values stored on `ShipView`, indexed by `SubsystemId` aligned
  with main.c's `StationId` enum.
- Hit-angle → station mapping in ship-local space:
  - Front cone (|local angle| ≤ 45°) → Weapons
  - Aft cone   (|local angle| ≥ 135°) → Science
  - Port      (+45° … +135°) → Helm
  - Starboard (-135° … -45°) → Engineering
- Per-station HP bars rendered as 2D fill-rects projected over each console
  via `t3d_viewport_calc_viewspace_pos`.
- Subsystem-down effects: helm at 0 HP zeros steer/impulse; weapons at 0 HP
  drops fire-event consumption. Engineering / science gating slots in with
  Phase 3 / Phase 4 since those subsystems don't have concrete effects yet.

### Phase 3 — energy & heat  `[x]`
- Engineering console already had the 3-channel allocation
  (`eng->energy[ENG_ENGINES/WEAPONS/SHIELDS]`); main.c pumps those values
  into ship_view via `ship_view_set_power` once per frame. Engineering at
  0 HP freezes the allocation so the engineer can't reshuffle when the
  console is destroyed.
- Weapons heat bar (HEAT_MAX = 100). +8 per phaser, +25 per torpedo;
  dissipates at 15/sec scaled by weapons allocation. At HEAT_MAX
  `ship_view_fire` no-ops; `ship_view_weapons_locked` exposes the latch
  for the OVERHEAT label.
- Shield bar (SHIELD_BASE_MAX = 60 at reference allocation, scales
  linearly with shields power). Regenerates at 4/sec scaled, accumulator
  on the struct so fractional regen doesn't drop. Damage hits shields
  first; overflow bleeds through to hull + station.
- Player projectile damage scales with weapons allocation (clamped to ≥1
  if any weapons power is allocated). Helm steer/impulse scale with
  engines allocation — sluggish at 0%, snappy at 100%.

### Phase 4 — officers & healing  `[x]`
- `PlayerSlot` gains `hp` / `hp_max` / `down` / `prev_a`. `OFFICER_HP_MAX`
  = 100, `OFFICER_HEAL_PER_PRESS` = 8, `HEAL_RANGE` = 18 world units.
- Damage routing into occupants happens in main.c, not ship_view: each
  frame, snapshot `station_hp[]`, run `ship_view_update`, then route any
  negative delta to whoever's at that station. ship_view stays unaware
  of the crew abstraction.
- Going down sets `players[pid].down = true`, clears `station = NONE`,
  and force-clears the relevant console's `occupant_pid` so the seat
  reads empty.
- Heal interaction: walking officer (STATION_NONE, not down) presses A
  near a downed teammate (squared-distance check vs `HEAL_RANGE2`) → +8
  HP per press. Edge-detected on `players[i].prev_a`. The press is
  consumed (`inputs[i].btn.a = 0`) so the engage-console fall-through
  doesn't fire on the same press.
- Downed officers skip movement, station drives, and proximity updates.
- Per-character HP bar (22×3 px) projected over each player's head;
  downed officers show a red "DOWN" label and an "[A] REVIVE" hint when
  any alive teammate is in range.
- Crew-lost game-over latch in main.c: when all present officers are
  down, the same overlay shows with banner "CREW INCAPACITATED".
  START reset refills officer HP, clears `down`, drops both latches.

### Phase 5 — 6-face shields + forward firing arc  `[x]`
- Replace `shield_hp` / `shield_max_cur` with `shield_face_hp[6]` /
  `shield_face_max[6]`. Bullet-vs-hull collision picks the absorbing
  face by dominant ship-local axis.
- Science console UI: redistribute capacity across faces (analogous to
  engineering's three-channel slider, but six cells).
- Weapons console clamps `aim_yaw` to ±FORWARD_ARC_HALF (~25°) — gunner
  can't fire outside the cone; helm has to turn for off-axis targets.

### Phase 6 — subsystem repair  `[x]`
- Engineering hold-A repair targeting a single station at a time. While
  repairing, engineering can't reallocate power. Repair rate scales
  with engineering's own HP.

### Phase 7 — fire suppression  `[x]`
- Per-room fire state. Ignite on rapid subsystem damage to a station
  in that room. Fire deals tick damage to officers + station HP.
- Vent: airlock-open one-shot clear, large damage to officers in room.
- Extinguisher pickup: one per room (procedural red-cylinder mesh),
  walk-up + A grabs, A again at the fire discharges. Single use.

### Phase 8 — missions & win condition  `[x]`
- Mission-select screen between lobby and game.
- Mission descriptor: enemy spawn list, win condition.
- Instant Action presets (1, 2, 3 fighters / 1 capital ship).
- Simple Mission preset (3 fighters or 1 capital ship).
- Win screen + return path to mission select.

### Phase 9 — crew model: NPCs, body swap, port-agnostic lobby  `[x]` (partial)
- Lobby no longer auto-readies P1; any connected port readies via A,
  any ready+connected port can hold START to launch. Solo-on-port-3
  works.
- Always 4 characters spawned regardless of player count. PlayerSlot
  gains `controlling_port` (-1 = NPC); the IS_PLAYER macro replaces
  the old `present` check semantics.
- L/R bumpers cycle a walking player through the unclaimed (NPC)
  bodies, skipping any slot already controlled by another port. Old
  body becomes an NPC.
- NPCs idle-wander inside their assigned `home_room_id` (one per
  console room, distributed at startup). Pick a random walkable cell
  in the room, walk there, pause 1–2.5 s, repeat. NPCs at stations
  sit motionless until a player swaps in (NPC-station-seating itself
  is deferred — currently no NPC starts at a station).
- NPCs count as full crew: take fire damage, vent damage, station
  damage if at a console, can be healed by mashing A nearby, and
  factor into the all-crew-down loss latch.

### Phase 9e — boarder enemies + NPC combat + pathfinding  `[ ]`
- New "boarder" enemy type that spawns inside the bridge interior
  (not in the corner viewport). Open question: how do boarders
  spawn? candidates: (a) periodic timer, (b) on shield-face-down
  events, (c) scripted in mission def, (d) on capital-ship close
  approach.
- NPCs run toward boarders and shoot them with handheld weapons
  (also new — characters firing projectiles inside the bridge).
- After combat, NPCs return to their assigned home rooms.
- Pathfinding: A* on the level grid OR steering-with-wall-avoidance.
  TBD — A* fits the gridded bridge layout, but adds enough code that
  a steering fallback might be enough for the bridge's small size.
- Open question: do players also use handheld weapons inside the
  bridge, or is shooting still strictly via the weapons-console
  ship-mounted phasers?

## Shields — 6-face model (Phase 5)

Replaces the single `shield_hp` value with six independent face buffers,
one per ship-local axis half:

| Face index | Local axis | Colloquial name |
|------------|------------|-----------------|
| 0          | +Z         | Forward / bow   |
| 1          | -Z         | Aft / stern     |
| 2          | +X         | Port            |
| 3          | -X         | Starboard       |
| 4          | +Y         | Dorsal / top    |
| 5          | -Y         | Ventral / belly |

Hit-angle resolution: the ship-local impact direction is normalised, then
the dominant component (largest |x|, |y|, or |z|) selects the face.
`station` damage routing (Phase 2) keeps using the existing four-cardinal
mapping; the **shield** face is independent — a hit can drain the dorsal
shield while still being routed to the Helm subsystem.

Each face has its own current / max value. Total shield capacity is split
across the six faces; the distribution is what science manages. Default
even split = capacity / 6 per face. Science console UI: a 3D cube wireframe
or a 6-cell grid; science officer pumps energy from one face to another
the same way engineering moves power between channels.

Damage routing: the hit face absorbs first, overflow bleeds through to
hull + station per the existing rules.

## Weapons — forward firing arc (Phase 5)

Today phasers/torpedoes accept `aim_yaw` and fire from the nose at any
yaw. Tighten this to a forward cone (default ±25°). The weapons console
clamps `aim_yaw` so the gunner can't aim outside the arc. Helm has to
turn the ship to bring a target into the arc.

## Subsystem repair (Phase 6)

Damaged stations don't come back without a deliberate action. The
engineer initiates repair from their console with a held-A interaction
on a per-station target (cycle target with stick L/R, hold A to repair).
While repairing, engineering can't redistribute power. Repair rate is
~5 HP/sec at the reference power allocation, scaled by engineering's own
HP (a half-broken engineering can only half-repair).

A repaired station resumes normal function the moment HP > 0.

## Fire suppression (Phase 7)

Rooms can catch fire when they take repeated subsystem damage. Fire is a
persistent room-state hazard:

- Trigger: any station that takes ≥ N damage in a short window (e.g. 3
  hits within 3 seconds) ignites fire in the room containing that station.
- Effect while burning: every officer standing in the room takes
  fire-tick damage (~2 HP/sec). The fire also slowly damages the
  station's HP further.
- Vent option: opening the airlock of a burning room sucks the fire out
  instantly but exposes anyone inside to vacuum — they take large one-
  shot damage (~30 HP). Engineering controls the vent.
- Fire extinguisher: each room has a red-cylinder-with-black-cap
  pickup. An officer walks up, presses A to grab it, walks to the fire,
  presses A again to discharge. One discharge clears the room's fire
  but consumes the extinguisher. Extinguishers respawn at their fixed
  spawn points each new mission, not within a run.

Asset: a procedural extinguisher mesh under `tools/gen-extinguisher-c.py`
following the same baked-OBJ convention as `tools/gen-character.py`.
Red cylinder, black cap, ~6 units tall.

## Missions & win condition (Phase 8)

Add a mission-select screen between lobby and game. Missions define:
- Enemy spawn list (count, type, capital ship presence)
- Win condition (kill all enemies in list)
- Failure conditions (existing: hull destroyed, crew incapacitated)

### Instant Action

Combat simulator. Pick from a small menu:
- 1 fighter (warm-up)
- 2 fighters (default)
- 3 fighters
- 1 capital ship (red, ~3× fighter scale, more HP, slower fire rate)

Win: all enemies destroyed. Win screen + return-to-mission-select.

### Simple Mission (first scripted mission)

Fight 3 fighters OR 1 capital ship. Same rules as Instant Action's bigger
options, but framed as a story beat — establishes the campaign opening.

## Campaign (documentation only — not yet implemented)

A linear story campaign. Each leg is a separate mission with its own
spawn list and win condition; the player progresses through them in
order from a campaign-select screen.

### Leg 1 — The missing fleet

Briefing: a fleet of ships went missing in this sector. Investigate.

The player approaches an abandoned wreck. Combat-light: maybe a few
debris fields, no live enemies yet. The crew has to walk to the
teleporter in the engineering bay and beam over to the wreck.

### Leg 2 — Aboard the wreck

Boarding action. The wreck's interior has hostile alien remnants
running around. The player crew has to:
- Shoot the boarders (handheld combat — new mechanic)
- Find the captain's log (interactable on the bridge)
- Read the log: the original crew was captured and brought to a planet
  to be used as hostages by a rogue faction demanding ships and weapons.

### Leg 3 — The trap

Beam back to the player ship. As they jump out, ambushed: the rogue
faction has plenty of ships and weapons (the demand was a feint).
Combat: fight off a wave of fighters. Some board the ship — repel them
in handheld combat in the bridge interior.

### Leg 4 — On the planet

Beam down to the planet. Ambushed again — fight off waves of foot
soldiers. Scan corpses for clues; reveals the hostages aren't on this
planet — they're held in space, on satellites orbiting another planet.

### Leg 5 — Satellite scan

Travel to the next planet. Scan satellites in orbit while fighting off
more aliens (probably interceptors trying to stop the scans). Locate
the hostage ships.

### Leg 6 — Final boss

Boss fight: the flagship of the rogue faction's fleet. Larger than a
capital ship, multiple weapon emplacements, multiple shield faces that
need to be cracked sequentially. Win the fight, recover the hostages.

Mechanics introduced incrementally per leg so the campaign also doubles
as the tutorial: leg 1 = navigation only, leg 2 = handheld combat, leg 3
= ship + boarding, leg 4 = on-foot wave combat, leg 5 = scanning
minigame, leg 6 = boss.

## Open questions

- **Bridge directional layout**: which world direction maps to which
  station for the hit-angle table. Best to read off the level data once
  Phase 2 starts.
- **Station "disabled" behavior**: at 0 station HP, does the station fully
  stop working, or just degrade? I lean toward fully stop until repaired.
- **Repair mechanic**: do stations come back from 0 HP? Probably yes via a
  long mash-A-at-the-console interaction, mirroring officer heal. Defer
  decision to Phase 2.
- **Difficulty scaling**: enemy count, fire rate, damage per hit. Tune
  during Phase 1 playtesting; record final numbers here.

## Tuning numbers (to fill in as we playtest)

| Value                          | Initial guess | Final |
|--------------------------------|---------------|-------|
| Hull HP                        | 100           | —     |
| Shield max (33% alloc)         | 60            | —     |
| Shield regen / sec (33% alloc) | 4             | —     |
| Weapons heat max               | 100           | —     |
| Heat per phaser shot           | 8             | —     |
| Heat per torpedo shot          | 25            | —     |
| Heat dissipation / sec         | 15            | —     |
| Station HP                     | 50            | —     |
| Officer HP                     | 100           | —     |
| Heal per A-press               | 8             | —     |
| Enemy phaser damage            | 5             | —     |
| Enemy fire interval (per ship) | 2.0 s         | —     |
| Enemy orbit radius             | 80 u          | —     |
| Enemy retreat duration         | 10 s          | —     |
