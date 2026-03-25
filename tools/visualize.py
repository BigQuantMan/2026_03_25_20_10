#!/usr/bin/env python3
"""
visualize.py  —  백테스트 결과 JSON → 차트
results/ 폴더의 가장 최근 JSON 파일을 자동으로 읽어 시각화한다.
"""

import json
import sys
from pathlib import Path
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
import numpy as np

RESULTS_DIR = Path(__file__).parent.parent / "results"

# ── 가장 최근 결과 파일 찾기 ──────────────────────────────────────
def find_latest_result() -> Path | None:
    files = sorted(RESULTS_DIR.glob("*.json"), key=lambda f: f.stat().st_mtime)
    return files[-1] if files else None

def load_result(path: Path) -> dict:
    with open(path) as f:
        return json.load(f)

# ── 시각화 ────────────────────────────────────────────────────────
def plot(result: dict, save_path: Path | None = None):
    strategy  = result["strategy"]
    metrics   = result["metrics"]
    eq_curve  = result["equity_curve"]   # [[ts_ms, value], ...]
    trades    = result["trades"]

    # equity curve → DataFrame
    eq_df = pd.DataFrame(eq_curve, columns=["ts", "value"])
    eq_df["dt"] = pd.to_datetime(eq_df["ts"], unit="ms", utc=True)
    eq_df["dt"] = eq_df["dt"].dt.tz_localize(None)

    # trades → DataFrame
    tr_df = pd.DataFrame(trades) if trades else pd.DataFrame()
    if not tr_df.empty:
        tr_df["dt"] = pd.to_datetime(tr_df["ts"], unit="ms", utc=True)
        tr_df["dt"] = tr_df["dt"].dt.tz_localize(None)

    fig, axes = plt.subplots(2, 2, figsize=(18, 10))
    fig.suptitle(
        f"Backtest Result — {strategy}\n"
        f"Sharpe: {metrics.get('sharpe', 0):.3f}  |  "
        f"Return: {metrics.get('total_return_pct', 0):.2f}%  |  "
        f"Avg Daily Turnover: {metrics.get('avg_daily_turnover', 0):.4f}",
        fontsize=13, fontweight="bold"
    )

    # ── (1) Equity Curve ──────────────────────────────────────
    ax = axes[0, 0]
    initial = eq_df["value"].iloc[0] if not eq_df.empty else 1
    total_ret = metrics.get("total_return_pct", 0)
    color = "limegreen" if total_ret >= 0 else "tomato"
    ax.plot(eq_df["dt"], eq_df["value"], color=color, lw=1.5)
    ax.fill_between(eq_df["dt"], eq_df["value"], initial, alpha=0.15, color=color)
    ax.axhline(initial, color="gray", lw=0.8, linestyle="--")
    ax.set_title(f"Equity Curve  ({total_ret:+.2f}%)")
    ax.set_ylabel("Portfolio Value (USDT)")
    ax.yaxis.set_major_formatter(plt.FuncFormatter(lambda x, _: f"{x:,.0f}"))
    ax.xaxis.set_major_formatter(mdates.DateFormatter("%m/%d"))
    ax.grid(alpha=0.25)

    # ── (2) 일별 수익률 ───────────────────────────────────────
    ax = axes[0, 1]
    daily = eq_df.set_index("dt")["value"].resample("1D").last().dropna()
    daily_ret = daily.pct_change().dropna() * 100
    colors_d = ["limegreen" if v >= 0 else "tomato" for v in daily_ret]
    ax.bar(daily_ret.index, daily_ret.values, color=colors_d, alpha=0.8, width=0.8)
    ax.axhline(0, color="gray", lw=0.8)
    ax.set_title("Daily Returns (%)")
    ax.set_ylabel("%")
    ax.xaxis.set_major_formatter(mdates.DateFormatter("%m/%d"))
    ax.grid(alpha=0.25, axis="y")

    # ── (3) 거래 분포 ─────────────────────────────────────────
    ax = axes[1, 0]
    if not tr_df.empty:
        instr_counts = tr_df["instr"].value_counts()
        side_counts  = tr_df["side"].value_counts()
        ax.bar(instr_counts.index, instr_counts.values, color="steelblue", alpha=0.8)
        ax.set_title(f"Trade Count by Instrument  (total {len(tr_df)})")
        ax.set_ylabel("Count")
        ax.grid(alpha=0.25, axis="y")
    else:
        ax.text(0.5, 0.5, "No trades", ha="center", va="center",
                transform=ax.transAxes, fontsize=14, color="gray")
        ax.set_title("Trade Count by Instrument")

    # ── (4) 메트릭 요약 텍스트 ───────────────────────────────
    ax = axes[1, 1]
    ax.axis("off")
    lines = [
        f"Strategy     : {strategy}",
        f"Period       : {result['period']['start'][:10]} ~ {result['period']['end'][:10]}",
        "",
        f"Sharpe       : {metrics.get('sharpe', 0):.4f}",
        f"Total Return : {metrics.get('total_return_pct', 0):.2f}%",
        f"Avg Turnover : {metrics.get('avg_daily_turnover', 0):.4f}",
        "",
        f"Total Trades : {len(tr_df) if not tr_df.empty else 0}",
    ]
    ax.text(0.05, 0.95, "\n".join(lines),
            transform=ax.transAxes, fontsize=12, va="top",
            fontfamily="monospace",
            bbox=dict(boxstyle="round", facecolor="lightgray", alpha=0.3))
    ax.set_title("Summary")

    plt.tight_layout()

    if save_path:
        plt.savefig(save_path, dpi=150, bbox_inches="tight")
        print(f"Chart saved: {save_path}")
    else:
        plt.show()

# ── 메인 ──────────────────────────────────────────────────────────
if __name__ == "__main__":
    json_path = Path(sys.argv[1]) if len(sys.argv) > 1 else find_latest_result()

    if not json_path or not json_path.exists():
        print("No result file found. Run backtester first.", file=sys.stderr)
        sys.exit(1)

    print(f"Visualizing: {json_path.name}")
    result = load_result(json_path)

    # PNG 저장 (같은 폴더에 같은 이름으로)
    save_path = json_path.with_suffix(".png")
    plot(result, save_path=save_path)
