#!/usr/bin/env python3
import argparse
import glob
import os
import re
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt


# -----------------------------
# Parsing utilities
# -----------------------------

@dataclass
class BlockResult:
    n_legacy: int
    n_he: int
    mu: float
    ap_cwmin: int
    ap_cwmax: int
    thr_total_mbps: float
    thr_he_avg_mbps: float
    thr_legacy_avg_mbps: float
    jain_group: float
    src_file: str


RE_HEADER_NL = re.compile(r"^\s*nLegacy\s*=\s*(\d+)\s*$", re.MULTILINE)
RE_HEADER_NH = re.compile(r"^\s*mHe\s*=\s*(\d+)\s*$", re.MULTILINE)
RE_HEADER_MU = re.compile(r"^\s*muAccessReqInterval\s*=\s*([0-9.eE+-]+)\s*$", re.MULTILINE)

RE_BLOCK_CW = re.compile(r"^-{3,}\s*AP_CWMIN\s*=\s*(\d+)\s*AP_CWMAX\s*=\s*(\d+)\s*-{3,}\s*$",
                         re.MULTILINE)

RE_NET_THR = re.compile(r"^Network throughput .*=\s*([0-9.]+)\s*Mbps\s*$", re.MULTILINE)
RE_GROUP_AVG = re.compile(
    r"^Group-average throughput .*:\s*HE\(11ax\)\s*=\s*([0-9.]+)\s*Mbps,\s*Legacy\(11ac\)\s*=\s*([0-9.]+)\s*Mbps\s*$",
    re.MULTILINE
)
RE_JAIN = re.compile(r"^Jain_group.*=\s*([0-9.]+)\s*$", re.MULTILINE)


def parse_one_file(path: str) -> List[BlockResult]:
    """
    Parse a single fairness_nLegacy*_mHe*_mu*.txt file.
    Returns a list of BlockResult, one per AP_CWMIN/AP_CWMAX block.
    """
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        txt = f.read()

    m_nl = RE_HEADER_NL.search(txt)
    m_nh = RE_HEADER_NH.search(txt)
    m_mu = RE_HEADER_MU.search(txt)
    if not (m_nl and m_nh and m_mu):
        raise ValueError(f"Cannot find header (nLegacy/mHe/muAccessReqInterval) in: {path}")

    n_legacy = int(m_nl.group(1))
    n_he = int(m_nh.group(1))
    mu = float(m_mu.group(1))

    # Find each CW block and parse summaries inside it.
    blocks = []
    cw_matches = list(RE_BLOCK_CW.finditer(txt))
    if not cw_matches:
        raise ValueError(f"No CW blocks found in: {path}")

    for i, m in enumerate(cw_matches):
        ap_cwmin = int(m.group(1))
        ap_cwmax = int(m.group(2))
        start = m.end()
        end = cw_matches[i + 1].start() if i + 1 < len(cw_matches) else len(txt)
        seg = txt[start:end]

        m_thr = RE_NET_THR.search(seg)
        m_avg = RE_GROUP_AVG.search(seg)
        m_j = RE_JAIN.search(seg)
        if not (m_thr and m_avg and m_j):
            # Some blocks might be incomplete; skip safely
            continue

        thr_total = float(m_thr.group(1))
        thr_he_avg = float(m_avg.group(1))
        thr_leg_avg = float(m_avg.group(2))
        jain = float(m_j.group(1))

        blocks.append(BlockResult(
            n_legacy=n_legacy, n_he=n_he, mu=mu,
            ap_cwmin=ap_cwmin, ap_cwmax=ap_cwmax,
            thr_total_mbps=thr_total,
            thr_he_avg_mbps=thr_he_avg,
            thr_legacy_avg_mbps=thr_leg_avg,
            jain_group=jain,
            src_file=os.path.basename(path)
        ))
    return blocks


def load_all_results(input_dir: str, pattern: str = "fairness_nLegacy*_mHe*_mu*.txt") -> pd.DataFrame:
    files = sorted(glob.glob(os.path.join(input_dir, pattern)))
    if not files:
        raise FileNotFoundError(f"No files matched: {os.path.join(input_dir, pattern)}")

    rows: List[Dict] = []
    for fp in files:
        for br in parse_one_file(fp):
            rows.append({
                "nLegacy": br.n_legacy,
                "nHe": br.n_he,
                "mu": br.mu,
                "apCwMin": br.ap_cwmin,
                "apCwMax": br.ap_cwmax,
                "thr_total_mbps": br.thr_total_mbps,
                "thr_he_avg_mbps": br.thr_he_avg_mbps,
                "thr_legacy_avg_mbps": br.thr_legacy_avg_mbps,
                "jain_group": br.jain_group,
                "src_file": br.src_file,
            })

    df = pd.DataFrame(rows)
    if df.empty:
        raise RuntimeError("Parsed zero valid CW blocks. Check log formatting or regex.")
    return df


