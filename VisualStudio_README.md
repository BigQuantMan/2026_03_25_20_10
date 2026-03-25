# Visual Studio guide (Windows native)

This version keeps the original replay dry-run flow and adds:

- **Realtime market-data feed** via Binance USD-M Futures **REST polling**
- **Real Binance Futures test-order submission** via `POST /fapi/v1/order/test`
- Native Windows dependencies only: **WinHTTP** + **BCrypt (CNG HMAC-SHA256)**

## What is implemented

1. `Strategy::on_bar()` -> `std::vector<Order>`
2. `OrderMapper` -> Binance Futures style request
3. `BinanceFuturesGateway` -> dry-run or real `/fapi/v1/order/test`
4. `BinanceRestPollingFuturesFeed` -> polls closed klines and latest funding rate

## Important limitation

The realtime feed is **REST polling**, not websocket streaming. It is compatible with Visual Studio / Windows-only setups, but it is not as low-latency as websockets.

## Quick start

Open `trackA_live_vs.sln`, choose **Debug | x64**, then build.

### Replay dry-run (default)
Run with:

```text
--mode replay --futures-csv results\prepared\sample_futures_live.csv --strategy LiveFundingMomentum
```

### Realtime dry-run
This polls live Binance market data but keeps order submission in dry-run mode.

```text
--mode realtime --strategy LiveFundingMomentum --symbol BTCUSDT --interval 1m --poll-ms 1000
```

### Realtime + real Binance Futures TEST order
Set API credentials in your environment or pass them on the command line.

```text
--mode realtime --strategy LiveFundingMomentum --symbol BTCUSDT --interval 1m --poll-ms 1000 --send-test-order --advance-state-on-accept
```

Environment variables:

```text
BINANCE_API_KEY
BINANCE_API_SECRET
```

Default endpoints:

- market data: `https://fapi.binance.com`
- test order: `https://demo-fapi.binance.com`

### Why `--advance-state-on-accept` exists
`/fapi/v1/order/test` validates the order request but does not create a live matching-engine order. If you leave strategy state untouched, a signal can repeat every bar. This flag advances local strategy state after a successful test-order response so you can continue the experiment flow.

## Common commands

### Replay / dry-run
```text
--mode replay --futures-csv results\prepared\sample_futures_live.csv --strategy LiveFundingMomentum
```

### Realtime market data / dry-run orders
```text
--mode realtime --strategy LiveGoldenCross --symbol BTCUSDT --interval 1m --poll-ms 1000 --max-bars 30
```

### Realtime market data / real Binance Futures TEST order
```text
--mode realtime --strategy LiveFundingMomentum --symbol BTCUSDT --interval 1m --poll-ms 1000 --send-test-order --advance-state-on-accept --max-bars 20
```

## Notes

- Quantity filters are **not auto-normalized** in this version. Choose a valid quantity for the selected symbol.
- The realtime feed emits **closed bars only**.
- FundingMomentum uses the latest `premiumIndex` funding rate snapshot attached to the newly closed bar.
