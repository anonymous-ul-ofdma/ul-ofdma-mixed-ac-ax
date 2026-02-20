#!/bin/bash

# ---- experiment parameters ----
N_LEGACY=3
M_HE=3
SIM_TIME=1
PAYLOAD=1000
LAMBDA_LEGACY=100
LAMBDA_HE=5000
AP_CWMIN=7
AP_CWMAX=63
MU_INTERVAL=0.05
N_RUNS=30
# --------------------------------

for i in $(seq 0 $N_RUNS); do
  ./ns3 run "fairness11ax \
    --nLegacy=$N_LEGACY \
    --mHe=$M_HE \
    --simTime=$SIM_TIME \
    --payloadSize=$PAYLOAD \
    --lambdaLegacy=$LAMBDA_LEGACY \
    --lambdaHe=$LAMBDA_HE \
    --apCwMin=$AP_CWMIN \
    --apCwMax=$AP_CWMAX \
    --muAccessReqInterval=$MU_INTERVAL" \
    --command-template="%s --RngRun=$i"
done | awk -f ./process.awk

# ---- print experiment configuration (footer) ----
echo ""
echo "===== Experiment configuration ====="
echo "nLegacy             = $N_LEGACY"
echo "mHe                 = $M_HE"
echo "simTime             = $SIM_TIME"
echo "payloadSize         = $PAYLOAD"
echo "lambdaLegacy        = $LAMBDA_LEGACY"
echo "lambdaHe            = $LAMBDA_HE"
echo "apCwMin             = $AP_CWMIN"
echo "apCwMax             = $AP_CWMAX"
echo "muAccessReqInterval = $MU_INTERVAL"
echo "RngRuns             = 0..$N_RUNS"
echo "===================================="
