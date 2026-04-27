#!/usr/bin/env python3
"""
Generate the weapons-console mesh.

Same authoring convention as tools/gen-bridge-panel.py (per CLAUDE.md: OBJ
is the canonical source-of-truth, JSON sidecar is for the model editor, the
matching C header is emitted by tools/gen-weapons-panel-c.py).

The weapons console is visually distinct from the bridge panel: an upright
narrow box (think gunner's seat) with a flat targeting screen tilted
slightly toward the player and a stubby cannon barrel sticking out the
front. UVs are arranged so the four 8-px bands of weapons_panel.png map to
the right faces:

  band 0 (V 0.00..0.25): targeting display
  band 1 (V 0.25..0.50): trigger buttons
  band 2 (V 0.50..0.75): metal body
  band 3 (V 0.75..1.00): warning chevron strip
"""

import json
import os
from typing import List, Tuple

GRID = 0.05
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_OBJ  = os.path.join(ROOT, "assets", "models", "weapons_panel.obj")
OUT_JSON = os.path.join(ROOT, "assets", "models", "weapons_panel.json")


BAND_DISPLAY  = (0.02, 0.23)
BAND_BUTTONS  = (0.27, 0.48)
BAND_METAL    = (0.52, 0.73)
BAND_CHEVRON  = (0.77, 0.98)


def quad_face(vi: List[int], u_lo: float, u_hi: float, band: Tuple[float, float]):
    v_lo, v_hi = band
    return (vi, (u_lo, u_hi, v_lo, v_hi))


