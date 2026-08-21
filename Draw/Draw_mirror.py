#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Draw_mirror.py

读取 LACT_sim 的镜面排布和机械遮挡配置，绘制镜面正投影及遮挡分布。

默认输入：
    configs/mirror_1229_facets.csv
    configs/obstructions/raytrace_final_structure_primitives.csv
    configs/simulation_fast_reflectivity.cfg

默认输出：
    mirror_obstruction_map.png
    mirror_obstruction_map.pdf

运行：
    python3 Draw_mirror.py

也可指定路径：
    python3 Draw_mirror.py \
        --mirror-csv configs/mirror_1229_facets.csv \
        --obstruction-csv configs/obstructions/raytrace_final_structure_primitives.csv \
        --runtime-cfg configs/simulation_fast_reflectivity.cfg \
        --output mirror_obstruction_map.png

说明：
1. 镜面六边形尺寸直接读取 mirror_1229_facets.csv。
2. support_strut 按圆柱在镜面方向上的二维投影绘制。
3. camera_body 按 polygon_prism 绘制。
4. camera_support_gap_box 按 box 及其多边形孔绘制。
5. 若 simulation_fast_reflectivity.cfg 存在，则额外绘制当前 Detector：
   φ170 mm 圆盘 + 140 mm 底座 + φ21 mm 物理接收孔。
