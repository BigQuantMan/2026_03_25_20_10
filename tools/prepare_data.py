#!/usr/bin/env python3
"""
prepare_data.py  —  parquet → CSV 변환

parquet 실제 스키마 (persister.py 기준):
  timestamp, symbol, event_type,
  strike_price, option_type, expiry_date,
  mark_price, index_price, estimated_settle_price,
  best_bid_price, best_ask_price, best_bid_quantity, best_ask_quantity,
  bid_iv, ask_iv, mark_iv,
  high_price_limit, low_price_limit,
  risk_free_rate,
  delta, theta, gamma, vega

출력 CSV 스펙 (backtester 표준):
  timestamp, symbol, strike, expiry, type,
  mark_price, mark_iv, index_price,
  delta, gamma, theta, vega,
  best_bid, best_ask, open_interest
"""

import sys
from pathlib import Path

import numpy as np
import pandas as pd

# ── 경로 설정 ──────────────────────────────────────────────────────
REPO_ROOT  = Path(__file__).parent.parent
DATA_DIR   = REPO_ROOT.parent / "data"
OUTPUT_DIR = REPO_ROOT / "results" / "prepared"
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

OOS_DAYS  = 7       # 최근 7일 = Out-of-Sample (Admin only)
RESAMPLE  = "5min"

# parquet → CSV 컬럼 매핑
COL_MAP = {
    "best_bid_price": "best_bid",
    "best_ask_price": "best_ask",
    # 나머지는 동일 이름 사용
}

# 최종 출력 컬럼 순서
OUTPUT_COLS = [
    "timestamp", "symbol", "strike", "expiry", "type",
    "mark_price", "mark_iv", "index_price",
    "delta", "gamma", "theta", "vega",
    "best_bid", "best_ask", "open_interest",
]

# 집계할 수치 컬럼 (resample 대상)
NUMERIC_COLS = [
    "mark_price", "mark_iv", "index_price",
    "delta", "gamma", "theta", "vega",
    "best_bid_price", "best_ask_price",
]

# ── 파일명 파싱 ────────────────────────────────────────────────────
def parse_filename(name: str) -> dict | None:
    stem = Path(name).stem          # "BTC-260125-89000-C"
    parts = stem.split("-")
    if len(parts) != 4:
        return None
    _, date_str, strike_str, opt_type = parts
    year  = 2000 + int(date_str[:2])
    month = int(date_str[2:4])
    day   = int(date_str[4:6])
    return {
        "strike": int(strike_str),
        "expiry": f"{year}-{month:02d}-{day:02d}",
        "type"  : opt_type,
    }

# ── 단일 parquet 로드 ─────────────────────────────────────────────
def load_parquet(filepath: Path) -> pd.DataFrame | None:
    info = parse_filename(filepath.name)
    if not info:
        return None

    try:
        df = pd.read_parquet(filepath)
    except Exception as e:
        print(f"  [SKIP] {filepath.name}: read error ({e})", file=sys.stderr)
        return None

    if df.empty or "timestamp" not in df.columns:
        return None

    # ── timestamp 정규화 ─────────────────────────────────────────
    df = df.sort_values("timestamp").reset_index(drop=True)
    if df["timestamp"].dt.tz is None:
        df["timestamp"] = df["timestamp"].dt.tz_localize("UTC")

    # ── 리샘플: 수치 컬럼만 명시적으로 집계 ────────────────────
    # pandas 2.x 호환: 수치 컬럼만 골라서 resample하면 안전
    agg_cols = {col: "last" for col in NUMERIC_COLS if col in df.columns}

    if "mark_price" not in agg_cols:
        return None

    df_r = (df.set_index("timestamp")
              .resample(RESAMPLE)
              .agg(agg_cols)
              .reset_index())

    # mark_price NaN / 0 행 제거
    df_r = df_r.dropna(subset=["mark_price"])
    df_r = df_r[df_r["mark_price"] > 0]

    if df_r.empty:
        return None

    # ── 컬럼명 매핑 (best_bid_price → best_bid 등) ──────────────
    df_r = df_r.rename(columns=COL_MAP)

    # ── 메타 컬럼: 파일명에서 파싱 ──────────────────────────────
    df_r["symbol"] = filepath.stem
    df_r["strike"] = info["strike"]
    df_r["expiry"] = info["expiry"]
    df_r["type"]   = info["type"]

    # ── timestamp → Unix ms ──────────────────────────────────────
    # parquet 저장 형식(ms/us/ns)에 관계없이 정확하게 변환:
    # datetime64[any] → datetime64[ns] → int64(ns) → // 1e6 → ms
    ts_utc = pd.to_datetime(df_r["timestamp"], utc=True)
    df_r["timestamp"] = (
        ts_utc.values.astype("datetime64[ns]").astype(np.int64) // 1_000_000
    )

    # ── 없는 컬럼 0으로 채우기 ───────────────────────────────────
    for col in ["mark_iv", "index_price",
                "delta", "gamma", "theta", "vega",
                "best_bid", "best_ask"]:
        if col not in df_r.columns:
            df_r[col] = 0.0

    # open_interest는 parquet에 없으므로 0
    df_r["open_interest"] = 0.0

    return df_r[OUTPUT_COLS]

