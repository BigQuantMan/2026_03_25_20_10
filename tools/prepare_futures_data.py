#!/usr/bin/env python3
"""
prepare_futures_data.py  —  선물 parquet → 백테스터 CSV 변환
=============================================================

fetch_history.py 로 수집한 선물 parquet 파일들을 읽어
백테스터(FuturesFeed)가 소비하는 CSV 한 장으로 합칩니다.

입력 구조:
  <history_dir>/
    BTCUSDT/
      klines_1m.parquet       ← OHLCV (필수)
      funding_rate.parquet    ← 8h 펀딩비 (없으면 0)
      open_interest.parquet   ← 1h OI    (없으면 0)
      ls_ratio.parquet        ← 1h L/S   (없으면 0)
      taker_ratio.parquet     ← 1h Taker (없으면 0)
    ETHUSDT/ ...

출력 CSV 스펙 (FuturesFeed 표준):
  timestamp, symbol,
  open, high, low, close, volume, quote_volume,
  funding_rate, open_interest, long_ratio, short_ratio, taker_buy_ratio

사용법:
  python3 tools/prepare_futures_data.py
  python3 tools/prepare_futures_data.py --interval 1h  --days 30
  python3 tools/prepare_futures_data.py --data-dir /path/to/history --out results/prepared
"""

import argparse
import json
import sys
from pathlib import Path

import numpy as np
import pandas as pd

# ── 기본 경로 ──────────────────────────────────────────────────────
REPO_ROOT    = Path(__file__).parent.parent
DEFAULT_DATA = REPO_ROOT.parent / "binance-futures-collector" / "data" / "history"
OUTPUT_DIR   = REPO_ROOT / "results" / "prepared"

# 출력 컬럼 순서 (FuturesFeed CSV 표준)
OUTPUT_COLS = [
    "timestamp", "symbol",
    "open", "high", "low", "close", "volume", "quote_volume",
    "funding_rate", "open_interest", "long_ratio", "short_ratio", "taker_buy_ratio",
    "is_active",  # 1 = 해당 월 top-N 유니버스 포함, 0 = 미포함 (데이터는 있으나 거래 제외)
]

OOS_DAYS = 7  # 최근 7일 = Out-of-Sample


def read_parquet_safe(path: Path) -> pd.DataFrame | None:
    """parquet 읽기 (없거나 실패하면 None)"""
    if not path.exists():
        return None
    try:
        df = pd.read_parquet(path)
        if df.empty:
            return None
        # timestamp 정규화: UTC-aware
        if "timestamp" in df.columns:
            if df["timestamp"].dt.tz is None:
                df["timestamp"] = df["timestamp"].dt.tz_localize("UTC")
            else:
                df["timestamp"] = df["timestamp"].dt.tz_convert("UTC")
        return df
    except Exception as e:
        print(f"  [WARN] {path.name}: {e}", file=sys.stderr)
        return None


def to_pandas_freq(interval: str) -> str:
    """Binance 스타일 interval → pandas resample 주파수 변환
    pandas 2.2+ 에서 'M'(month)와 'm'(deprecated→month) 혼동 방지:
      1m, 5m, 15m, 30m → min (minutes)
      1h, 4h           → h  (hours)
      1d               → D  (days)
    """
    mapping = {
        "1m": "1min", "3m": "3min", "5m": "5min",
        "15m": "15min", "30m": "30min",
        "1h": "1h",  "2h": "2h",  "4h": "4h",  "6h": "6h",  "12h": "12h",
        "1d": "1D",  "3d": "3D",  "1w": "1W",
    }
    return mapping.get(interval, interval)


def resample_klines(df: pd.DataFrame, interval: str) -> pd.DataFrame:
    """klines를 지정 interval로 리샘플 (OHLCV 규칙 적용)"""
    freq = to_pandas_freq(interval)
    df = df.set_index("timestamp").sort_index()
    resampled = df.resample(freq).agg({
        "open":           "first",
        "high":           "max",
        "low":            "min",
        "close":          "last",
        "volume":         "sum",
        "quote_volume":   "sum",
    }).dropna(subset=["close"])
    resampled = resampled[resampled["close"] > 0]
    return resampled.reset_index()


