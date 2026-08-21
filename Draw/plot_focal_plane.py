#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import sys

import numpy as np
import pandas as pd

import matplotlib
matplotlib.use("Agg")

import matplotlib.pyplot as plt


# ============================================================
# 输入参数
# ============================================================

if len(sys.argv) < 2:
    print("Usage:")
    print("python Draw/plot_focal_plane.py hits.csv")
    sys.exit(1)

csv_path = sys.argv[1]


# ============================================================
# 用户参数
# ============================================================

# 焦平面显示范围 [mm]
PLOT_LIMIT_MM = 12.5

# 二维直方图 bin 数
HIST2D_BINS = 250

# 一维直方图 bin 数
HIST1D_BINS = 200

# 输出分辨率
DPI = 300


# ============================================================
# 读取数据
# ============================================================

print("============================================================")
print("Reading focal-plane hit data")
print("============================================================")
print("Input file:")
print(csv_path)
print()

df = pd.read_csv(csv_path)

print("Total rows =", len(df))


# ============================================================
# 只保留命中焦平面的光线
# ============================================================

df = df[df["hit_surface"] == 1].copy()

n_hit_surface = len(df)

print()
print("hit_surface =", n_hit_surface)


# ============================================================
# 删除 u_m / v_m 中 NaN 和 inf
# ============================================================

valid = (
    np.isfinite(df["u_m"]) &
    np.isfinite(df["v_m"])
)

n_invalid = int((~valid).sum())

df = df[valid].copy()

n_valid = len(df)

print("invalid u/v =", n_invalid)
print("valid focal-plane photons =", n_valid)


# ============================================================
# 单位转换：m -> mm
# ============================================================

x_mm = df["u_m"].to_numpy(dtype=np.float64) * 1000.0
y_mm = df["v_m"].to_numpy(dtype=np.float64) * 1000.0


# ============================================================
# 基本统计
# ============================================================

x_min = np.min(x_mm)
x_max = np.max(x_mm)

y_min = np.min(y_mm)
y_max = np.max(y_mm)

x_center = np.mean(x_mm)
y_center = np.mean(y_mm)

print()
print("============================================================")
print("Focal-plane statistics")
print("============================================================")

print("x range [mm] =", x_min, x_max)
print("y range [mm] =", y_min, y_max)

print()
print("centroid u [mm] =", x_center)
print("centroid v [mm] =", y_center)


# ============================================================
# RMS spot radius
# ============================================================

dx = x_mm - x_center
dy = y_mm - y_center

radius_mm = np.sqrt(
    dx * dx +
    dy * dy
)

rms_radius_mm = np.sqrt(
    np.mean(radius_mm ** 2)
)

print("RMS spot radius [mm] =", rms_radius_mm)


# ============================================================
# D80
#
# D80 定义：
# 以光斑质心为中心，包含 80% 光子的圆的直径
# ============================================================

r80_mm = np.percentile(
    radius_mm,
    80.0
)

d80_mm = 2.0 * r80_mm

print("R80 [mm] =", r80_mm)
print("D80 [mm] =", d80_mm)


# ============================================================
# 1. 焦平面散点图
# ============================================================

fig, ax = plt.subplots(
    figsize=(6.5, 6.0)
)

ax.scatter(
    x_mm,
    y_mm,
    s=0.15,
    alpha=0.12,
    rasterized=True
)

ax.set_xlabel(
    "u [mm]",
    fontsize=14
)

ax.set_ylabel(
    "v [mm]",
    fontsize=14
)

ax.set_xlim(
    -PLOT_LIMIT_MM,
    PLOT_LIMIT_MM
)

ax.set_ylim(
    -PLOT_LIMIT_MM,
    PLOT_LIMIT_MM
)

ax.set_aspect(
    "equal",
    adjustable="box"
)

ax.tick_params(
    axis="both",
    direction="in",
    top=True,
    right=True,
    labelsize=12
)

