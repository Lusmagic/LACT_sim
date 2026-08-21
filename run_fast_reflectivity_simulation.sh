#!/bin/bash
set -euo pipefail

# ============================================================
# LACT fast reflectivity simulation
# ============================================================

# ============================================================
# 1. User configuration
# ============================================================

PROJECT_DIR="$(pwd)"

BUILD_DIR="build"
BUILD_JOBS=4

# 主光学配置：
# 提供望远镜、镜面、机械遮挡、输出靶面、镜面反射率文件等基础配置。
# 注意：其中 source.config 仅用于建立基础 SourceConfig；
# 实际平行光参数最终由 RUNTIME_CFG 中的 source.* 覆盖。
MAIN_CFG="configs/official_tests/perfect_parallel_raytrace_structure_whiteboard.cfg"

# 快速反射率模拟统一参数配置：
# 实际光源及快速反射率模拟相关参数均以该文件为准。
# source.*       : 平行光光子数、波长、位置、半径、方向、随机种子等
# cmos.*         : CMOS 位置、指向、镜头、FOV、外壳
#                  φ21 mm物理接收孔、有效入瞳及FOV
# target.*       : 漫反射靶中心、法向、尺寸、反射率、随机种子
# first_hit.*    : 镜面 / CMOS 第一交点互斥
# obstruction.*  : 运行时机械遮挡控制
# mirror.*       : 镜面反射率 Monte Carlo 控制
# output.*       : 输出文件
# diagnostics.*  : 统计与诊断输出
RUNTIME_CFG="configs/simulation_fast_reflectivity.cfg"

PARALLEL_EXE="${BUILD_DIR}/run_parallel_mirror_detector"
DIFFUSE_TARGET_EXE="${BUILD_DIR}/run_diffuse_target"
DIFFUSE_MD_EXE="${BUILD_DIR}/run_diffuse_mirror_detector"

TARGET_HITS="run_logs/official_tests/raytrace_structure_parallel/hits.csv"
DIFFUSE_HITS="diffuse_hits.csv"

PARALLEL_CMOS_HITS="parallel_cmos_hits.csv"
DIFFUSE_MIRROR_HITS="diffuse_mirror_hits.csv"
DIFFUSE_CMOS_HITS="diffuse_cmos_hits.csv"

LOG_DIR="run_logs/fast_reflectivity"

PARALLEL_LOG="${LOG_DIR}/01_parallel_mirror_detector.log"
DIFFUSE_TARGET_LOG="${LOG_DIR}/02_diffuse_target.log"
DIFFUSE_MD_LOG="${LOG_DIR}/03_diffuse_mirror_detector.log"
SUMMARY_FILE="${LOG_DIR}/summary.txt"

# ============================================================
# 2. Basic checks
# ============================================================

echo "============================================================"
echo "LACT Fast Reflectivity Simulation"
echo "============================================================"
echo "Project directory : ${PROJECT_DIR}"
echo "Main config       : ${MAIN_CFG}"
echo "Runtime config    : ${RUNTIME_CFG}"
echo "Build directory   : ${BUILD_DIR}"
echo "Log directory     : ${LOG_DIR}"
echo "============================================================"

if [ ! -f "${MAIN_CFG}" ]; then
    echo "ERROR: main config not found:"
    echo "       ${MAIN_CFG}"
    exit 1
fi

if [ ! -f "${RUNTIME_CFG}" ]; then
    echo "ERROR: runtime config not found:"
    echo "       ${RUNTIME_CFG}"
    exit 1
fi

if [ ! -f "apps/run_parallel_mirror_detector.cpp" ]; then
    echo "ERROR: apps/run_parallel_mirror_detector.cpp not found."
    exit 1
fi

if [ ! -f "apps/run_diffuse_target.cpp" ]; then
    echo "ERROR: apps/run_diffuse_target.cpp not found."
    exit 1
fi

if [ ! -f "apps/run_diffuse_mirror_detector.cpp" ]; then
    echo "ERROR: apps/run_diffuse_mirror_detector.cpp not found."
    exit 1
