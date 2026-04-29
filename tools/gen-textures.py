#!/usr/bin/env python3
"""
Procedural texture generator for star-crew-64.

Produces 32x32 RGBA PNGs into assets/textures/:
  - hallway.png       : industrial corrugated floor (deck plating)
  - room.png          : crew quarters floor panel
  - airlock.png       : yellow/black hazard stripes
  - hallway_wall.png  : industrial corridor wall (vertical paneling, conduit)
  - room_wall.png     : crew quarters wall (warm composite paneling)
  - character.png     : simple humanoid color atlas (head/torso/limbs regions)

Wall textures are mapped to vertical quads (V axis = world height); the
trim strips near V=0 and V=SIZE-1 read as floor-level baseboard and
ceiling cornice respectively when applied to a TILE_SIZE x WALL_HEIGHT
quad.

No third-party deps — writes PNGs directly via zlib + struct.
N64 textures are tiny; 32x32 is the size cap we've chosen.
"""

import os
import struct
import zlib

SIZE = 32
OUT = os.path.join(os.path.dirname(__file__), "..", "assets", "textures")


def write_png(path: str, pixels: list[tuple[int, int, int, int]], size: int = SIZE) -> None:
    """Write a square RGBA PNG. `pixels` is a flat row-major list of length size*size."""
    assert len(pixels) == size * size

    raw = bytearray()
    for y in range(size):
        raw.append(0)  # filter: None
        for x in range(size):
            r, g, b, a = pixels[y * size + x]
            raw.extend((r & 255, g & 255, b & 255, a & 255))

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (
            struct.pack(">I", len(data))
            + tag
            + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        )

    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)  # 8-bit RGBA
    idat = zlib.compress(bytes(raw), 9)

    with open(path, "wb") as f:
        f.write(sig)
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", idat))
        f.write(chunk(b"IEND", b""))


def make_hallway() -> list[tuple[int, int, int, int]]:
    """Dark deck plating with a brighter center stripe and rivet dots."""
    base = (55, 60, 72, 255)
    stripe = (90, 100, 120, 255)
    rivet = (25, 28, 35, 255)
    hl = (120, 135, 160, 255)

    px = [base] * (SIZE * SIZE)
    for y in range(SIZE):
        for x in range(SIZE):
            # Corrugation: horizontal bands every 4 rows
            if y % 4 == 0:
                px[y * SIZE + x] = rivet
            elif y % 4 == 1:
                px[y * SIZE + x] = hl
            # Center travel stripe
            if 13 <= x <= 18:
                px[y * SIZE + x] = stripe if y % 4 != 0 else rivet
            # Rivets along edges
            if (x in (1, 30)) and (y % 6 == 3):
                px[y * SIZE + x] = rivet
    return px


def make_room() -> list[tuple[int, int, int, int]]:
    """Crew-quarters floor: 2 panels with seams, subtle noise."""
    base = (80, 78, 72, 255)
    seam = (45, 43, 40, 255)
    light = (110, 108, 100, 255)

    px = [base] * (SIZE * SIZE)
    for y in range(SIZE):
        for x in range(SIZE):
            # Panel seams at the midpoints
            if x == 0 or x == 15 or x == 16 or x == 31 or y == 0 or y == 15 or y == 16 or y == 31:
                px[y * SIZE + x] = seam
            # Deterministic "noise" highlights (checkerboard-ish)
            elif ((x * 7 + y * 13) % 23) < 2:
                px[y * SIZE + x] = light
    return px