def main() -> None:
    # ---- Vertex layout --------------------------------------------------
    # Console body is a 0.7 × 0.9 × 0.5 box (narrower + taller than the
    # bridge panel). Barrel is a small box jutting +Z out of the front face.
    #
    # Body cube corners:
    #     7----------6
    #    /|         /|
    #   4----------5 |
    #   | 3--------|-2
    #   |/         |/
    #   0----------1
    verts = [
        # Body (0..7)
        [-0.35, 0.00, -0.25],  # 0 front-bot-L
        [ 0.35, 0.00, -0.25],  # 1 front-bot-R
        [ 0.35, 0.00,  0.25],  # 2 back-bot-R
        [-0.35, 0.00,  0.25],  # 3 back-bot-L
        [-0.35, 0.90, -0.25],  # 4 front-top-L
        [ 0.35, 0.90, -0.25],  # 5 front-top-R
        [ 0.35, 0.90,  0.25],  # 6 back-top-R
        [-0.35, 0.90,  0.25],  # 7 back-top-L
        # Barrel (8..15) — small 0.12 × 0.12 × 0.30 box centered on the
        # front face at y=0.45 and sticking out into -Z.
        [-0.06, 0.39, -0.55],  # 8  barrel front-bot-L  (most -Z)
        [ 0.06, 0.39, -0.55],  # 9  barrel front-bot-R
        [ 0.06, 0.39, -0.25],  # 10 barrel back-bot-R   (mounted on body face)
        [-0.06, 0.39, -0.25],  # 11 barrel back-bot-L
        [-0.06, 0.51, -0.55],  # 12 barrel front-top-L
        [ 0.06, 0.51, -0.55],  # 13 barrel front-top-R
        [ 0.06, 0.51, -0.25],  # 14 barrel back-top-R
        [-0.06, 0.51, -0.25],  # 15 barrel back-top-L
    ]

    faces: list = []

    # ---- Body faces -----------------------------------------------------
    # Bottom (-Y) — metal.
    faces.append(quad_face([0, 1, 2, 3], 0.0, 1.0, BAND_METAL))
    # Back (+Z) — metal.
    faces.append(quad_face([3, 2, 6, 7], 0.0, 1.0, BAND_METAL))
    # Left (-X) — metal.
    faces.append(quad_face([0, 3, 7, 4], 0.0, 1.0, BAND_METAL))
    # Right (+X) — metal.
    faces.append(quad_face([1, 5, 6, 2], 0.0, 1.0, BAND_METAL))
    # Top (+Y) — buttons (trigger row).
    faces.append(quad_face([4, 5, 6, 7], 0.0, 1.0, BAND_BUTTONS))

    # Front face (-Z) — split into three horizontal bands so we can map
    # display to the upper section, metal to the middle (where the barrel
    # mounts), and chevrons to the bottom (warning strip near the floor).
    #
    # Need 4 extra verts on the front face to split it horizontally.
    # Display zone: y=0.62..0.90, metal zone: y=0.20..0.62, chevron zone:
    # y=0.00..0.20.
    front_y_lo  = 0.20
    front_y_mid = 0.62
    verts.append([-0.35, front_y_lo,  -0.25])  # 16 front-mid-low-L
    verts.append([ 0.35, front_y_lo,  -0.25])  # 17 front-mid-low-R
    verts.append([-0.35, front_y_mid, -0.25])  # 18 front-mid-high-L
    verts.append([ 0.35, front_y_mid, -0.25])  # 19 front-mid-high-R
    # Chevron strip (y=0..0.20)
    faces.append(quad_face([0, 1, 17, 16], 0.0, 1.0, BAND_CHEVRON))
    # Metal middle (y=0.20..0.62) — where the barrel mounts. We'll
    # subtract a small rectangle for the barrel root from the face by
    # splitting it into 4 surrounding sub-quads, but for low-poly tidiness
    # we just emit one flat metal quad and let the barrel poke out.
    faces.append(quad_face([16, 17, 19, 18], 0.0, 1.0, BAND_METAL))
    # Display top (y=0.62..0.90) — targeting screen.
    faces.append(quad_face([18, 19, 5, 4], 0.0, 1.0, BAND_DISPLAY))

    # ---- Barrel faces (always metal — short box) ------------------------
    # Front (most -Z) — verts 8,9,13,12 (CCW seen from -Z).
    faces.append(quad_face([8, 9, 13, 12], 0.0, 1.0, BAND_METAL))
    # Top — 12,13,14,15.
    faces.append(quad_face([12, 13, 14, 15], 0.0, 1.0, BAND_METAL))
    # Bottom — 8,11,10,9 (CCW seen from -Y).
    faces.append(quad_face([8, 11, 10, 9], 0.0, 1.0, BAND_METAL))
    # Left (-X) — 8,12,15,11.
    faces.append(quad_face([8, 12, 15, 11], 0.0, 1.0, BAND_METAL))
    # Right (+X) — 9,10,14,13.
    faces.append(quad_face([9, 10, 14, 13], 0.0, 1.0, BAND_METAL))
    # Back of the barrel sits flush against the body's front face — no
    # face emitted (it'd be invisible inside the body).

    # ---- Snap + write ---------------------------------------------------
    def snap(v: float) -> float:
        return round(round(v / GRID) * GRID, 4)
    verts = [[snap(c) for c in v] for v in verts]

    os.makedirs(os.path.dirname(OUT_OBJ), exist_ok=True)
    with open(OUT_OBJ, "w") as f:
        f.write("# weapons_panel.obj — generated by tools/gen-weapons-panel.py\n")
        f.write("# UVs sample weapons_panel.png; bands documented in gen-textures.py\n")
        f.write("o weapons_panel\n")
        for v in verts:
            f.write(f"v {v[0]:.4f} {v[1]:.4f} {v[2]:.4f}\n")
        uv_index = 1
        face_uv_indices: list[list[int]] = []
        for vi, (u_lo, u_hi, v_lo, v_hi) in faces:
            corners = [(u_lo, v_lo), (u_hi, v_lo), (u_hi, v_hi), (u_lo, v_hi)]
            for u, v in corners:
                f.write(f"vt {u:.4f} {v:.4f}\n")
            face_uv_indices.append([uv_index, uv_index + 1, uv_index + 2, uv_index + 3])
            uv_index += 4
        for (vi, _uv), uv_idx in zip(faces, face_uv_indices):
            tris = [
                ([vi[0], vi[1], vi[2]], [uv_idx[0], uv_idx[1], uv_idx[2]]),
                ([vi[0], vi[2], vi[3]], [uv_idx[0], uv_idx[2], uv_idx[3]]),
            ]
            for tri_v, tri_uv in tris:
                f.write(
                    f"f {tri_v[0]+1}/{tri_uv[0]} "
                    f"{tri_v[1]+1}/{tri_uv[1]} "
                    f"{tri_v[2]+1}/{tri_uv[2]}\n"
                )

    sidecar = {
        "name": "weapons_panel",
        "grid": GRID,
        "vertices": verts,
        "faces": [
            {
                "vertices": vi,
                "uv": {"u_lo": u_lo, "u_hi": u_hi, "v_lo": v_lo, "v_hi": v_hi},
            }
            for vi, (u_lo, u_hi, v_lo, v_hi) in faces
        ],
    }
    with open(OUT_JSON, "w") as f:
        json.dump(sidecar, f, indent=2)

    print(f"wrote {OUT_OBJ}  ({len(verts)} verts, {len(faces)} quads)")
    print(f"wrote {OUT_JSON}")


if __name__ == "__main__":
    main()