6. 默认按 source.beam_direction 对所有遮挡投影到镜面平均 Z 平面。
"""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
from matplotlib.patches import Circle, PathPatch, Polygon
from matplotlib.path import Path as MplPath

Point2D = Tuple[float, float]
Point3D = Tuple[float, float, float]

# ============================================================
# Plot style
# ============================================================

MIRROR_FACE = "#D9D9D9"
MIRROR_EDGE = "#666666"
SUPPORT_FACE = "#D55E00"
CAMERA_FACE = "#0072B2"
FRAME_FACE = "#CC79A7"
DETECTOR_FACE = "#4D4D4D"
HOLE_EDGE = "#111111"

# ============================================================
# Generic helpers
# ============================================================

def parse_float(value: str, default: Optional[float] = None) -> float:
    text = value.strip()
    if text == "":
        if default is None:
            raise ValueError("Empty numeric field.")
        return default
    return float(text)


def load_key_value_cfg(path: Path) -> Dict[str, str]:
    cfg: Dict[str, str] = {}
    if not path.exists():
        return cfg

    with path.open("r", encoding="utf-8") as file:
        for raw_line in file:
            line = raw_line.split("#", 1)[0].strip()
            if not line or "=" not in line:
                continue
            key, value = line.split("=", 1)
            cfg[key.strip()] = value.strip()

    return cfg


def parse_vec3(text: str, default: Point3D) -> Point3D:
    if not text:
        return default
    values = [float(item.strip()) for item in text.split(",")]
    if len(values) != 3:
        raise ValueError(f"Invalid Vec3: {text}")
    return values[0], values[1], values[2]


def normalize3(vector: Point3D) -> Point3D:
    x, y, z = vector
    norm = math.sqrt(x * x + y * y + z * z)
    if norm <= 1.0e-15:
        raise ValueError("Zero-length direction vector.")
    return x / norm, y / norm, z / norm


def project_point_to_z(
    point: Point3D,
    direction: Point3D,
    target_z: float,
) -> Point2D:
    x, y, z = point
    dx, dy, dz = direction

    if abs(dz) <= 1.0e-15:
        raise ValueError("Beam direction is parallel to the mirror projection plane.")

    t = (target_z - z) / dz
    return x + t * dx, y + t * dy


def rotate_xy(x: float, y: float, angle: float) -> Point2D:
    c = math.cos(angle)
    s = math.sin(angle)
    return c * x - s * y, s * x + c * y


# ============================================================
# Mirror geometry
# ============================================================

def read_mirror_facets(path: Path) -> List[Dict[str, str]]:
    with path.open("r", encoding="utf-8-sig", newline="") as file:
        rows = list(csv.DictReader(file))

    if not rows:
        raise RuntimeError(f"No mirror facets found in {path}")

    required = {
        "id",
        "center_x",
        "center_y",
        "center_z",
        "aperture_shape",
        "size1",
        "aperture_rotation_rad",
    }

    missing = required.difference(rows[0].keys())
    if missing:
        raise RuntimeError(
            "Mirror CSV missing columns: " + ", ".join(sorted(missing))
        )

    return rows


def hexagon_vertices(
    center_x: float,
    center_y: float,
    flat_to_flat: float,
    rotation_rad: float,
) -> List[Point2D]:
    """
    与 OpticalTracer 中 Hexagon aperture 判据一致：

        apothem = 0.5 * size1
        max(
            |x|,
            |0.5*x + sqrt(3)/2*y|,
            |-0.5*x + sqrt(3)/2*y|
        ) <= apothem

    因此 size1 为六边形对边距离。
    """
    apothem = 0.5 * flat_to_flat
    y1 = apothem / math.sqrt(3.0)
    y2 = 2.0 * apothem / math.sqrt(3.0)

    local_vertices = [
        (apothem, y1),
        (0.0, y2),
        (-apothem, y1),
        (-apothem, -y1),
        (0.0, -y2),
        (apothem, -y1),
    ]

    vertices: List[Point2D] = []

    for x, y in local_vertices:
        xr, yr = rotate_xy(x, y, rotation_rad)
        vertices.append((center_x + xr, center_y + yr))

    return vertices


def draw_mirrors(
    axis: plt.Axes,
    facets: Sequence[Dict[str, str]],
    show_ids: bool,
) -> None:
    for row in facets:
        shape = row["aperture_shape"].strip().lower()
        cx = parse_float(row["center_x"])
        cy = parse_float(row["center_y"])
        size1 = parse_float(row["size1"])
        rotation = parse_float(row["aperture_rotation_rad"], 0.0)

        if shape == "hexagon":
            vertices = hexagon_vertices(cx, cy, size1, rotation)
            patch = Polygon(
                vertices,
                closed=True,
                facecolor=MIRROR_FACE,
                edgecolor=MIRROR_EDGE,
                linewidth=0.7,
                zorder=1,
            )
        elif shape == "circular":
            patch = Circle(
                (cx, cy),
                radius=size1,
                facecolor=MIRROR_FACE,
                edgecolor=MIRROR_EDGE,
                linewidth=0.7,
                zorder=1,
            )
        elif shape == "square":
            half = 0.5 * size1
            local = [
                (-half, -half),
                (half, -half),
                (half, half),
                (-half, half),
            ]
            vertices = []
            for x, y in local:
                xr, yr = rotate_xy(x, y, rotation)
                vertices.append((cx + xr, cy + yr))
            patch = Polygon(
                vertices,
                closed=True,
                facecolor=MIRROR_FACE,
                edgecolor=MIRROR_EDGE,
                linewidth=0.7,
                zorder=1,
            )
        else:
            raise RuntimeError(f"Unsupported mirror aperture_shape: {shape}")

        axis.add_patch(patch)

        if show_ids:
            axis.text(
                cx,
                cy,
                row["id"],
                ha="center",
                va="center",
                fontsize=6.5,
                zorder=10,
            )


# ============================================================
# Obstruction geometry
# ============================================================

def read_obstructions(path: Path) -> List[Dict[str, str]]:
    with path.open("r", encoding="utf-8-sig", newline="") as file:
        lines = [line for line in file if not line.lstrip().startswith("#")]

    rows = list(csv.DictReader(lines))

    if not rows:
        raise RuntimeError(f"No obstruction primitives found in {path}")

    return rows


def regular_polygon_vertices(
    center: Point2D,
    radius: float,
    sides: int,
    rotation_rad: float,
    clockwise: bool = False,
) -> List[Point2D]:
    cx, cy = center
    vertices = []

    indices: Iterable[int]
    if clockwise:
        indices = range(sides - 1, -1, -1)
    else:
        indices = range(sides)

    for i in indices:
        angle = rotation_rad + 2.0 * math.pi * i / sides
        vertices.append(
            (
                cx + radius * math.cos(angle),
                cy + radius * math.sin(angle),
            )
        )

    return vertices


def capsule_vertices(
    p0: Point2D,
    p1: Point2D,
    radius: float,
    samples_per_cap: int = 24,
) -> List[Point2D]:
    x0, y0 = p0
    x1, y1 = p1
    dx = x1 - x0
    dy = y1 - y0
    length = math.hypot(dx, dy)

    if length <= 1.0e-12:
        return [
            (
                x0 + radius * math.cos(2.0 * math.pi * i / 48),
                y0 + radius * math.sin(2.0 * math.pi * i / 48),
            )
            for i in range(48)
        ]

    angle = math.atan2(dy, dx)
    vertices: List[Point2D] = []

    for i in range(samples_per_cap + 1):
        theta = angle + math.pi / 2.0 + math.pi * i / samples_per_cap
        vertices.append(
            (
                x0 + radius * math.cos(theta),
                y0 + radius * math.sin(theta),
            )
        )

    for i in range(samples_per_cap + 1):
        theta = angle - math.pi / 2.0 + math.pi * i / samples_per_cap
        vertices.append(
            (
                x1 + radius * math.cos(theta),
                y1 + radius * math.sin(theta),
            )
        )

    return vertices


def compound_polygon_with_hole(
    outer_vertices: Sequence[Point2D],
    hole_vertices: Sequence[Point2D],
    facecolor: str,
    alpha: float,
    zorder: int,
) -> PathPatch:
    outer = list(outer_vertices)
    hole = list(reversed(hole_vertices))

    vertices: List[Point2D] = []
    codes: List[int] = []

    for polygon in (outer, hole):
        vertices.extend(polygon)
        vertices.append(polygon[0])
        codes.extend(
            [MplPath.MOVETO]
            + [MplPath.LINETO] * (len(polygon) - 1)
            + [MplPath.CLOSEPOLY]
        )

    path = MplPath(vertices, codes)

    return PathPatch(
        path,
        facecolor=facecolor,
        edgecolor=facecolor,
        linewidth=1.0,
        alpha=alpha,
        zorder=zorder,
    )


def draw_obstructions(
    axis: plt.Axes,
    rows: Sequence[Dict[str, str]],
    beam_direction: Point3D,
    mirror_z: float,
) -> Dict[str, int]:
    counts: Dict[str, int] = {}

    for row in rows:
        primitive_type = row.get("type", "").strip().lower()
        role = row.get("role", "").strip() or primitive_type
        counts[role] = counts.get(role, 0) + 1

        if primitive_type == "cylinder":
            p0 = (
                parse_float(row["x0_m"]),
                parse_float(row["y0_m"]),
                parse_float(row["z0_m"]),
            )
            p1 = (
                parse_float(row["x1_m"]),
                parse_float(row["y1_m"]),
                parse_float(row["z1_m"]),
            )
            radius = parse_float(row["radius_m"])
            q0 = project_point_to_z(p0, beam_direction, mirror_z)
            q1 = project_point_to_z(p1, beam_direction, mirror_z)

            patch = Polygon(
                capsule_vertices(q0, q1, radius),
                closed=True,
                facecolor=SUPPORT_FACE,
                edgecolor=SUPPORT_FACE,
                linewidth=0.8,
                alpha=0.62,
                zorder=4,
            )
            axis.add_patch(patch)
            continue

        if primitive_type == "polygon_prism":
            center = (
                parse_float(row["center_x_m"]),
                parse_float(row["center_y_m"]),
                parse_float(row["center_z_m"]),
            )
            radius = parse_float(row["radius_m"])
            sides = int(round(parse_float(row["sides"])))
            rotation = parse_float(row["rotation_rad"], 0.0)
            projected_center = project_point_to_z(
                center,
                beam_direction,
                mirror_z,
            )

            patch = Polygon(
                regular_polygon_vertices(
                    projected_center,
                    radius,
                    sides,
                    rotation,
                ),
                closed=True,
                facecolor=CAMERA_FACE,
                edgecolor=CAMERA_FACE,
                linewidth=1.0,
                alpha=0.60,
                zorder=5,
            )
            axis.add_patch(patch)
            continue

        if primitive_type == "box":
            center = (
                parse_float(row["center_x_m"]),
                parse_float(row["center_y_m"]),
                parse_float(row["center_z_m"]),
            )
            half_x = parse_float(row["half_x_m"])
            half_y = parse_float(row["half_y_m"])
            projected_center = project_point_to_z(
                center,
                beam_direction,
                mirror_z,
            )

            cx, cy = projected_center
            outer = [
                (cx - half_x, cy - half_y),
                (cx + half_x, cy - half_y),
                (cx + half_x, cy + half_y),
                (cx - half_x, cy + half_y),
            ]

            hole_radius_text = row.get("hole_radius_m", "").strip()

            if hole_radius_text:
                hole_radius = float(hole_radius_text)
                hole_sides_text = row.get("hole_sides", "").strip()
                hole_sides = int(round(float(hole_sides_text))) if hole_sides_text else 64
                hole_rotation = parse_float(
                    row.get("hole_rotation_rad", ""),
                    0.0,
                )

                hole = regular_polygon_vertices(
                    projected_center,
                    hole_radius,
                    hole_sides,
                    hole_rotation,
                )

                patch = compound_polygon_with_hole(
                    outer,
                    hole,
                    FRAME_FACE,
                    0.58,
                    3,
                )
            else:
                patch = Polygon(
                    outer,
                    closed=True,
                    facecolor=FRAME_FACE,
                    edgecolor=FRAME_FACE,
                    linewidth=1.0,
                    alpha=0.58,
                    zorder=3,
                )

            axis.add_patch(patch)
            continue

        print(
            f"Warning: unsupported obstruction type "
            f"'{primitive_type}' ({row.get('name', '')}), skipped."
        )

    return counts


# ============================================================
# Detector geometry from simulation_fast_reflectivity.cfg
# ============================================================

def circle_path_vertices(
    center: Point2D,
    radius: float,
    samples: int,
    clockwise: bool,
) -> List[Point2D]:
    cx, cy = center
    indices = range(samples - 1, -1, -1) if clockwise else range(samples)

    return [
        (
            cx + radius * math.cos(2.0 * math.pi * i / samples),
            cy + radius * math.sin(2.0 * math.pi * i / samples),
        )
        for i in indices
    ]


def draw_detector_from_cfg(
    axis: plt.Axes,
    cfg: Dict[str, str],
    beam_direction: Point3D,
    mirror_z: float,
) -> bool:
    required = [
        "cmos.position_local",
        "cmos.body_diameter_m",
        "cmos.base_width_m",
        "cmos.base_exposed_height_m",
        "cmos.base_left_inset_m",
        "cmos.physical_hole_diameter_m",
        "cmos.physical_hole_left_edge_distance_m",
    ]

    if any(key not in cfg for key in required):
        return False

    position = parse_vec3(
        cfg["cmos.position_local"],
        (0.0, 0.0, -16.0),
    )
    projected_center = project_point_to_z(
        position,
        beam_direction,
        mirror_z,
    )

    body_radius = 0.5 * float(cfg["cmos.body_diameter_m"])
    base_width = float(cfg["cmos.base_width_m"])
    base_exposed = float(cfg["cmos.base_exposed_height_m"])
    base_left_inset = float(cfg["cmos.base_left_inset_m"])
    hole_radius = 0.5 * float(cfg["cmos.physical_hole_diameter_m"])
    hole_left_distance = float(
        cfg["cmos.physical_hole_left_edge_distance_m"]
    )

    base_left = -body_radius + base_left_inset
    base_right = base_left + base_width
    base_bottom = -body_radius - base_exposed

    limiting_x = max(abs(base_left), abs(base_right))

    if limiting_x >= body_radius:
        raise ValueError(
            "Detector base cannot be completely covered by circular body."
        )

    base_top = -math.sqrt(
        body_radius * body_radius -
        limiting_x * limiting_x
    )

    hole_center_x = (
        -body_radius +
        hole_left_distance +
        hole_radius
    )

    cx, cy = projected_center

    # Base solid.
    base_vertices = [
        (cx + base_left, cy + base_bottom),
        (cx + base_right, cy + base_bottom),
        (cx + base_right, cy + base_top),
        (cx + base_left, cy + base_top),
    ]

    axis.add_patch(
        Polygon(
            base_vertices,
            closed=True,
            facecolor=DETECTOR_FACE,
            edgecolor=DETECTOR_FACE,
            linewidth=1.0,
            alpha=0.78,
            zorder=7,
        )
    )

    # Circular body with a real transparent receiver hole.
    outer = circle_path_vertices(
        (cx, cy),
        body_radius,
        160,
        clockwise=False,
    )

    inner = circle_path_vertices(
        (cx + hole_center_x, cy),
        hole_radius,
        80,
        clockwise=False,
    )

    detector_body = compound_polygon_with_hole(
        outer,
        inner,
        DETECTOR_FACE,
        0.78,
        8,
    )

    detector_body.set_edgecolor(DETECTOR_FACE)
    axis.add_patch(detector_body)

    axis.add_patch(
        Circle(
            (cx + hole_center_x, cy),
            hole_radius,
            facecolor="none",
            edgecolor=HOLE_EDGE,
            linewidth=0.8,
            zorder=9,
        )
    )

    return True


# ============================================================
# Figure
# ============================================================

def mirror_extent(
    facets: Sequence[Dict[str, str]],
    margin: float = 0.45,
) -> Tuple[float, float, float, float]:
    xs = [parse_float(row["center_x"]) for row in facets]
    ys = [parse_float(row["center_y"]) for row in facets]
    sizes = [parse_float(row["size1"]) for row in facets]

    radius_margin = max(sizes) / math.sqrt(3.0) + margin

    return (
        min(xs) - radius_margin,
        max(xs) + radius_margin,
        min(ys) - radius_margin,
        max(ys) + radius_margin,
    )


def build_legend(
    axis: plt.Axes,
    detector_drawn: bool,
) -> None:
    handles = [
        Line2D(
            [0],
            [0],
            marker="s",
            linestyle="none",
            markerfacecolor=MIRROR_FACE,
            markeredgecolor=MIRROR_EDGE,
            markersize=9,
            label="Mirror facets",
        ),
        Line2D(
            [0],
            [0],
            color=SUPPORT_FACE,
            linewidth=7,
            alpha=0.62,
            label="Support struts",
        ),
        Line2D(
            [0],
            [0],
            color=CAMERA_FACE,
            linewidth=7,
            alpha=0.60,
            label="Camera body",
        ),
        Line2D(
            [0],
            [0],
            color=FRAME_FACE,
            linewidth=7,
            alpha=0.58,
            label="Camera support frame",
        ),
    ]

    if detector_drawn:
        handles.append(
            Line2D(
                [0],
                [0],
                color=DETECTOR_FACE,
                linewidth=7,
                alpha=0.78,
                label="Reflectivity detector",
            )
        )

    axis.legend(
        handles=handles,
        loc="upper right",
        frameon=True,
        framealpha=0.95,
        fontsize=9,
    )


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Draw LACT mirror distribution and obstruction projection."
    )

    parser.add_argument(
        "--mirror-csv",
        type=Path,
        default=Path("configs/mirror_1229_facets.csv"),
        help="Mirror-facet CSV.",
    )

    parser.add_argument(
        "--obstruction-csv",
        type=Path,
        default=Path(
            "configs/obstructions/"
            "raytrace_final_structure_primitives.csv"
        ),
        help="Obstruction primitive CSV.",
    )

    parser.add_argument(
        "--runtime-cfg",
        type=Path,
        default=Path("configs/simulation_fast_reflectivity.cfg"),
        help="Runtime config used for source direction and Detector geometry.",
    )

    parser.add_argument(
        "--output",
        type=Path,
        default=Path("mirror_obstruction_map.png"),
        help="Output raster image.",
    )

    parser.add_argument(
        "--pdf",
        type=Path,
        default=Path("mirror_obstruction_map.pdf"),
        help="Output vector PDF. Use an empty string to disable.",
    )

    parser.add_argument(
        "--show-ids",
        action="store_true",
        help="Draw mirror facet IDs.",
    )

    parser.add_argument(
        "--title",
        default="LACT mirror distribution and obstruction projection",
        help="Figure title.",
    )

    args = parser.parse_args()

    facets = read_mirror_facets(args.mirror_csv)
    obstruction_rows = read_obstructions(args.obstruction_csv)
    runtime_cfg = load_key_value_cfg(args.runtime_cfg)

    beam_direction = normalize3(
        parse_vec3(
            runtime_cfg.get(
                "source.beam_direction",
                "0.0,0.0,-1.0",
            ),
            (0.0, 0.0, -1.0),
        )
    )

    mirror_z_values = [
        parse_float(row["center_z"])
        for row in facets
    ]

    mirror_z = sum(mirror_z_values) / len(mirror_z_values)

    plt.rcParams["font.family"] = "serif"
    plt.rcParams["font.serif"] = [
        "Times New Roman",
        "Times",
        "DejaVu Serif",
    ]
    plt.rcParams["axes.unicode_minus"] = False

    figure, axis = plt.subplots(figsize=(8.2, 8.2))

    draw_mirrors(
        axis,
        facets,
        args.show_ids,
    )

    obstruction_counts = draw_obstructions(
        axis,
        obstruction_rows,
        beam_direction,
        mirror_z,
    )

    detector_drawn = draw_detector_from_cfg(
        axis,
        runtime_cfg,
        beam_direction,
        mirror_z,
    )

    xmin, xmax, ymin, ymax = mirror_extent(facets)

    axis.set_xlim(xmin, xmax)
    axis.set_ylim(ymin, ymax)
    axis.set_aspect("equal", adjustable="box")
    axis.set_xlabel("X (m)", fontsize=12)
    axis.set_ylabel("Y (m)", fontsize=12)
    axis.set_title(args.title, fontsize=13)
    axis.tick_params(labelsize=10)
    axis.grid(False)

    build_legend(
        axis,
        detector_drawn,
    )

    figure.tight_layout()

    args.output.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    figure.savefig(
        args.output,
        dpi=300,
        bbox_inches="tight",
    )

    if str(args.pdf).strip():
        args.pdf.parent.mkdir(
            parents=True,
            exist_ok=True,
        )

        figure.savefig(
            args.pdf,
            bbox_inches="tight",
        )

    plt.close(figure)

    print("========================================")
    print("LACT mirror obstruction drawing")
    print("========================================")
    print(f"mirror_csv={args.mirror_csv}")
    print(f"obstruction_csv={args.obstruction_csv}")
    print(f"runtime_cfg={args.runtime_cfg}")
    print(f"mirror_facets={len(facets)}")
    print(f"mirror_projection_z_m={mirror_z:.9f}")
    print(
        "beam_direction="
        f"{beam_direction[0]:.8f},"
        f"{beam_direction[1]:.8f},"
        f"{beam_direction[2]:.8f}"
    )
    print(f"obstruction_primitives={len(obstruction_rows)}")

    for role, count in sorted(obstruction_counts.items()):
        print(f"obstruction_{role}={count}")

    print(f"runtime_detector_drawn={int(detector_drawn)}")
    print(f"png_output={args.output}")

    if str(args.pdf).strip():
        print(f"pdf_output={args.pdf}")


if __name__ == "__main__":
    main()
