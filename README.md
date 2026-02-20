# ul-ofdma-mixed-ac-ax
Reproducibility artifact for a mixed IEEE 802.11ac/ax uplink study: UL OFDMA opportunity vs AP contention tuning under legacy-protection constraints (ns-3.46 + post-processing scripts + figures).

````markdown
# Reproducibility Artifact: Fairness-Constrained UL OFDMA in Mixed 802.11ac/ax WLANs (ns-3.46)

This repository is the reproducibility artifact for a paper studying **uplink (UL) OFDMA in IEEE 802.11ax** under **mixed legacy coexistence** with IEEE 802.11ac stations.  
The artifact provides:

- **ns-3.46 simulation scenario** (mixed 11ac/11ax, saturated uplink)
- Parameter sweeps for:
  - **UL OFDMA opportunity** via `muAccessReqInterval` (denoted μ)
  - **AP EDCA contention** via `(CWmin, CWmax)` pairs
- **Post-processing script** to extract:
  - average throughput per station (HE and legacy)
  - total network throughput
  - Jain’s fairness index between HE and legacy groups
- **Raw output logs** and scripts to reproduce the figures/tables in the paper

> **Anonymous review note:** author identity is intentionally omitted.  
> The artifact is self-contained and can be executed without any private dependencies.

---

## Repository Structure

- `ns3/`  
  - `fairness11ax.cc` : main ns-3.46 simulation scenario (mixed HE/legacy UL)
  - (optional) patches / helper files if you used them
- `scripts/`  
  - `process.awk` : parses ns-3 output logs and summarizes metrics
  - `plot_results.py` : regenerates figures (if provided)
- `results/`  
  - `fairness_nLegacy*_mHe*_mu*.txt` : raw logs for each scenario and μ
  - `processed/` : parsed CSV summaries (if generated)
  - `figs/` : regenerated figures (optional)
- `README.md` : this file

---

## Requirements

### A) Build and run simulations
- ns-3 **version 3.46**
- A Linux environment is recommended (Ubuntu 20.04/22.04 tested)
- Build tools: `g++`, `python3`, `cmake`, `ninja` (or waf depending on your ns-3 build)
- `awk` (GNU awk)

### B) Post-processing and plotting (optional)
- `python3`
- `numpy`, `pandas`, `matplotlib`

Install Python deps:
```bash
pip3 install -r scripts/requirements.txt
````

---

## Step 1 — Install ns-3.46

If you already have ns-3.46, skip this step.

Example (official ns-3):

```bash
git clone https://gitlab.com/nsnam/ns-3-dev.git ns-3-dev
cd ns-3-dev
git checkout ns-3.46
```

Build:

```bash
./ns3 configure -d optimized
./ns3 build
```

---

## Step 2 — Copy the scenario into ns-3.46

Copy `fairness11ax.cc` into ns-3.46 `scratch/` (or wherever you keep custom scenarios):

```bash
cp ns3/fairness11ax.cc /path/to/ns-3-dev/scratch/
```

If you modified any ns-3 Wi-Fi module source files, place them under `ns3/patches/` and apply them (optional):

```bash
cd /path/to/ns-3-dev
git apply /path/to/repo/ns3/patches/*.patch
./ns3 build
```

---

## Step 3 — Run the simulation

From the ns-3.46 root directory:

```bash
cd /path/to/ns-3-dev
./ns3 run "scratch/fairness11ax --nLegacy=5 --mHe=5 --mu=0.01 --apCwMin=15 --apCwMax=1023 --seed=1"
```

### Parameter meanings (paper notation)

* `nLegacy` : number of legacy (802.11ac) stations (n_L)
* `mHe`     : number of HE (802.11ax) stations (n_H)
* `mu`      : `muAccessReqInterval` controlling UL OFDMA opportunity (μ)
* `apCwMin` / `apCwMax` : AP EDCA contention window parameters
* `seed`    : RNG seed / run index

> The paper uses sweeps over μ and multiple `(CWmin, CWmax)` pairs for each μ.

---

## Step 4 — Sweep μ and AP CW pairs (batch run)

You can run all experiments using a simple bash loop. Example:

```bash
for mu in 0.0 0.001 0.01 0.05 0.1; do
  for cwmin in 7 15 31; do
    for cwmax in 127 255 511 1023; do
      ./ns3 run "scratch/fairness11ax --nLegacy=5 --mHe=5 --mu=${mu} --apCwMin=${cwmin} --apCwMax=${cwmax} --seed=1" \
        | tee results/raw_nL5_mH5_mu${mu}_cw${cwmin}_${cwmax}_seed1.txt
    done
  done
done
```

Repeat over seeds to compute averages:

```bash
for seed in 1 2 3 4 5; do
  # same loops as above; just pass --seed=${seed}
done
```

---

## Step 5 — Post-process logs into metrics (AWK)

The script `scripts/process.awk` extracts:

* average throughput per HE station
* average throughput per legacy station
* total throughput
* Jain fairness between HE and legacy groups

Example:

```bash
gawk -f scripts/process.awk results/raw_nL5_mH5_mu0.01_cw15_1023_seed1.txt > results/processed/summary_seed1.txt
```

To process a directory:

```bash
mkdir -p results/processed
for f in results/*.txt; do
  base=$(basename "$f" .txt)
  gawk -f scripts/process.awk "$f" > "results/processed/${base}.summary"
done
```

> If your `process.awk` expects a specific log format (markers/strings), keep the ns-3 output unchanged.

---

## Step 6 — Regenerate paper figures (Python)

If `scripts/plot_results.py` is provided, run:

```bash
python3 scripts/plot_results.py \
  --input results/processed \
  --outdir results/figs
```

This will regenerate figures such as:

* best feasible throughput vs μ (observed vs model)
* throughput–fairness scatter per μ (CW sweep)
* predicted vs observed throughput/fairness (model validation)

---

## Precomputed Results (Included)

For convenience, the repository includes the raw log files used in the paper:

* `results/fairness_nLegacy2_mHe8_mu*.txt`
* `results/fairness_nLegacy5_mHe5_mu*.txt`
* `results/fairness_nLegacy8_mHe2_mu*.txt`

These correspond to three population mixes:

* HE-dominant: (n_L, n_H) = (2, 8)
* balanced:    (5, 5)
* legacy-dominant: (8, 2)

---

## Notes on “gaps” in best-feasible curves

Some plots (e.g., best-feasible throughput vs μ under fairness constraint) may show **gaps**.
These indicate μ values for which **no tested (CWmin, CWmax) pair satisfies the fairness constraint** (i.e., the constrained optimization is infeasible over the explored CW set).
This is expected and is discussed in the paper as a feasibility boundary.

---

## Contact / Issues

For anonymous review, please use GitHub Issues for questions or reproduction problems.