ax.set_title(
    f"Photon Distribution on Focal Plane\n"
    f"N = {n_valid:,}",
    fontsize=15,
    pad=8
)

fig.subplots_adjust(
    left=0.14,
    right=0.96,
    bottom=0.13,
    top=0.90
)

fig.savefig(
    "focal_plane_scatter.png",
    dpi=DPI,
    bbox_inches="tight"
)

plt.close(fig)

print()
print("Saved: focal_plane_scatter.png")


# ============================================================
# 2. 二维光子计数分布
# ============================================================

fig, ax = plt.subplots(
    figsize=(6.8, 6.0)
)

h = ax.hist2d(
    x_mm,
    y_mm,
    bins=HIST2D_BINS,
    range=[
        [-PLOT_LIMIT_MM, PLOT_LIMIT_MM],
        [-PLOT_LIMIT_MM, PLOT_LIMIT_MM]
    ]
)

ax.set_xlabel(
    "u [mm]",
    fontsize=14
)

ax.set_ylabel(
    "v [mm]",
    fontsize=14
)

ax.set_xlim(
    -PLOT_LIMIT_MM,
    PLOT_LIMIT_MM
)

ax.set_ylim(
    -PLOT_LIMIT_MM,
    PLOT_LIMIT_MM
)

ax.set_aspect(
    "equal",
    adjustable="box"
)

ax.tick_params(
    axis="both",
    direction="in",
    top=True,
    right=True,
    labelsize=12
)

ax.set_title(
    f"Photon Density on Focal Plane\n"
    f"N = {n_valid:,}",
    fontsize=15,
    pad=8
)

# ----------------------------
# colorbar
# ----------------------------

cbar = fig.colorbar(
    h[3],
    ax=ax,
    pad=0.025,
    fraction=0.046
)

cbar.set_label(
    "Photon counts",
    fontsize=13
)

cbar.ax.tick_params(
    labelsize=11
)

# ----------------------------
# 布局
# ----------------------------

fig.subplots_adjust(
    left=0.14,
    right=0.87,
    bottom=0.13,
    top=0.90
)

fig.savefig(
    "focal_plane_hist2d.png",
    dpi=DPI,
    bbox_inches="tight"
)

plt.close(fig)

print("Saved: focal_plane_hist2d.png")


# ============================================================
# 3. D80 示意图
# ============================================================

fig, ax = plt.subplots(
    figsize=(6.8, 6.0)
)

h = ax.hist2d(
    x_mm,
    y_mm,
    bins=HIST2D_BINS,
    range=[
        [-PLOT_LIMIT_MM, PLOT_LIMIT_MM],
        [-PLOT_LIMIT_MM, PLOT_LIMIT_MM]
    ]
)

# D80 圆
circle = plt.Circle(
    (x_center, y_center),
    r80_mm,
    fill=False,
    linewidth=1.5
)

ax.add_patch(circle)

# 光斑质心
ax.plot(
    x_center,
    y_center,
    marker="+",
    markersize=10,
    markeredgewidth=1.5
)

ax.set_xlabel(
    "u [mm]",
    fontsize=14
)

ax.set_ylabel(
    "v [mm]",
    fontsize=14
)

ax.set_xlim(
    -PLOT_LIMIT_MM,
    PLOT_LIMIT_MM
)

ax.set_ylim(
    -PLOT_LIMIT_MM,
    PLOT_LIMIT_MM
)

ax.set_aspect(
    "equal",
    adjustable="box"
)

ax.tick_params(
    axis="both",
    direction="in",
    top=True,
    right=True,
    labelsize=12
)

ax.set_title(
    f"Focal-plane Spot with D80\n"
    f"D80 = {d80_mm:.3f} mm",
    fontsize=15,
    pad=8
)

cbar = fig.colorbar(
    h[3],
    ax=ax,
    pad=0.025,
    fraction=0.046
)

cbar.set_label(
    "Photon counts",
    fontsize=13
)