def make_airlock() -> list[tuple[int, int, int, int]]:
    """Hazard stripes: diagonal yellow/black bands, warning border."""
    yellow = (230, 195, 40, 255)
    black = (25, 22, 18, 255)
    border = (200, 40, 40, 255)

    px = [black] * (SIZE * SIZE)
    for y in range(SIZE):
        for x in range(SIZE):
            # Diagonal bands, 4 px wide
            if ((x + y) // 4) % 2 == 0:
                px[y * SIZE + x] = yellow
            else:
                px[y * SIZE + x] = black
            # Red warning border
            if x < 2 or x > 29 or y < 2 or y > 29:
                px[y * SIZE + x] = border
    return px


def make_hallway_wall() -> list[tuple[int, int, int, int]]:
    """
    Industrial corridor wall: dark steel paneling with vertical recessed
    channels every 8 px, a horizontal conduit strip near the top, and a
    baseboard at the bottom. Reads correctly when V=0 is the floor.
    """
    base       = (62, 70, 84, 255)     # cool steel
    channel    = (38, 44, 54, 255)     # recessed panel seam
    highlight  = (110, 122, 140, 255)  # specular edge of the channel
    baseboard  = (28, 32, 40, 255)
    cornice    = (90, 100, 118, 255)
    conduit    = (180, 140, 60, 255)   # amber pipe accent
    rivet      = (24, 26, 32, 255)

    px = [base] * (SIZE * SIZE)
    for y in range(SIZE):
        for x in range(SIZE):
            # Vertical paneling: a 2 px channel every 8 px.
            xm = x % 8
            if xm == 0:
                c = channel
            elif xm == 1:
                c = highlight
            else:
                c = base
            # Horizontal cornice band near the top (y close to SIZE-1 = ceiling).
            if y >= SIZE - 3:
                c = cornice if y == SIZE - 2 else channel
            # Conduit pipe just below the cornice.
            elif SIZE - 7 <= y <= SIZE - 5:
                c = conduit if y == SIZE - 6 else (140, 110, 50, 255)
            # Baseboard at the floor (y = 0..2).
            elif y <= 2:
                c = baseboard
            # Occasional rivets along mid-height.
            elif y in (12, 20) and x % 8 == 4:
                c = rivet
            px[y * SIZE + x] = c
    return px


def make_room_wall() -> list[tuple[int, int, int, int]]:
    """
    Crew quarters wall: warm composite paneling with horizontal seams
    every 6 px, a wood-like base trim, and a soft picture rail near the
    top. Same V=0 = floor convention as hallway_wall.
    """
    base      = (140, 118, 92, 255)    # warm tan composite
    seam      = (90, 72, 55, 255)
    highlight = (180, 158, 130, 255)
    baseboard = (60, 48, 36, 255)
    rail      = (100, 80, 60, 255)
    accent    = (200, 175, 140, 255)

    px = [base] * (SIZE * SIZE)
    for y in range(SIZE):
        for x in range(SIZE):
            c = base
            # Horizontal panel seams every 6 px (1 px dark + 1 px highlight).
            ym = y % 6
            if ym == 0:
                c = seam
            elif ym == 1:
                c = highlight
            # Baseboard at the floor.
            if y <= 2:
                c = baseboard
            elif y == 3:
                c = seam
            # Picture-rail trim near the ceiling.
            elif y == SIZE - 4:
                c = rail
            elif y >= SIZE - 3:
                c = accent if y == SIZE - 2 else seam
            # Subtle vertical wood-grain ticks.
            elif (x * 11 + y * 5) % 29 == 0:
                c = highlight
            px[y * SIZE + x] = c
    return px


def make_bridge_panel() -> list[tuple[int, int, int, int]]:
    """
    Bridge control panel atlas (32x32). The panel mesh samples three regions
    of this texture to dress its different faces:
      band 0 (y  0- 7): glowing main display — green grid + scanline
      band 1 (y  8-15): button rows — red/yellow/green caps with bezels
      band 2 (y 16-23): metal console body — dark brushed steel + seams
      band 3 (y 24-31): impulse throttle slot — dark groove + amber edge
    """
    bezel    = (28, 30, 38, 255)
    metal    = (62, 66, 78, 255)
    metal_hl = (110, 118, 132, 255)
    metal_dk = (38, 42, 50, 255)
    screen   = (12, 35, 22, 255)
    grid     = (40, 180, 110, 255)
    glow     = (160, 240, 200, 255)
    btn_red    = (220, 60, 50, 255)
    btn_red_hl = (255, 130, 110, 255)
    btn_yel    = (240, 200, 60, 255)
    btn_yel_hl = (255, 235, 130, 255)
    btn_grn    = (60, 200, 110, 255)
    btn_grn_hl = (140, 240, 170, 255)
    amber      = (230, 165, 50, 255)
    amber_dk   = (130, 90, 30, 255)

    px = [metal] * (SIZE * SIZE)
    for y in range(SIZE):
        for x in range(SIZE):
            if y < 8:
                # Main display: dark green base with grid lines + horizontal scanlines.
                c = screen
                if x % 4 == 0 or (y % 4 == 0):
                    c = grid
                if y == 3 and 4 <= x <= 27:
                    c = glow  # readout highlight line
                if x < 1 or x > 30 or y == 0 or y == 7:
                    c = bezel
            elif y < 16:
                # Button row: 4 large round-ish caps across, bezel between them.
                cell = x // 8
                inset_x = x % 8
                inset_y = (y - 8)
                # bezel between cells (x % 8 == 0 or 7) and at top/bottom of band
                if inset_x == 0 or inset_x == 7 or inset_y == 0 or inset_y == 7:
                    c = bezel
                else:
                    base, hl = [
                        (btn_red, btn_red_hl),
                        (btn_yel, btn_yel_hl),
                        (btn_grn, btn_grn_hl),
                        (btn_red, btn_red_hl),
                    ][cell % 4]
                    # round-ish: corners get bezel
                    if (inset_x in (1, 6)) and (inset_y in (1, 6)):
                        c = bezel
                    elif inset_x in (2, 3) and inset_y in (2, 3):
                        c = hl
                    else:
                        c = base
            elif y < 24:
                # Console body: brushed metal w/ horizontal seam at top + bottom.
                yb = y - 16
                if yb == 0 or yb == 7:
                    c = metal_dk
                elif (x * 3 + yb) % 11 == 0:
                    c = metal_hl
                else:
                    c = metal
                # vertical screw heads
                if x in (3, 28) and yb in (3, 4):
                    c = bezel
            else:
                # Throttle slot: dark recess with amber illuminated edges.
                yb = y - 24
                slot_top = 1
                slot_bot = 6
                if yb in (slot_top, slot_bot):
                    c = amber
                elif slot_top < yb < slot_bot:
                    if x in (1, 30):
                        c = amber_dk
                    elif (x % 4) == 0:
                        c = amber_dk
                    else:
                        c = bezel
                else:
                    c = metal_dk
            px[y * SIZE + x] = c
    return px


def make_weapons_panel() -> list[tuple[int, int, int, int]]:
    """
    Weapons console atlas (32x32). Same band layout as bridge_panel.png so the
    weapons-panel mesh can use the identical UV scheme:
      band 0 (y  0- 7): targeting display — red CRT with crosshair + scanline
      band 1 (y  8-15): trigger buttons — red caps with bezels
      band 2 (y 16-23): metal console body — gun-metal grey w/ angled trim
      band 3 (y 24-31): warning chevron strip — red/black hazard chevrons
    Visually distinct from the bridge panel (red theme, targeting reticle)
    so the player can tell the two consoles apart at a glance.
    """
    bezel    = (28, 30, 38, 255)
    metal    = (62, 60, 64, 255)
    metal_hl = (110, 108, 112, 255)
    metal_dk = (38, 36, 40, 255)
    screen   = (35, 8, 12, 255)
    grid     = (200, 50, 50, 255)
    glow     = (255, 200, 180, 255)
    btn_red    = (220, 50, 40, 255)
    btn_red_hl = (255, 130, 100, 255)
    btn_dk     = (140, 25, 20, 255)
    chev_red   = (210, 40, 35, 255)
    chev_dk    = (40, 8, 8, 255)

    px = [metal] * (SIZE * SIZE)
    for y in range(SIZE):
        for x in range(SIZE):
            if y < 8:
                # Targeting display: dark-red base with grid + a centered
                # crosshair so it visually reads as "weapons targeting".
                c = screen
                if x % 4 == 0 or (y % 4 == 0):
                    c = grid
                # Cross-hair: center column + center row.
                if x in (15, 16):
                    c = glow
                if y in (3, 4):
                    c = glow
                # Border bezel.
                if x < 1 or x > 30 or y == 0 or y == 7:
                    c = bezel
            elif y < 16:
                # Trigger button row: 4 large red caps across.
                cell = x // 8
                inset_x = x % 8
                inset_y = (y - 8)
                if inset_x == 0 or inset_x == 7 or inset_y == 0 or inset_y == 7:
                    c = bezel
                else:
                    base = btn_red
                    hl   = btn_red_hl
                    dk   = btn_dk
                    if (inset_x in (1, 6)) and (inset_y in (1, 6)):
                        c = bezel
                    elif inset_x in (2, 3) and inset_y in (2, 3):
                        c = hl
                    elif (cell % 2) == 0:
                        c = base
                    else:
                        c = dk
            elif y < 24:
                # Console body: gun-metal grey with diagonal trim + screws.
                yb = y - 16
                if yb == 0 or yb == 7:
                    c = metal_dk
                elif (x + yb) % 9 == 0:
                    c = metal_hl
                else:
                    c = metal
                if x in (3, 28) and yb in (3, 4):
                    c = bezel
            else:
                # Warning chevron strip — red and black diagonal chevrons.
                yb = y - 24
                # Chevron pattern: triangular stripes 4 px wide.
                if ((x + yb) // 4) % 2 == 0:
                    c = chev_red
                else:
                    c = chev_dk
                # Top/bottom seam.
                if yb in (0, 7):
                    c = metal_dk
            px[y * SIZE + x] = c
    return px


def make_engineering_panel() -> list[tuple[int, int, int, int]]:
    """
    Engineering console atlas (32x32). Amber theme so the player can tell it
    apart from helm (green) and weapons (red). Same band layout as the other
    panels so the weapons-panel mesh can be reused here.
      band 0 (y  0- 7): power-grid display — amber bars on dark
      band 1 (y  8-15): system buttons — amber caps with bezels
      band 2 (y 16-23): metal console body
      band 3 (y 24-31): caution stripe — amber/black diagonals
    """
    bezel    = (30, 28, 24, 255)
    metal    = (78, 70, 58, 255)
    metal_hl = (130, 118, 95, 255)
    metal_dk = (45, 40, 32, 255)
    screen   = (28, 18, 6, 255)
    bar_lo   = (130, 80, 20, 255)
    bar_hi   = (255, 180, 60, 255)
    glow     = (255, 220, 140, 255)
    btn      = (220, 140, 30, 255)
    btn_hl   = (255, 200, 90, 255)
    btn_dk   = (150, 90, 15, 255)
    cau_amb  = (240, 175, 35, 255)
    cau_dk   = (40, 30, 8, 255)

    px = [metal] * (SIZE * SIZE)
    for y in range(SIZE):
        for x in range(SIZE):
            if y < 8:
                # Power grid: vertical amber bars with varying heights.
                c = screen
                # 6 bars across (each ~5px wide). Each bar fills from the
                # bottom up to a deterministic height so the readout looks
                # like a power-distribution chart.
                bar = x // 5
                col_in_bar = x % 5
                heights = [4, 6, 3, 5, 6, 2]
                if 1 <= col_in_bar <= 3 and bar < 6:
                    h = heights[bar]
                    if (7 - y) <= h:
                        c = bar_hi if (7 - y) == h else bar_lo
                if x < 1 or x > 30 or y == 0 or y == 7:
                    c = bezel
                if y == 1 and 4 <= x <= 27:
                    c = glow
            elif y < 16:
                cell = x // 8
                inset_x = x % 8
                inset_y = (y - 8)
                if inset_x == 0 or inset_x == 7 or inset_y == 0 or inset_y == 7:
                    c = bezel
                else:
                    if (inset_x in (1, 6)) and (inset_y in (1, 6)):
                        c = bezel
                    elif inset_x in (2, 3) and inset_y in (2, 3):
                        c = btn_hl
                    elif (cell % 2) == 0:
                        c = btn
                    else:
                        c = btn_dk
            elif y < 24:
                yb = y - 16
                if yb == 0 or yb == 7:
                    c = metal_dk
                elif (x + yb) % 9 == 0:
                    c = metal_hl
                else:
                    c = metal
                if x in (3, 28) and yb in (3, 4):
                    c = bezel
            else:
                yb = y - 24
                if ((x + yb) // 4) % 2 == 0:
                    c = cau_amb
                else:
                    c = cau_dk
                if yb in (0, 7):
                    c = metal_dk
            px[y * SIZE + x] = c
    return px


def make_science_panel() -> list[tuple[int, int, int, int]]:
    """
    Science console atlas (32x32). Cyan/blue theme — visually distinct from
    helm (green), weapons (red), engineering (amber). Same band layout.
      band 0 (y  0- 7): shield-status display — cyan grid + arc readout
      band 1 (y  8-15): touch-pad buttons — cyan caps
      band 2 (y 16-23): metal console body — blue-tinted grey
      band 3 (y 24-31): cyan trim stripe
    """
    bezel    = (24, 28, 38, 255)
    metal    = (58, 64, 78, 255)
    metal_hl = (110, 122, 145, 255)
    metal_dk = (32, 36, 48, 255)
    screen   = (8, 18, 32, 255)
    grid     = (60, 160, 220, 255)
    glow     = (180, 240, 255, 255)
    arc_dim  = (40, 110, 180, 255)
    btn      = (60, 150, 220, 255)
    btn_hl   = (170, 220, 255, 255)
    btn_dk   = (30, 80, 130, 255)
    trim     = (90, 180, 230, 255)
    trim_dk  = (20, 50, 80, 255)

    px = [metal] * (SIZE * SIZE)
    for y in range(SIZE):
        for x in range(SIZE):
            if y < 8:
                c = screen
                if x % 4 == 0 or (y % 4 == 0):
                    c = grid
                # Shield-arc readout: a wide arc curve in the upper band.
                # Approximate with a few pixels along an ellipse y = 6 - r.
                cx = 16
                cy = 7
                dx = x - cx
                dy = y - cy
                r2 = dx * dx + (dy * dy) * 4
                if 30 <= r2 <= 45:
                    c = glow
                elif 46 <= r2 <= 70:
                    c = arc_dim
                if x < 1 or x > 30 or y == 0 or y == 7:
                    c = bezel
            elif y < 16:
                cell = x // 8
                inset_x = x % 8
                inset_y = (y - 8)
                if inset_x == 0 or inset_x == 7 or inset_y == 0 or inset_y == 7:
                    c = bezel
                else:
                    if (inset_x in (1, 6)) and (inset_y in (1, 6)):
                        c = bezel
                    elif inset_x in (2, 3) and inset_y in (2, 3):
                        c = btn_hl
                    elif (cell % 2) == 0:
                        c = btn
                    else:
                        c = btn_dk
            elif y < 24:
                yb = y - 16
                if yb == 0 or yb == 7:
                    c = metal_dk
                elif (x + yb) % 9 == 0:
                    c = metal_hl
                else:
                    c = metal
                if x in (3, 28) and yb in (3, 4):
                    c = bezel
            else:
                yb = y - 24
                if yb in (0, 7):
                    c = metal_dk
                elif (x + yb) % 6 == 0:
                    c = trim
                elif (x + yb) % 6 == 3:
                    c = trim_dk
                else:
                    c = bezel
            px[y * SIZE + x] = c
    return px


def make_ship() -> list[tuple[int, int, int, int]]:
    """
    Ship hull atlas (32x32). Two bands:
      band 0 (y  0-15): top-side hull — pale grey panels + cockpit accent
      band 1 (y 16-31): underside / engines — darker plating + amber thruster
    The procedural ship model UVs sample whichever band matches the face.
    """
    hull       = (180, 188, 200, 255)
    hull_hl    = (220, 226, 235, 255)
    hull_dk    = (110, 118, 132, 255)
    seam       = (60, 65, 75, 255)
    cockpit    = (40, 110, 200, 255)
    cockpit_hl = (130, 200, 255, 255)
    under      = (70, 75, 88, 255)
    under_dk   = (40, 44, 52, 255)
    engine     = (255, 170, 60, 255)
    engine_hl  = (255, 230, 160, 255)

    px = [hull] * (SIZE * SIZE)
    for y in range(SIZE):
        for x in range(SIZE):
            if y < 16:
                c = hull
                # plate seams
                if x in (0, 15, 16, 31) or y in (0, 15):
                    c = seam
                elif (x + y) % 9 == 0:
                    c = hull_hl
                elif (x * 5 + y * 3) % 13 == 0:
                    c = hull_dk
                # cockpit dome — a small bright patch in the upper-middle
                if 12 <= x <= 19 and 4 <= y <= 9:
                    c = cockpit
                    if 14 <= x <= 17 and 5 <= y <= 7:
                        c = cockpit_hl
            else:
                c = under
                yb = y - 16
                if x in (0, 15, 16, 31) or yb in (0, 15):
                    c = under_dk
                elif (x * 7 + yb * 11) % 17 == 0:
                    c = under_dk
                # twin engine glow patches at the back
                if (3 <= x <= 8 or 23 <= x <= 28) and 10 <= yb <= 13:
                    c = engine
                    if (4 <= x <= 7 or 24 <= x <= 27) and 11 <= yb <= 12:
                        c = engine_hl
            px[y * SIZE + x] = c
    return px


def make_character() -> list[tuple[int, int, int, int]]:
    """
    Character UV atlas (32x32). Laid out as 4 horizontal bands, each 32x8:
      row 0 (y  0- 7): head/skin
      row 1 (y  8-15): torso/shirt
      row 2 (y 16-23): arms/sleeves
      row 3 (y 24-31): legs/pants + boots strip
    Model UVs can be assigned to sample whichever band matches the body part.
    """
    skin = (215, 175, 140, 255)
    skin_dk = (160, 120, 90, 255)
    shirt = (60, 95, 160, 255)
    shirt_hl = (95, 135, 200, 255)
    pants = (50, 55, 65, 255)
    pants_hl = (80, 85, 95, 255)
    boots = (30, 30, 35, 255)

    px = [(0, 0, 0, 255)] * (SIZE * SIZE)
    for y in range(SIZE):
        for x in range(SIZE):
            if y < 8:
                c = skin_dk if (x + y) % 6 == 0 else skin
            elif y < 16:
                c = shirt_hl if (x * 3 + y) % 7 == 0 else shirt
            elif y < 24:
                c = shirt_hl if x % 8 < 2 else shirt  # sleeve with cuff accent
            else:
                if y >= 29:
                    c = boots
                else:
                    c = pants_hl if x % 5 == 0 else pants
            px[y * SIZE + x] = c
    return px


STAR_SIZE = 8  # small billboards


def _make_star_sprite(core, halo) -> list[tuple[int, int, int, int]]:
    """
    8×8 star billboard: a single bright core at (3,3) with a soft 4-neighbour
    halo, everything else transparent-looking (black with alpha=0). Combining
    multiple scattered star quads in 3D gives the background its twinkle.
    """
    bg = (0, 0, 0, 0)  # zero alpha so the dark corners don't punch a hole
    px = [bg] * (STAR_SIZE * STAR_SIZE)
    cx, cy = 3, 3

    px[cy * STAR_SIZE + cx] = core
    # Plus-shaped halo; corners stay transparent so the sprite reads as round.
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        x, y = cx + dx, cy + dy
        if 0 <= x < STAR_SIZE and 0 <= y < STAR_SIZE:
            px[y * STAR_SIZE + x] = halo
    return px


# ---- Button prompt sprites (16×16, transparent background) ---------------
# Each sprite is a small icon hovered above a player when they're in range
# of a console. The four icons cover the whole gameplay vocabulary:
#   - prompt_a / prompt_b / prompt_z : N64 button labels in their canonical
#     colors (A=blue, B=green, Z=grey/black) so muscle memory still works.
#   - prompt_stick : a circular d-pad/stick glyph for "use the stick".
# All four share the same canvas size + circular shape so they hover at a
# consistent screen size when stacked or shown alone.

PROMPT_SIZE = 16


def _make_button_sprite(label: str, fill: tuple, outline: tuple,
                        text: tuple) -> list[tuple[int, int, int, int]]:
    """16×16 round button with a single ASCII glyph centered. Background is
    fully transparent so the billboard reads as a floating disk."""
    bg = (0, 0, 0, 0)
    px = [bg] * (PROMPT_SIZE * PROMPT_SIZE)
    cx = cy = 7.5  # center between pixels 7 and 8 so the disk is symmetric
    r_outer = 7.0
    r_inner = 5.6

    for y in range(PROMPT_SIZE):
        for x in range(PROMPT_SIZE):
            dx = x - cx
            dy = y - cy
            d = (dx * dx + dy * dy) ** 0.5
            if d <= r_inner:
                px[y * PROMPT_SIZE + x] = fill
            elif d <= r_outer:
                px[y * PROMPT_SIZE + x] = outline

    # 5x5 bitmap glyphs centered at (8, 8). Just A / B / Z — the only
    # labels we need.
    glyphs = {
        "A": [
            "..#..",
            ".#.#.",
            ".###.",
            ".#.#.",
            ".#.#.",
        ],
        "B": [
            ".##..",
            ".#.#.",
            ".##..",
            ".#.#.",
            ".##..",
        ],
        "Z": [
            ".###.",
            "...#.",
            "..#..",
            ".#...",
            ".###.",
        ],
    }
    glyph = glyphs.get(label.upper())
    if glyph is not None:
        # Place glyph so its 5x5 footprint is centered on (8, 8).
        gx0 = 5
        gy0 = 5
        for gy, row in enumerate(glyph):
            for gx, ch in enumerate(row):
                if ch == "#":
                    px[(gy0 + gy) * PROMPT_SIZE + (gx0 + gx)] = text
    return px


def make_prompt_a():
    return _make_button_sprite("A",
        fill=(70, 130, 220, 255),     # N64 blue
        outline=(20, 60, 120, 255),
        text=(255, 255, 255, 255))


def make_prompt_b():
    return _make_button_sprite("B",
        fill=(70, 200, 110, 255),     # N64 green
        outline=(20, 100, 50, 255),
        text=(255, 255, 255, 255))


def make_prompt_z():
    return _make_button_sprite("Z",
        fill=(80, 80, 88, 255),       # dark trigger grey
        outline=(20, 20, 24, 255),
        text=(240, 240, 250, 255))


def make_prompt_start() -> list[tuple[int, int, int, int]]:
    """Start-button glyph: red-orange disk with a small house-shape /
    'play' triangle, used for the lobby launch prompt."""
    fill    = (210, 60, 50, 255)
    outline = (90, 25, 20, 255)
    text    = (255, 255, 255, 255)
    bg = (0, 0, 0, 0)
    px = [bg] * (PROMPT_SIZE * PROMPT_SIZE)
    cx = cy = 7.5
    r_outer = 7.0
    r_inner = 5.6
    for y in range(PROMPT_SIZE):
        for x in range(PROMPT_SIZE):
            dx = x - cx
            dy = y - cy
            d = (dx * dx + dy * dy) ** 0.5
            if d <= r_inner:
                px[y * PROMPT_SIZE + x] = fill
            elif d <= r_outer:
                px[y * PROMPT_SIZE + x] = outline
    # Centered "play" triangle pointing right.
    tri = [
        "..##.....",
        "..###....",
        "..####...",
        "..#####..",
        "..####...",
        "..###....",
        "..##.....",
    ]
    gx0 = 4
    gy0 = 4
    for gy, row in enumerate(tri):
        for gx, ch in enumerate(row):
            if ch == "#":
                px[(gy0 + gy) * PROMPT_SIZE + (gx0 + gx)] = text
    return px


def make_prompt_stick() -> list[tuple[int, int, int, int]]:
    """Stick glyph: a grey ring with a 4-direction arrow rosette inside.
    Reads as 'use the stick' at the small billboard size."""
    bg = (0, 0, 0, 0)
    fill    = (200, 200, 210, 255)
    outline = (60,  60,  70,  255)
    arrow   = (40,  40,  50,  255)
    px = [bg] * (PROMPT_SIZE * PROMPT_SIZE)
    cx = cy = 7.5
    r_outer = 7.0
    r_inner = 5.6
    r_dot   = 1.6
    for y in range(PROMPT_SIZE):
        for x in range(PROMPT_SIZE):
            dx = x - cx
            dy = y - cy
            d2 = dx * dx + dy * dy
            d = d2 ** 0.5
            if d <= r_dot:
                px[y * PROMPT_SIZE + x] = arrow
            elif d <= r_inner:
                px[y * PROMPT_SIZE + x] = fill
            elif d <= r_outer:
                px[y * PROMPT_SIZE + x] = outline
    # Tiny arrow tips at the four cardinals — pixel triangles 1px deep.
    for ax, ay in ((8, 1), (8, 14), (1, 8), (14, 8)):
        px[ay * PROMPT_SIZE + ax] = arrow
    # +1 pixel wider at base so the tips aren't single-pixel specks.
    for ax, ay in ((7, 2), (9, 2), (7, 13), (9, 13),
                   (2, 7), (2, 9), (13, 7), (13, 9)):
        px[ay * PROMPT_SIZE + ax] = arrow
    return px


def make_star_white():
    return _make_star_sprite(core=(255, 255, 250, 255), halo=(160, 160, 180, 255))


def make_star_blue():
    return _make_star_sprite(core=(180, 210, 255, 255), halo=(70, 110, 200, 255))


def make_star_yellow():
    return _make_star_sprite(core=(255, 230, 140, 255), halo=(190, 150,  60, 255))


def make_star_red():
    return _make_star_sprite(core=(255, 140, 120, 255), halo=(170,  60,  50, 255))


def main() -> None:
    os.makedirs(OUT, exist_ok=True)
    # 32×32 level/character textures.
    big_outputs = {
        "hallway.png": make_hallway(),
        "room.png": make_room(),
        "airlock.png": make_airlock(),
        "hallway_wall.png": make_hallway_wall(),
        "room_wall.png": make_room_wall(),
        "character.png": make_character(),
        "bridge_panel.png": make_bridge_panel(),
        "weapons_panel.png": make_weapons_panel(),
        "engineering_panel.png": make_engineering_panel(),
        "science_panel.png": make_science_panel(),
        "ship.png": make_ship(),
    }
    for name, pixels in big_outputs.items():
        path = os.path.join(OUT, name)
        write_png(path, pixels, SIZE)
        print(f"  wrote {path}")

    # 8×8 billboard star sprites.
    small_outputs = {
        "star_white.png":  make_star_white(),
        "star_blue.png":   make_star_blue(),
        "star_yellow.png": make_star_yellow(),
        "star_red.png":    make_star_red(),
    }
    for name, pixels in small_outputs.items():
        path = os.path.join(OUT, name)
        write_png(path, pixels, STAR_SIZE)
        print(f"  wrote {path}")

    # 16×16 console-prompt button glyphs (transparent background).
    prompt_outputs = {
        "prompt_a.png":     make_prompt_a(),
        "prompt_b.png":     make_prompt_b(),
        "prompt_z.png":     make_prompt_z(),
        "prompt_start.png": make_prompt_start(),
        "prompt_stick.png": make_prompt_stick(),
    }
    for name, pixels in prompt_outputs.items():
        path = os.path.join(OUT, name)
        write_png(path, pixels, PROMPT_SIZE)
        print(f"  wrote {path}")

    # Clean up old single-sheet star textures that have been replaced by the
    # individual 8×8 billboard sprites above.
    for stale_name in ("stars.png", "starfield.png"):
        stale = os.path.join(OUT, stale_name)
        if os.path.exists(stale):
            os.remove(stale)
            print(f"  removed {stale}")


if __name__ == "__main__":
    main()
