#!/usr/bin/env python3
"""
Generate a low-poly blocky humanoid character into assets/models/character.json.

Six cube parts on a strict 0.1-unit grid so every vertex is snappable in the
web editor. Face-winding is CCW outward. The JSON format is intentionally
minimal — vertices + triangles only; UVs/materials are a separate concern.
"""

import json
import os
from typing import List, Tuple

GRID = 0.1
OUT = os.path.join(os.path.dirname(__file__), "..", "assets", "models", "character.json")

# name, (xmin, ymin, zmin), (xmax, ymax, zmax)
# Body parts thickened on Z so the character doesn't look paper-flat from the
# 3/4 view. `nose` is a small block jutting out +Z so the character's facing
# direction is visible as it rotates.
PARTS: List[Tuple[str, Tuple[float, float, float], Tuple[float, float, float]]] = [
    ("head",  (-0.2, 1.2, -0.2), ( 0.2, 1.6,  0.2)),
    ("nose",  (-0.1, 1.3,  0.2), ( 0.1, 1.5,  0.3)),
    ("torso", (-0.3, 0.6, -0.2), ( 0.3, 1.2,  0.2)),
    ("arm_l", (-0.5, 0.6, -0.1), (-0.3, 1.1,  0.1)),
    ("arm_r", ( 0.3, 0.6, -0.1), ( 0.5, 1.1,  0.1)),
    ("leg_l", (-0.3, 0.0, -0.1), (-0.1, 0.6,  0.1)),
    ("leg_r", ( 0.1, 0.0, -0.1), ( 0.3, 0.6,  0.1)),
]


def box_verts(mn, mx):
    x0, y0, z0 = mn
    x1, y1, z1 = mx
    return [
        [x0, y0, z0], [x1, y0, z0], [x1, y0, z1], [x0, y0, z1],  # bottom 0-3
        [x0, y1, z0], [x1, y1, z0], [x1, y1, z1], [x0, y1, z1],  # top    4-7
    ]


# Triangles for a box (CCW outward).
BOX_TRIS = [
    [0, 2, 1], [0, 3, 2],  # bottom (-Y)
    [4, 5, 6], [4, 6, 7],  # top    (+Y)
    [0, 1, 5], [0, 5, 4],  # back   (-Z)
    [3, 6, 2], [3, 7, 6],  # front  (+Z)
    [0, 4, 7], [0, 7, 3],  # left   (-X)
    [1, 2, 6], [1, 6, 5],  # right  (+X)
]


def snap(v: float) -> float:
    return round(v / GRID) * GRID


def main() -> None:
    vertices: list[list[float]] = []
    triangles: list[list[int]] = []
    parts_meta: list[dict] = []

    for name, mn, mx in PARTS:
        base = len(vertices)
        for v in box_verts(mn, mx):
            vertices.append([round(snap(c), 4) for c in v])
        for t in BOX_TRIS:
            triangles.append([base + i for i in t])
        parts_meta.append({"name": name, "vertex_start": base, "vertex_count": 8})

    model = {
        "name": "character",
        "grid": GRID,
        "parts": parts_meta,
        "vertices": vertices,
        "triangles": triangles,
    }

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        json.dump(model, f, indent=2)
    print(f"wrote {OUT}  ({len(vertices)} verts, {len(triangles)} tris)")


if __name__ == "__main__":
    main()
