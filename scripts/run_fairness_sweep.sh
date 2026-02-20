#!/bin/bash
set -euo pipefail

# ---- fixed experiment parameters ----
N_LEGACY=2
M_HE=8
SIM_TIME=1
PAYLOAD=1000
LAMBDA_LEGACY=5000
LAMBDA_HE=5000
N_RUNS=30
# -------------------------------------

# Sweep lists.
MU_INTERVALS=(0.0 0.1 0.05 0.01 0.001)
# Each entry is "CWMIN:CWMAX"
CW_PAIRS=(
  "15:1023"
  "15:512"
  "15:256"
  "7:128"
  "7:63"
  "3:63"
  "3:31"
  "3:15"
)

OUT_DIR="sweep-output"
mkdir -p "$OUT_DIR"

run_one_pair() {
  local mu_interval="$1"
  local cwmin="$2"
  local cwmax="$3"

  for i in $(seq 0 "$N_RUNS"); do
    ./ns3 run "fairness11ax \
      --nLegacy=$N_LEGACY \
      --mHe=$M_HE \
      --simTime=$SIM_TIME \
      --payloadSize=$PAYLOAD \
      --lambdaLegacy=$LAMBDA_LEGACY \
      --lambdaHe=$LAMBDA_HE \
      --apCwMin=$cwmin \
      --apCwMax=$cwmax \
      --muAccessReqInterval=$mu_interval" \
      --command-template="%s --RngRun=$i"
  done | awk -f ./process.awk
}

for mu in "${MU_INTERVALS[@]}"; do
  out_file="$OUT_DIR/fairness_nLegacy${N_LEGACY}_mHe${M_HE}_mu${mu}.txt"
  : > "$out_file"

  echo "===== Sweep configuration =====" >> "$out_file"
  echo "nLegacy             = $N_LEGACY" >> "$out_file"
  echo "mHe                 = $M_HE" >> "$out_file"
  echo "simTime             = $SIM_TIME" >> "$out_file"
  echo "payloadSize         = $PAYLOAD" >> "$out_file"
  echo "lambdaLegacy        = $LAMBDA_LEGACY" >> "$out_file"
  echo "lambdaHe            = $LAMBDA_HE" >> "$out_file"
  echo "muAccessReqInterval = $mu" >> "$out_file"
  echo "RngRuns             = 0..$N_RUNS" >> "$out_file"
  echo "===============================" >> "$out_file"
  echo "" >> "$out_file"

  for pair in "${CW_PAIRS[@]}"; do
    cwmin="${pair%%:*}"
    cwmax="${pair##*:}"

    echo "----- AP_CWMIN=$cwmin AP_CWMAX=$cwmax -----" >> "$out_file"
    run_one_pair "$mu" "$cwmin" "$cwmax" >> "$out_file"
    echo "" >> "$out_file"
  done
done