def load_symbol(sym_dir: Path, interval: str) -> pd.DataFrame | None:
    """심볼 1개의 모든 데이터를 interval 기준으로 통합"""
    symbol = sym_dir.name
    freq   = to_pandas_freq(interval)   # pandas resample 주파수

    # ── 1. klines (필수) ──────────────────────────────────────────
    # 지원 interval 중 존재하는 것 사용 (1m 우선)
    klines_df = None
    for klines_interval in ["1m", "3m", "5m", "15m", "30m", "1h"]:
        kf = read_parquet_safe(sym_dir / f"klines_{klines_interval}.parquet")
        if kf is not None:
            klines_df = kf
            break

    if klines_df is None:
        print(f"  [SKIP] {symbol}: klines not found")
        return None

    # 필요 컬럼 확인
    required = {"timestamp", "open", "high", "low", "close", "volume"}
    if not required.issubset(klines_df.columns):
        print(f"  [SKIP] {symbol}: missing klines columns")
        return None

    # klines → 목표 interval로 리샘플
    klines_df = resample_klines(klines_df, interval)
    if klines_df.empty:
        return None

    # timestamp → int64 (Unix ms) 로 변환
    klines_df = klines_df.set_index("timestamp")

    # ── 2. Funding Rate (8h → interval ffill) ────────────────────
    fund_df = read_parquet_safe(sym_dir / "funding_rate.parquet")
    if fund_df is not None and "funding_rate" in fund_df.columns:
        fund_df = (fund_df[["timestamp", "funding_rate"]]
                   .set_index("timestamp")
                   .resample(freq).last()
                   .ffill())
        klines_df = klines_df.join(fund_df[["funding_rate"]], how="left")
    if "funding_rate" not in klines_df.columns:
        klines_df["funding_rate"] = 0.0
    klines_df["funding_rate"] = klines_df["funding_rate"].fillna(0.0)

    # ── 3. Open Interest (1h → interval) ─────────────────────────
    oi_df = read_parquet_safe(sym_dir / "open_interest.parquet")
    if oi_df is not None and "open_interest" in oi_df.columns:
        oi_df = (oi_df[["timestamp", "open_interest"]]
                 .set_index("timestamp")
                 .resample(freq).last()
                 .ffill())
        klines_df = klines_df.join(oi_df[["open_interest"]], how="left")
    if "open_interest" not in klines_df.columns:
        klines_df["open_interest"] = 0.0
    klines_df["open_interest"] = klines_df["open_interest"].fillna(0.0)

    # ── 4. Long/Short Ratio (1h → interval) ──────────────────────
    ls_df = read_parquet_safe(sym_dir / "ls_ratio.parquet")
    if ls_df is not None and {"long_ratio", "short_ratio"}.issubset(ls_df.columns):
        ls_df = (ls_df[["timestamp", "long_ratio", "short_ratio"]]
                 .set_index("timestamp")
                 .resample(freq).last()
                 .ffill())
        klines_df = klines_df.join(ls_df[["long_ratio", "short_ratio"]], how="left")
    if "long_ratio" not in klines_df.columns:
        klines_df["long_ratio"] = 0.0
    if "short_ratio" not in klines_df.columns:
        klines_df["short_ratio"] = 0.0
    klines_df[["long_ratio", "short_ratio"]] = (
        klines_df[["long_ratio", "short_ratio"]].fillna(0.0)
    )

    # ── 5. Taker Buy/Sell Ratio (1h → interval) ──────────────────
    tk_df = read_parquet_safe(sym_dir / "taker_ratio.parquet")
    if tk_df is not None and "buy_sell_ratio" in tk_df.columns:
        # buy_sell_ratio → taker_buy_ratio (buy / (buy + sell))
        if {"buy_vol", "sell_vol"}.issubset(tk_df.columns):
            tk_df["taker_buy_ratio"] = (
                tk_df["buy_vol"] / (tk_df["buy_vol"] + tk_df["sell_vol"] + 1e-12)
            )
        else:
            tk_df["taker_buy_ratio"] = tk_df["buy_sell_ratio"] / (1 + tk_df["buy_sell_ratio"])
        tk_df = (tk_df[["timestamp", "taker_buy_ratio"]]
                 .set_index("timestamp")
                 .resample(freq).last()
                 .ffill())
        klines_df = klines_df.join(tk_df[["taker_buy_ratio"]], how="left")
    if "taker_buy_ratio" not in klines_df.columns:
        klines_df["taker_buy_ratio"] = 0.0
    klines_df["taker_buy_ratio"] = klines_df["taker_buy_ratio"].fillna(0.0)

    # ── 타임스탬프 → Unix ms ──────────────────────────────────────
    klines_df = klines_df.reset_index()
    ts_utc = pd.to_datetime(klines_df["timestamp"], utc=True)
    klines_df["timestamp"] = (
        ts_utc.values.astype("datetime64[ns]").astype(np.int64) // 1_000_000
    )

    klines_df["symbol"] = symbol

    # quote_volume 컬럼 확인
    if "quote_volume" not in klines_df.columns:
        klines_df["quote_volume"] = 0.0

    # is_active: universe.json 적용 전 임시로 1로 설정 (main에서 덮어씀)
    klines_df["is_active"] = 1

    return klines_df[OUTPUT_COLS]


