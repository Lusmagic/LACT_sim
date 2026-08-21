#!/bin/bash

set -e

# ============================================================
# LACT reflectance Monte Carlo production batch
#
# Usage:
#   ./run_batch.sh <ProcId> [Offset]
#
# Example:
#   ./run_batch.sh 7 500
#
# gives:
#   BATCH_ID = 507
#
# Each BATCH_ID gets a deterministic pseudo-random 64-bit seed.
# The same BATCH_ID always gets the same seed.
# ============================================================


# ============================================================
# 0. Arguments
# ============================================================

PROC_ID=$1
OFFSET=${2:-0}

if [ -z "$PROC_ID" ]; then
    echo "ERROR: missing ProcId"
    echo "Usage:"
    echo "  $0 <ProcId> [Offset]"
    exit 1
fi

BATCH_ID=$((OFFSET + PROC_ID))


# ============================================================
# 1. Global parameters
# ============================================================

N_PHOTONS=10000000

PROJECT_DIR=/afs/ihep.ac.cn/users/l/liushuo/home/LACT_sim-main

cd "$PROJECT_DIR" || exit 1


# ============================================================
# 2. Generate deterministic pseudo-random 64-bit seed
#
# Same BATCH_ID -> same seed
# Different BATCH_ID -> different pseudo-random seed
#
# SHA256 is used only to generate the seed.
# Actual photon Monte Carlo RNG is still mt19937_64 in C++.
# ============================================================

SEED=$(python3 - "$BATCH_ID" << 'PY'
import sys
import hashlib

batch_id = int(sys.argv[1])

key = f"LACT_reflectance_{batch_id}".encode("utf-8")

digest = hashlib.sha256(key).digest()

seed = int.from_bytes(
    digest[:8],
    byteorder="little",
    signed=False
)

print(seed)
PY
)

if [ -z "$SEED" ]; then
    echo "ERROR: failed to generate random seed"
    exit 2
fi


# ============================================================
# 3. Batch names
# ============================================================

BATCH_TAG=$(printf "%06d" "$BATCH_ID")

RESULT_DIR="${PROJECT_DIR}/batch_results/batch_${BATCH_TAG}"

mkdir -p "$RESULT_DIR"


# ============================================================
# 4. Worker-node temporary directory
# ============================================================

if [ -n "$TMPDIR" ]; then
    WORK_DIR="${TMPDIR}/lact_${USER}_${BATCH_TAG}"
else
    WORK_DIR="/tmp/lact_${USER}_${BATCH_TAG}"
fi

mkdir -p "$WORK_DIR"


# ============================================================
# 5. Paths
# ============================================================

HITS_CSV="${WORK_DIR}/hits.csv"

DIFFUSE_HITS_CSV="${WORK_DIR}/diffuse_hits.csv"

SOURCE_CFG="${PROJECT_DIR}/configs/sources/parallel_batch_${BATCH_TAG}.cfg"

MAIN_CFG="${PROJECT_DIR}/configs/official_tests/parallel_batch_${BATCH_TAG}.cfg"

SUMMARY_FILE="${RESULT_DIR}/summary.txt"

INFO_FILE="${RESULT_DIR}/batch_info.txt"


# ============================================================
# 6. Failure handler
# ============================================================

cleanup_on_error()
{
    EXIT_CODE=$?

    if [ "$EXIT_CODE" -ne 0 ]; then

        echo
        echo "============================================================"
        echo "BATCH FAILED"
        echo "============================================================"
        echo "batch_id=${BATCH_ID}"
        echo "random_seed=${SEED}"
        echo "exit_code=${EXIT_CODE}"
        echo "time=$(date)"

        cat >> "$INFO_FILE" << EOF
end_time=$(date)
status=failed
exit_code=${EXIT_CODE}
EOF

        echo
        echo "Temporary files retained for debugging:"
        echo "$WORK_DIR"
    fi

    exit "$EXIT_CODE"
}

trap cleanup_on_error EXIT


# ============================================================
# 7. Print batch information
# ============================================================

echo "============================================================"
echo "LACT reflectance production batch"
echo "============================================================"
echo "proc_id        = ${PROC_ID}"
echo "offset         = ${OFFSET}"
echo "batch_id       = ${BATCH_ID}"
echo "batch_tag      = ${BATCH_TAG}"
echo "n_photons      = ${N_PHOTONS}"
echo "random_seed    = ${SEED}"
echo "hostname       = $(hostname)"
echo "work_dir       = ${WORK_DIR}"
echo "result_dir     = ${RESULT_DIR}"
echo "start_time     = $(date)"
echo "============================================================"


# ============================================================
# 8. Write batch metadata
# ============================================================