cbar.ax.tick_params(
    labelsize=11
)

fig.subplots_adjust(
    left=0.14,
    right=0.87,
    bottom=0.13,
    top=0.90
)

fig.savefig(
    "focal_plane_D80.png",
    dpi=DPI,
    bbox_inches="tight"
)

plt.close(fig)

print("Saved: focal_plane_D80.png")


# ============================================================
# 4. u 方向光子分布
# ============================================================

fig, ax = plt.subplots(
    figsize=(7.0, 5.0)
)

ax.hist(
    x_mm,
    bins=HIST1D_BINS,
    range=(-PLOT_LIMIT_MM, PLOT_LIMIT_MM)
)

ax.set_xlabel(
    "u [mm]",
    fontsize=14
)

ax.set_ylabel(
    "Photon counts",
    fontsize=14
)

ax.set_xlim(
    -PLOT_LIMIT_MM,
    PLOT_LIMIT_MM
)

ax.tick_params(
    axis="both",
    direction="in",
    top=True,
    right=True,
    labelsize=12
)

ax.set_title(
    "Focal-plane Distribution along u",
    fontsize=15
)

fig.subplots_adjust(
    left=0.14,
    right=0.96,
    bottom=0.15,
    top=0.90
)

fig.savefig(
    "focal_plane_u_hist.png",
    dpi=DPI,
    bbox_inches="tight"
)

plt.close(fig)

print("Saved: focal_plane_u_hist.png")


# ============================================================
# 5. v 方向光子分布
# ============================================================

fig, ax = plt.subplots(
    figsize=(7.0, 5.0)
)

ax.hist(
    y_mm,
    bins=HIST1D_BINS,
    range=(-PLOT_LIMIT_MM, PLOT_LIMIT_MM)
)

ax.set_xlabel(
    "v [mm]",
    fontsize=14
)

ax.set_ylabel(
    "Photon counts",
    fontsize=14
)

ax.set_xlim(
    -PLOT_LIMIT_MM,
    PLOT_LIMIT_MM
)

ax.tick_params(
    axis="both",
    direction="in",
    top=True,
    right=True,
    labelsize=12
)

ax.set_title(
    "Focal-plane Distribution along v",
    fontsize=15
)

fig.subplots_adjust(
    left=0.14,
    right=0.96,
    bottom=0.15,
    top=0.90
)

fig.savefig(
    "focal_plane_v_hist.png",
    dpi=DPI,
    bbox_inches="tight"
)

plt.close(fig)

print("Saved: focal_plane_v_hist.png")


# ============================================================
# 输出统计结果到 txt
# ============================================================

with open(
    "focal_plane_statistics.txt",
    "w",
    encoding="utf-8"
) as f:

    f.write("Focal-plane photon statistics\n")
    f.write("========================================\n")

    f.write(
        f"hit_surface={n_hit_surface}\n"
    )

    f.write(
        f"invalid_uv={n_invalid}\n"
    )

    f.write(
        f"valid_photons={n_valid}\n"
    )

    f.write(
        f"u_min_mm={x_min:.9f}\n"
    )

    f.write(
        f"u_max_mm={x_max:.9f}\n"
    )

    f.write(
        f"v_min_mm={y_min:.9f}\n"
    )

    f.write(
        f"v_max_mm={y_max:.9f}\n"
    )

    f.write(
        f"centroid_u_mm={x_center:.9f}\n"
    )

    f.write(
        f"centroid_v_mm={y_center:.9f}\n"
    )

    f.write(
        f"rms_radius_mm={rms_radius_mm:.9f}\n"
    )

    f.write(
        f"r80_mm={r80_mm:.9f}\n"
    )

    f.write(
        f"d80_mm={d80_mm:.9f}\n"
    )

print()
print("Saved: focal_plane_statistics.txt")


# ============================================================
# 完成
# ============================================================

print()
print("============================================================")
print("Done")
print("============================================================")