def main():
    parser = argparse.ArgumentParser(description="선물 parquet → 백테스터 CSV 변환")
    parser.add_argument("--data-dir", type=str, default=str(DEFAULT_DATA),
                        help=f"history 폴더 경로 (기본: {DEFAULT_DATA})")
    parser.add_argument("--out", type=str, default=str(OUTPUT_DIR),
                        help=f"출력 폴더 (기본: {OUTPUT_DIR})")
    parser.add_argument("--interval", type=str, default="1m",
                        help="목표 바 간격 (기본: 1m; 예: 1m, 5m, 15m, 1h)")
    parser.add_argument("--days",  type=int,  default=None,
                        help="최근 N일만 포함 (기본: 전체, --start 와 병용 불가)")
    parser.add_argument("--start", type=str,  default=None,
                        help="데이터 시작일 (YYYY-MM-DD, 예: 2025-02-01)")
    parser.add_argument("--end",   type=str,  default=None,
                        help="데이터 종료일 (YYYY-MM-DD, 포함, 기본: 전체)")
    parser.add_argument("--oos-days", type=int, default=OOS_DAYS,
                        help=f"Out-of-Sample 일수 (기본: {OOS_DAYS})")
    args = parser.parse_args()

    data_dir   = Path(args.data_dir)
    output_dir = Path(args.out)
    output_dir.mkdir(parents=True, exist_ok=True)

    if not data_dir.exists():
        print(f"[ERROR] data-dir not found: {data_dir}", file=sys.stderr)
        sys.exit(1)

    sym_dirs = sorted([d for d in data_dir.iterdir() if d.is_dir()])
    if not sym_dirs:
        print(f"[ERROR] No symbol directories in {data_dir}", file=sys.stderr)
        sys.exit(1)

    print()
    print("=" * 60)
    print("  Binance Futures Backtester — Data Preparation")
    print("=" * 60)
    print(f"  data-dir  : {data_dir}")
    print(f"  interval  : {args.interval}")
    print(f"  symbols   : {len(sym_dirs)}")
    print(f"  output    : {output_dir}")
    print()

    # ── universe.json 로드 (월별 top-N 유니버스) ──────────────────
    universe_path = data_dir / "universe.json"
    universe: dict[str, list[str]] = {}  # {"2025-01": ["BTCUSDT", ...], ...}
    if universe_path.exists():
        with open(universe_path) as f:
            universe = json.load(f)
        print(f"  universe  : {universe_path}  ({len(universe)}개월)")
    else:
        print(f"  universe  : 없음 (universe.json 미발견 → is_active 전체 1로 설정)")
        print(f"              compute_universe.py 를 실행하면 월별 top-N 필터가 활성화됩니다.")
    print()

    dfs = []
    for sym_dir in sym_dirs:
        df = load_symbol(sym_dir, args.interval)
        if df is not None and not df.empty:
            dfs.append(df)
            n = len(df)
            ts_min = pd.Timestamp(int(df["timestamp"].min()), unit="ms", tz="UTC")
            ts_max = pd.Timestamp(int(df["timestamp"].max()), unit="ms", tz="UTC")
            print(f"  ✓ {sym_dir.name:<14} {n:>7,} rows  [{ts_min.date()} ~ {ts_max.date()}]")

    if not dfs:
        print("[ERROR] No data loaded!", file=sys.stderr)
        sys.exit(1)

    all_df = pd.concat(dfs, ignore_index=True)
    all_df = all_df.sort_values(["timestamp", "symbol"]).reset_index(drop=True)

    # ── is_active 컬럼 계산 ────────────────────────────────────────
    if universe:
        # 월(YYYY-MM) 컬럼 생성
        ts_dt = pd.to_datetime(all_df["timestamp"], unit="ms", utc=True)
        all_df["_month"] = ts_dt.dt.to_period("M").astype(str)

        # 월 → active set 사전 구축 (forward-fill: 해당 월 없으면 직전 월 사용)
        months_sorted = sorted(universe.keys())
        month_to_set: dict[str, set | None] = {}
        for m in all_df["_month"].unique():
            prev = [x for x in months_sorted if x <= m]
            if prev:
                month_to_set[m] = set(universe[prev[-1]])
            else:
                month_to_set[m] = None  # 직전 월 없음 → 전체 활성

        # 벡터화: 월별 groupby → isin 마스크 (row-by-row apply 대비 수십 배 빠름)
        is_active_arr = np.ones(len(all_df), dtype=np.int8)
        for m, grp in all_df.groupby("_month"):
            syms = month_to_set.get(m)
            if syms is not None:
                inactive_mask = ~grp["symbol"].isin(syms)
                is_active_arr[grp.index[inactive_mask]] = 0
        all_df["is_active"] = is_active_arr
        all_df = all_df.drop(columns=["_month"])

        n_active   = (all_df["is_active"] == 1).sum()
        n_inactive = (all_df["is_active"] == 0).sum()
        print(f"\n  is_active=1 : {n_active:,} rows  (top-N 유니버스 내)")
        print(f"  is_active=0 : {n_inactive:,} rows  (유니버스 밖, mark-to-market용)")
    else:
        all_df["is_active"] = 1

    # ── 날짜 범위 필터링 ──────────────────────────────────────────
    if args.start:
        start_ms = int(pd.Timestamp(args.start, tz="UTC").timestamp() * 1000)
        all_df   = all_df[all_df["timestamp"] >= start_ms].copy()
        print(f"\n  → start 필터: {args.start} 이후  ({len(all_df):,} rows)")
    if args.end:
        # end 날짜는 해당 날 23:59:59 까지 포함
        end_ms = int((pd.Timestamp(args.end, tz="UTC") + pd.Timedelta(days=1) - pd.Timedelta(milliseconds=1)).timestamp() * 1000)
        all_df  = all_df[all_df["timestamp"] <= end_ms].copy()
        print(f"  → end   필터: {args.end} 이전  ({len(all_df):,} rows)")
    if args.days is not None and args.start is None:
        max_ts  = all_df["timestamp"].max()
        cutoff  = max_ts - args.days * 24 * 3600 * 1000
        all_df  = all_df[all_df["timestamp"] >= cutoff].copy()
        print(f"\n  → 최근 {args.days}일 필터 적용: {len(all_df):,} rows")

    # IS / OOS 분리
    max_ts  = all_df["timestamp"].max()
    oos_cut = max_ts - args.oos_days * 24 * 3600 * 1000
    is_df   = all_df[all_df["timestamp"] <  oos_cut].copy()
    oos_df  = all_df[all_df["timestamp"] >= oos_cut].copy()

    print()
    print(f"  Total rows : {len(all_df):,}")
    print(f"  IS  rows   : {len(is_df):,}")
    print(f"  OOS rows   : {len(oos_df):,}  ← Admin only")
    print()

    def save_csv(df: pd.DataFrame, name: str):
        path = output_dir / name
        if df.empty:
            print(f"  → {name}: 0 rows [empty — skipped]")
            return
        df.to_csv(path, index=False)
        ts_min = pd.Timestamp(int(df["timestamp"].min()), unit="ms", tz="UTC")
        ts_max = pd.Timestamp(int(df["timestamp"].max()), unit="ms", tz="UTC")
        syms   = df["symbol"].nunique()
        print(f"  → {name}: {len(df):,} rows  [{ts_min.date()} ~ {ts_max.date()}]  ({syms} symbols)")

    print("Saving...")
    save_csv(all_df, "futures_data.csv")
    save_csv(is_df,  "futures_is_data.csv")
    save_csv(oos_df, "futures_oos_data.csv")

    print()
    print("  ✅ Done!")
    print()
    print("  실행 예시:")
    print(f"    ./backtester results/prepared/futures_is_data.csv --mode futures --verbose")
    print()


if __name__ == "__main__":
    main()