cat > "$INFO_FILE" << EOF
proc_id=${PROC_ID}
offset=${OFFSET}
batch_id=${BATCH_ID}
batch_tag=${BATCH_TAG}
n_photons=${N_PHOTONS}
random_seed=${SEED}
hostname=$(hostname)
work_dir=${WORK_DIR}
start_time=$(date)
status=running
EOF


# ============================================================
# 9. Generate source configuration
# ============================================================

cat > "$SOURCE_CFG" << EOF
mode=ParallelBeam
n_bunches=${N_PHOTONS}
multiplicity=1
wavelength_nm=400
time_ns=0
photon_weight=1
source_plane_z=1
beam_radius_m=4
beam_direction=0,0,-1
random_seed=${SEED}
EOF


# ============================================================
# 10. Generate main optical configuration
# ============================================================

cat > "$MAIN_CFG" << EOF
telescope.config=telescope_1229_minimal.cfg
mirror.config=../mirrors/mirror_1229_imported.cfg
source.config=../sources/parallel_batch_${BATCH_TAG}.cfg
output.config=../outputs/whiteboard_f8.cfg
obstruction.config=../obstructions/raytrace_final_structure.cfg

propagation.speed_of_light_m_per_ns=0.299792458

output.mode=hits
output.csv=${HITS_CSV}
EOF


# ============================================================
# STEP 1/5
#
# Parallel light
#       ->
# Mirror
#       ->
# Diffuse target / output plane
#
# Generates:
#   hits.csv
# ============================================================

echo
echo "============================================================"
echo "[1/5] Parallel -> Mirror -> Target"
echo "============================================================"

/usr/bin/time -p \
env -u LD_LIBRARY_PATH \
"${PROJECT_DIR}/build/run_optical_sim_test" \
"$MAIN_CFG" \
> "${RESULT_DIR}/optical.log" \
2> "${RESULT_DIR}/optical_time.log"


if [ ! -s "$HITS_CSV" ]; then
    echo "ERROR: hits.csv missing or empty"
    exit 11
fi


wc -l "$HITS_CSV" \
> "${RESULT_DIR}/hits_lines.txt"


# ============================================================
# STEP 2/5
#
# Parallel light
#       ->
# CMOS detector
#
# Uses the same:
#   N_PHOTONS
#   random_seed
# ============================================================

echo
echo "============================================================"
echo "[2/5] Parallel -> CMOS"
echo "============================================================"

/usr/bin/time -p \
env -u LD_LIBRARY_PATH \
"${PROJECT_DIR}/build/run_parallel_cmos_hit" \
"$MAIN_CFG" \
> "${RESULT_DIR}/parallel_cmos.log" \
2> "${RESULT_DIR}/parallel_cmos_time.log"


# ============================================================
# STEP 3/5
#
# Target hits
#       ->
# Lambert diffuse rays
#
# Generates:
#   diffuse_hits.csv
# ============================================================

echo
echo "============================================================"
echo "[3/5] Target -> Lambert"
echo "============================================================"

cd "$WORK_DIR"

/usr/bin/time -p \
env -u LD_LIBRARY_PATH \
"${PROJECT_DIR}/build/run_diffuse_target" \
"$HITS_CSV" \
> "${RESULT_DIR}/diffuse_target.log" \
2> "${RESULT_DIR}/diffuse_target_time.log"


if [ ! -s "$DIFFUSE_HITS_CSV" ]; then
    echo "ERROR: diffuse_hits.csv missing or empty"
    exit 12
fi


wc -l "$DIFFUSE_HITS_CSV" \
> "${RESULT_DIR}/diffuse_hits_lines.txt"


cd "$PROJECT_DIR"


# ============================================================
# STEP 4/5
#
# Lambert diffuse target
#       ->
# Telescope mirror
# ============================================================

echo
echo "============================================================"
echo "[4/5] Lambert -> Mirror"
echo "============================================================"

/usr/bin/time -p \
env -u LD_LIBRARY_PATH \
"${PROJECT_DIR}/build/run_diffuse_mirror_hit" \
"$DIFFUSE_HITS_CSV" \
"$MAIN_CFG" \
> "${RESULT_DIR}/diffuse_mirror.log" \
2> "${RESULT_DIR}/diffuse_mirror_time.log"


# ============================================================
# STEP 5/5
#
# Lambert diffuse target
#       ->
# CMOS detector
# ============================================================

echo
echo "============================================================"
echo "[5/5] Lambert -> CMOS"
echo "============================================================"