fi

mkdir -p "${LOG_DIR}"
mkdir -p "$(dirname "${TARGET_HITS}")"

# ============================================================
# 3. CMake configure
# ============================================================

echo
echo "============================================================"
echo "[1/6] Configure CMake"
echo "============================================================"

cmake -S . -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DLACT_ENABLE_HESSIO=OFF \
    -DCMAKE_C_COMPILER=/usr/bin/gcc \
    -DCMAKE_CXX_COMPILER=/usr/bin/g++

# ============================================================
# 4. Build
# ============================================================

echo
echo "============================================================"
echo "[2/6] Build simulation programs"
echo "============================================================"

cmake --build "${BUILD_DIR}" -j"${BUILD_JOBS}" \
    --target \
    run_parallel_mirror_detector \
    run_diffuse_target \
    run_diffuse_mirror_detector

for exe in \
    "${PARALLEL_EXE}" \
    "${DIFFUSE_TARGET_EXE}" \
    "${DIFFUSE_MD_EXE}"
do
    if [ ! -x "${exe}" ]; then
        echo "ERROR: executable not found:"
        echo "       ${exe}"
        exit 1
    fi
done

# ============================================================
# 5. Clean previous outputs
# ============================================================

echo
echo "============================================================"
echo "[3/6] Clean previous outputs"
echo "============================================================"

rm -f "${TARGET_HITS}"
rm -f "${DIFFUSE_HITS}"
rm -f "${PARALLEL_CMOS_HITS}"
rm -f "${DIFFUSE_MIRROR_HITS}"
rm -f "${DIFFUSE_CMOS_HITS}"

rm -f "${PARALLEL_LOG}"
rm -f "${DIFFUSE_TARGET_LOG}"
rm -f "${DIFFUSE_MD_LOG}"
rm -f "${SUMMARY_FILE}"

echo "Old simulation outputs removed."

# ============================================================
# 6. Parallel light -> Mirror / Detector
# ============================================================

echo
echo "============================================================"
echo "[4/6] Parallel light -> Mirror / Detector"
echo "============================================================"

env -u LD_LIBRARY_PATH \
"${PARALLEL_EXE}" \
"${MAIN_CFG}" \
"${RUNTIME_CFG}" \
2>&1 | tee "${PARALLEL_LOG}"

if [ ! -f "${TARGET_HITS}" ]; then
    echo
    echo "ERROR: target hits CSV was not generated:"
    echo "       ${TARGET_HITS}"
    exit 1
fi

echo
echo "Generated target hits:"
ls -lh "${TARGET_HITS}"

if [ -f "${PARALLEL_CMOS_HITS}" ]; then
    echo
    echo "Generated parallel CMOS hits:"
    ls -lh "${PARALLEL_CMOS_HITS}"
fi

# ============================================================
# 7. Target -> Lambert diffuse rays
# ============================================================

echo
echo "============================================================"
echo "[5/6] Diffuse target -> Lambert rays"
echo "============================================================"

env -u LD_LIBRARY_PATH \
"${DIFFUSE_TARGET_EXE}" \
"${TARGET_HITS}" \
"${RUNTIME_CFG}" \
2>&1 | tee "${DIFFUSE_TARGET_LOG}"

if [ ! -f "${DIFFUSE_HITS}" ]; then
    echo
    echo "ERROR: diffuse hits CSV was not generated:"
    echo "       ${DIFFUSE_HITS}"
    exit 1
fi

echo
echo "Generated diffuse rays:"
ls -lh "${DIFFUSE_HITS}"

# ============================================================
# 8. Diffuse rays -> Mirror / Detector
# ============================================================

echo
echo "============================================================"
echo "[6/6] Diffuse rays -> Mirror / Detector"
echo "============================================================"

env -u LD_LIBRARY_PATH \
"${DIFFUSE_MD_EXE}" \
"${DIFFUSE_HITS}" \
"${MAIN_CFG}" \
"${RUNTIME_CFG}" \
2>&1 | tee "${DIFFUSE_MD_LOG}"