# -----------------------------
# Plotting
# -----------------------------

def ensure_dir(d: str) -> None:
    os.makedirs(d, exist_ok=True)


def scenario_tag(nL: int, nH: int) -> str:
    return f"nL{nL}_nH{nH}"


def plot_scatter_thr_vs_fairness(df: pd.DataFrame, outdir: str, by_mu: bool = False) -> None:
    """
    Scatter plot of total throughput vs Jain fairness for each scenario.
    If by_mu=True, generate one figure per mu; otherwise one combined figure per scenario.
    """
    ensure_dir(outdir)
    scenarios = sorted(df[["nLegacy", "nHe"]].drop_duplicates().itertuples(index=False, name=None))

    for nL, nH in scenarios:
        sdf = df[(df["nLegacy"] == nL) & (df["nHe"] == nH)].copy()
        sdf.sort_values(["mu", "apCwMin", "apCwMax"], inplace=True)

        if by_mu:
            for mu, mdf in sdf.groupby("mu"):
                plt.figure()
                plt.scatter(mdf["jain_group"], mdf["thr_total_mbps"])
                plt.xlabel("Jain fairness (HE vs Legacy)")
                plt.ylabel("Total throughput (Mbps)")
                plt.title(f"Throughput vs Fairness ({scenario_tag(nL,nH)}), mu={mu:g}")
                plt.grid(True, linewidth=0.3)
                fn = os.path.join(outdir, f"scatter_thr_vs_fairness_{scenario_tag(nL,nH)}_mu{mu:g}.png")
                plt.tight_layout()
                plt.savefig(fn, dpi=200)
                plt.close()
        else:
            plt.figure()
            # color by mu (no explicit colormap selection; matplotlib will choose defaults)
            mus = sorted(sdf["mu"].unique())
            for mu in mus:
                mdf = sdf[sdf["mu"] == mu]
                plt.scatter(mdf["jain_group"], mdf["thr_total_mbps"], label=f"mu={mu:g}", s=25)
            plt.xlabel("Jain fairness (HE vs Legacy)")
            plt.ylabel("Total throughput (Mbps)")
            plt.title(f"Throughput vs Fairness ({scenario_tag(nL,nH)})")
            plt.grid(True, linewidth=0.3)
            plt.legend()
            fn = os.path.join(outdir, f"scatter_thr_vs_fairness_{scenario_tag(nL,nH)}.png")
            plt.tight_layout()
            plt.savefig(fn, dpi=200)
            plt.close()


def compute_best_feasible(df: pd.DataFrame, eta: float) -> pd.DataFrame:
    """
    For each (nLegacy,nHe,mu), choose the CW pair that maximizes throughput among those with jain >= eta.
    Returns rows: nLegacy,nHe,mu,best_thr_obs,best_cwmin,best_cwmax,best_jain
    Missing feasibility -> NaN best_thr_obs.
    """
    out_rows = []
    for (nL, nH, mu), g in df.groupby(["nLegacy", "nHe", "mu"]):
        feas = g[g["jain_group"] >= eta].copy()
        if feas.empty:
            out_rows.append({
                "nLegacy": nL, "nHe": nH, "mu": mu,
                "best_thr_obs": np.nan,
                "best_cwmin_obs": np.nan,
                "best_cwmax_obs": np.nan,
                "best_jain_obs": np.nan
            })
            continue
        best = feas.loc[feas["thr_total_mbps"].idxmax()]
        out_rows.append({
            "nLegacy": nL, "nHe": nH, "mu": mu,
            "best_thr_obs": float(best["thr_total_mbps"]),
            "best_cwmin_obs": int(best["apCwMin"]),
            "best_cwmax_obs": int(best["apCwMax"]),
            "best_jain_obs": float(best["jain_group"])
        })
    out = pd.DataFrame(out_rows).sort_values(["nLegacy", "nHe", "mu"])
    return out


def load_model_best(model_csv: str) -> pd.DataFrame:
    """
    Optional: load a model best-throughput file for overlay.
    Expected columns (minimum):
      nLegacy, nHe, mu, best_thr_model
    """
    m = pd.read_csv(model_csv)
    required = {"nLegacy", "nHe", "mu", "best_thr_model"}
    if not required.issubset(set(m.columns)):
        raise ValueError(f"Model CSV must contain columns: {sorted(required)}")
    return m