/usr/bin/time -p \
env -u LD_LIBRARY_PATH \
"${PROJECT_DIR}/build/run_diffuse_cmos_hit" \
"$DIFFUSE_HITS_CSV" \
> "${RESULT_DIR}/diffuse_cmos.log" \
2> "${RESULT_DIR}/diffuse_cmos_time.log"


# ============================================================
# 11. Extract machine-readable counters
# ============================================================

OPT_TOTAL=$(
    grep '^total_photons=' \
    "${RESULT_DIR}/optical.log" \
    | tail -1 \
    | cut -d= -f2
)


OPT_MIRROR=$(
    grep '^hit_mirror=' \
    "${RESULT_DIR}/optical.log" \
    | tail -1 \
    | cut -d= -f2
)


OPT_TARGET=$(
    grep '^hit_output_plane=' \
    "${RESULT_DIR}/optical.log" \
    | tail -1 \
    | cut -d= -f2
)


DIRECT_CMOS=$(
    grep '^hit_cmos' \
    "${RESULT_DIR}/parallel_cmos.log" \
    | tail -1 \
    | awk -F= '
    {
        gsub(/ /,"",$2);
        print $2
    }'
)


DIFFUSE_TOTAL=$(
    grep '^total_diffuse_rays' \
    "${RESULT_DIR}/diffuse_mirror.log" \
    | tail -1 \
    | awk -F= '
    {
        gsub(/ /,"",$2);
        print $2
    }'
)


DIFFUSE_MIRROR=$(
    grep '^hit_mirror ' \
    "${RESULT_DIR}/diffuse_mirror.log" \
    | tail -1 \
    | awk -F= '
    {
        gsub(/ /,"",$2);
        print $2
    }'
)


DIFFUSE_CMOS=$(
    grep '^hit_cmos' \
    "${RESULT_DIR}/diffuse_cmos.log" \
    | tail -1 \
    | awk -F= '
    {
        gsub(/ /,"",$2);
        print $2
    }'
)


# ============================================================
# 12. Validate extracted counters
# ============================================================

for VALUE in \
"$OPT_TOTAL" \
"$OPT_MIRROR" \
"$OPT_TARGET" \
"$DIRECT_CMOS" \
"$DIFFUSE_TOTAL" \
"$DIFFUSE_MIRROR" \
"$DIFFUSE_CMOS"
do

    if [ -z "$VALUE" ]; then
        echo "ERROR: failed to extract summary counters"
        exit 13
    fi

done


# ============================================================
# 13. Write final summary.txt
#
# IMPORTANT:
# random_seed is explicitly recorded here.
# ============================================================

cat > "$SUMMARY_FILE" << EOF
============================================================
LACT Monte Carlo Batch Summary
============================================================

batch_id=${BATCH_ID}
proc_id=${PROC_ID}
offset=${OFFSET}

random_seed=${SEED}

n_photons_requested=${N_PHOTONS}

------------------------------------------------------------
Parallel optical path
------------------------------------------------------------

source_total=${OPT_TOTAL}
optical_hit_mirror=${OPT_MIRROR}
optical_hit_target=${OPT_TARGET}

------------------------------------------------------------
Direct CMOS
------------------------------------------------------------

direct_cmos_hit=${DIRECT_CMOS}

------------------------------------------------------------
Diffuse target path
------------------------------------------------------------

diffuse_total=${DIFFUSE_TOTAL}
diffuse_mirror_hit=${DIFFUSE_MIRROR}
diffuse_cmos_hit=${DIFFUSE_CMOS}

------------------------------------------------------------
Runtime information
------------------------------------------------------------

hostname=$(hostname)
status=completed
EOF


# ============================================================
# 14. Mark batch complete
# ============================================================

cat >> "$INFO_FILE" << EOF
end_time=$(date)
status=completed
exit_code=0
EOF


# ============================================================
# 15. Print summary before cleanup
# ============================================================

echo
echo "============================================================"
echo "BATCH RESULT"
echo "============================================================"

cat "$SUMMARY_FILE"


# ============================================================
# 16. Delete temporary large files
#
# The important statistical results have already been saved
# in summary.txt and individual logs.
# ============================================================

echo
echo "============================================================"
echo "Cleaning temporary files"
echo "============================================================"

rm -f "$HITS_CSV"

rm -f "$DIFFUSE_HITS_CSV"

rm -f "$SOURCE_CFG"

rm -f "$MAIN_CFG"

rm -rf "$WORK_DIR"


# ============================================================
# 17. Finish
# ============================================================

echo
echo "============================================================"
echo "BATCH COMPLETED"
echo "============================================================"
echo "batch_id=${BATCH_ID}"
echo "random_seed=${SEED}"
echo "summary=${SUMMARY_FILE}"
echo "end_time=$(date)"

trap - EXIT

exit 0