echo
echo "============================================================"
echo "Simulation completed"
echo "============================================================"

# ============================================================
# 9. Output file check
# ============================================================

echo
echo "[Output files]"

for file in \
    "${TARGET_HITS}" \
    "${PARALLEL_CMOS_HITS}" \
    "${DIFFUSE_HITS}" \
    "${DIFFUSE_MIRROR_HITS}" \
    "${DIFFUSE_CMOS_HITS}"
do
    if [ -f "${file}" ]; then
        ls -lh "${file}"
    else
        echo "Not generated: ${file}"
    fi
done

# ============================================================
# 10. Extract machine-readable values
# ============================================================

get_value()
{
    local file="$1"
    local key="$2"

    grep -m1 "^${key}=" "${file}" 2>/dev/null |
        cut -d'=' -f2- ||
        true
}

TOTAL_PARALLEL_RAYS="$(
    get_value "${PARALLEL_LOG}" "total_parallel_rays"
)"

MIRROR_GEOMETRY_HITS="$(
    get_value "${PARALLEL_LOG}" "mirror_geometry_hits"
)"

PARALLEL_MIRROR_FIRST="$(
    get_value "${PARALLEL_LOG}" "mirror_first"
)"

PARALLEL_DETECTOR_FIRST="$(
    get_value "${PARALLEL_LOG}" "detector_first"
)"

BLOCKED_PARALLEL_TO_MIRROR="$(
    get_value "${PARALLEL_LOG}" "blocked_parallel_to_mirror"
)"

FINAL_PARALLEL_MIRROR_HITS="$(
    get_value "${PARALLEL_LOG}" "final_parallel_mirror_hits"
)"

ABSORBED_BY_MIRROR="$(
    get_value "${PARALLEL_LOG}" "absorbed_by_mirror"
)"

REFLECTED_BY_MIRROR="$(
    get_value "${PARALLEL_LOG}" "reflected_by_mirror"
)"

BLOCKED_MIRROR_TO_TARGET="$(
    get_value "${PARALLEL_LOG}" "blocked_mirror_to_target"
)"

FINAL_TARGET_HITS="$(
    get_value "${PARALLEL_LOG}" "final_target_hits"
)"

FINAL_PARALLEL_CMOS_HITS="$(
    get_value "${PARALLEL_LOG}" "final_parallel_cmos_hits"
)"

INPUT_TARGET_PHOTONS="$(
    get_value "${DIFFUSE_TARGET_LOG}" "input_target_photons"
)"

INSIDE_TARGET="$(
    get_value "${DIFFUSE_TARGET_LOG}" "inside_target"
)"

OUTSIDE_TARGET="$(
    get_value "${DIFFUSE_TARGET_LOG}" "outside_target"
)"

ABSORBED_BY_TARGET="$(
    get_value "${DIFFUSE_TARGET_LOG}" "absorbed_by_target"
)"

REFLECTED_BY_TARGET="$(
    get_value "${DIFFUSE_TARGET_LOG}" "reflected_by_target"
)"

DIFFUSE_PHOTONS="$(
    get_value "${DIFFUSE_TARGET_LOG}" "diffuse_photons"
)"

TOTAL_DIFFUSE_RAYS="$(
    get_value "${DIFFUSE_MD_LOG}" "total_diffuse_rays"
)"

DIFFUSE_MIRROR_FIRST="$(
    get_value "${DIFFUSE_MD_LOG}" "mirror_first"
)"

DIFFUSE_DETECTOR_FIRST="$(
    get_value "${DIFFUSE_MD_LOG}" "detector_first"
)"

BLOCKED_TARGET_TO_MIRROR="$(
    get_value "${DIFFUSE_MD_LOG}" "blocked_target_to_mirror"
)"

FINAL_DIFFUSE_MIRROR_HITS="$(
    get_value "${DIFFUSE_MD_LOG}" "final_mirror_hits"
)"