# ── 전체 로드 ──────────────────────────────────────────────────────
def load_all() -> pd.DataFrame:
    files = sorted(DATA_DIR.rglob("BTC-*.parquet"))
    print(f"Found {len(files)} parquet files")

    dfs = []
    skipped = 0
    for i, f in enumerate(files):
        df = load_parquet(f)
        if df is not None:
            dfs.append(df)
        else:
            skipped += 1

        # 진행 상황 출력
        if (i + 1) % 200 == 0:
            print(f"  {i+1}/{len(files)} processed ({len(dfs)} loaded, {skipped} skipped)")

    print(f"  {len(files)}/{len(files)} done. ({len(dfs)} loaded, {skipped} skipped)")

    if not dfs:
        print("No data loaded!", file=sys.stderr)
        sys.exit(1)

    all_df = pd.concat(dfs, ignore_index=True)
    all_df = all_df.sort_values("timestamp").reset_index(drop=True)
    print(f"Total rows: {len(all_df):,}")
    return all_df

# ── IS / OOS 분리 ──────────────────────────────────────────────────
def split_is_oos(df: pd.DataFrame) -> tuple[pd.DataFrame, pd.DataFrame]:
    max_ts = df["timestamp"].max()
    cutoff = max_ts - OOS_DAYS * 24 * 3600 * 1000  # ms

    is_df  = df[df["timestamp"] <  cutoff].copy()
    oos_df = df[df["timestamp"] >= cutoff].copy()
    return is_df, oos_df

# ── 저장 ──────────────────────────────────────────────────────────
def save_csv(df: pd.DataFrame, name: str):
    path = OUTPUT_DIR / name
    if df.empty:
        print(f"  → {path.name}: 0 rows  [empty — skipped]")
        return
    df.to_csv(path, index=False)
    ts_min = pd.Timestamp(int(df["timestamp"].min()), unit="ms", tz="UTC")
    ts_max = pd.Timestamp(int(df["timestamp"].max()), unit="ms", tz="UTC")
    print(f"  → {path.name}: {len(df):,} rows  [{ts_min.date()} ~ {ts_max.date()}]")

# ── 메인 ──────────────────────────────────────────────────────────
if __name__ == "__main__":
    print("=" * 50)
    print("  Binance Backtester — Data Preparation")
    print(f"  Data dir : {DATA_DIR}")
    print(f"  OOS days : {OOS_DAYS}  (hidden from researchers)")
    print(f"  Resample : {RESAMPLE}")
    print("=" * 50)

    all_df = load_all()

    is_df, oos_df = split_is_oos(all_df)
    print(f"\nIS  rows : {len(is_df):,}")
    print(f"OOS rows : {len(oos_df):,}  ← Admin only")

    print("\nSaving...")
    save_csv(all_df, "all_data.csv")
    save_csv(is_df,  "is_data.csv")
    save_csv(oos_df, "oos_data.csv")

    print("\n✅ Done. Files in:", OUTPUT_DIR)