def plot_best_thr_vs_mu(best_obs: pd.DataFrame, outdir: str, eta: float,
                        model_best: Optional[pd.DataFrame] = None) -> None:
    ensure_dir(outdir)
    scenarios = sorted(best_obs[["nLegacy", "nHe"]].drop_duplicates().itertuples(index=False, name=None))

    for nL, nH in scenarios:
        sdf = best_obs[(best_obs["nLegacy"] == nL) & (best_obs["nHe"] == nH)].copy()
        sdf.sort_values("mu", inplace=True)

        plt.figure()
        plt.plot(sdf["mu"], sdf["best_thr_obs"], marker="o", label="Observed (ns-3)")

        if model_best is not None:
            mdf = model_best[(model_best["nLegacy"] == nL) & (model_best["nHe"] == nH)].copy()
            mdf.sort_values("mu", inplace=True)
            plt.plot(mdf["mu"], mdf["best_thr_model"], marker="s", label="Model")

        plt.xlabel("mu (muAccessReqInterval)")
        plt.ylabel("Best feasible total throughput (Mbps)")
        plt.title(f"Best-feasible throughput vs mu ({scenario_tag(nL,nH)}), eta={eta:g}")
        plt.grid(True, linewidth=0.3)
        plt.legend()
        fn = os.path.join(outdir, f"best_thr_vs_mu_{scenario_tag(nL,nH)}.png")
        plt.tight_layout()
        plt.savefig(fn, dpi=200)
        plt.close()

        # If model exists, also export an explicit obs-vs-model named file (for your paper naming)
        if model_best is not None:
            fn2 = os.path.join(outdir, f"best_thr_vs_mu_{scenario_tag(nL,nH)}_obs_vs_model.png")
            # copy by re-saving same fig name pattern is not possible after close; re-plot quickly:
            plt.figure()
            plt.plot(sdf["mu"], sdf["best_thr_obs"], marker="o", label="Observed (ns-3)")
            mdf = model_best[(model_best["nLegacy"] == nL) & (model_best["nHe"] == nH)].copy()
            mdf.sort_values("mu", inplace=True)
            plt.plot(mdf["mu"], mdf["best_thr_model"], marker="s", label="Model")
            plt.xlabel("mu (muAccessReqInterval)")
            plt.ylabel("Best feasible total throughput (Mbps)")
            plt.title(f"Best-feasible throughput vs mu ({scenario_tag(nL,nH)}), eta={eta:g}")
            plt.grid(True, linewidth=0.3)
            plt.legend()
            plt.tight_layout()
            plt.savefig(fn2, dpi=200)
            plt.close()


def main():
    ap = argparse.ArgumentParser(description="Plot ns-3 fairness sweep results from fairness_nLegacy*_mHe*_mu*.txt files.")
    ap.add_argument("--input", required=True, help="Directory containing fairness_nLegacy*_mHe*_mu*.txt files")
    ap.add_argument("--outdir", default="figs", help="Output directory for figures")
    ap.add_argument("--eta", type=float, default=0.95, help="Fairness threshold for best-feasible throughput")
    ap.add_argument("--scatter_by_mu", action="store_true", help="Generate scatter plot per mu (more figures)")
    ap.add_argument("--model_best_csv", default=None,
                    help="Optional CSV to overlay model results. Columns: nLegacy,nHe,mu,best_thr_model")
    ap.add_argument("--export_csv", action="store_true", help="Export parsed CW-level table and best-feasible table as CSV")
    args = ap.parse_args()

    df = load_all_results(args.input)

    ensure_dir(args.outdir)

    # Optional exports
    if args.export_csv:
        df.to_csv(os.path.join(args.outdir, "parsed_cw_level.csv"), index=False)

    # Scatter: throughput vs fairness
    plot_scatter_thr_vs_fairness(df, args.outdir, by_mu=args.scatter_by_mu)

    # Best-feasible: throughput vs mu
    best_obs = compute_best_feasible(df, eta=args.eta)
    if args.export_csv:
        best_obs.to_csv(os.path.join(args.outdir, f"best_feasible_eta{args.eta:g}.csv"), index=False)

    model_best = load_model_best(args.model_best_csv) if args.model_best_csv else None
    plot_best_thr_vs_mu(best_obs, args.outdir, eta=args.eta, model_best=model_best)

    print(f"Done. Figures saved to: {args.outdir}")


if __name__ == "__main__":
    main()