BLOCKED_TARGET_TO_DETECTOR="$(
    get_value "${DIFFUSE_MD_LOG}" "blocked_target_to_detector"
)"

FINAL_DIFFUSE_CMOS_HITS="$(
    get_value "${DIFFUSE_MD_LOG}" "final_cmos_hits"
)"

# ============================================================
# 11. Summary
# ============================================================

{
    echo "============================================================"
    echo "LACT Fast Reflectivity Simulation Summary"
    echo "============================================================"
    echo "Main config                     : ${MAIN_CFG}"
    echo "Runtime config                  : ${RUNTIME_CFG}"
    echo "------------------------------------------------------------"

    echo "[1] Parallel light"
    echo "Total parallel rays             : ${TOTAL_PARALLEL_RAYS:-N/A}"
    echo "Mirror geometry hits            : ${MIRROR_GEOMETRY_HITS:-N/A}"
    echo "Mirror first                    : ${PARALLEL_MIRROR_FIRST:-N/A}"
    echo "Detector first                  : ${PARALLEL_DETECTOR_FIRST:-N/A}"
    echo "Blocked parallel -> mirror      : ${BLOCKED_PARALLEL_TO_MIRROR:-N/A}"
    echo "Final parallel mirror hits      : ${FINAL_PARALLEL_MIRROR_HITS:-N/A}"
    echo "Absorbed by mirror              : ${ABSORBED_BY_MIRROR:-N/A}"
    echo "Reflected by mirror             : ${REFLECTED_BY_MIRROR:-N/A}"
    echo "Blocked mirror -> target        : ${BLOCKED_MIRROR_TO_TARGET:-N/A}"
    echo "Final target hits               : ${FINAL_TARGET_HITS:-N/A}"
    echo "Final parallel CMOS hits        : ${FINAL_PARALLEL_CMOS_HITS:-N/A}"
    echo "------------------------------------------------------------"

    echo "[2] Diffuse target"
    echo "Input target photons            : ${INPUT_TARGET_PHOTONS:-N/A}"
    echo "Inside target                   : ${INSIDE_TARGET:-N/A}"
    echo "Outside target                  : ${OUTSIDE_TARGET:-N/A}"
    echo "Absorbed by target              : ${ABSORBED_BY_TARGET:-N/A}"
    echo "Reflected by target             : ${REFLECTED_BY_TARGET:-N/A}"
    echo "Generated diffuse photons       : ${DIFFUSE_PHOTONS:-N/A}"
    echo "------------------------------------------------------------"

    echo "[3] Diffuse light"
    echo "Total diffuse rays              : ${TOTAL_DIFFUSE_RAYS:-N/A}"
    echo "Mirror first                    : ${DIFFUSE_MIRROR_FIRST:-N/A}"
    echo "Detector first                  : ${DIFFUSE_DETECTOR_FIRST:-N/A}"
    echo "Blocked target -> mirror        : ${BLOCKED_TARGET_TO_MIRROR:-N/A}"
    echo "Final diffuse mirror hits       : ${FINAL_DIFFUSE_MIRROR_HITS:-N/A}"
    echo "Blocked target -> detector      : ${BLOCKED_TARGET_TO_DETECTOR:-N/A}"
    echo "Final diffuse CMOS hits         : ${FINAL_DIFFUSE_CMOS_HITS:-N/A}"
    echo "------------------------------------------------------------"

    echo "[Output]"
    echo "Target hits CSV                 : ${TARGET_HITS}"
    echo "Parallel CMOS CSV               : ${PARALLEL_CMOS_HITS}"
    echo "Diffuse rays CSV                : ${DIFFUSE_HITS}"
    echo "Diffuse mirror CSV              : ${DIFFUSE_MIRROR_HITS}"
    echo "Diffuse CMOS CSV                : ${DIFFUSE_CMOS_HITS}"
    echo "============================================================"
} | tee "${SUMMARY_FILE}"

echo
echo "Summary saved to:"
echo "${SUMMARY_FILE}"
