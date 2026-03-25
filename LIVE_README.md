# Live replay bridge 추가 설명

이 프로젝트에는 원래 다음 흐름만 있었습니다.

- 전략 -> `std::vector<Order>` 생성
- `Backtester` / `Portfolio` 가 내부 체결

이번 추가 파일들은 여기에 아래 경로를 하나 더 만든 것입니다.

- 전략 -> `std::vector<Order>` 생성
- `OrderMapper` 가 `Order` 를 `LiveOrderRequest` 로 변환
- `BinanceFuturesGateway` 가 Binance Futures REST 요청 생성
- `LiveTrader` 가 replay feed 를 돌면서 주문 제출

## 무엇이 실제로 구현되었나

- `main_live.cpp`
  - 백테스터 대신 live bridge 실행기 진입점
- `include/live/*`, `src/live/*`
  - 주문 변환 / HTTP / HMAC / Binance Futures Gateway / live runner
- `strategies/live_futures_strategies.hpp`
  - on_fill 기준으로 상태를 갱신하는 live-safe 전략 2종

## 중요한 한계

1. **시장 데이터는 아직 실시간 websocket 이 아니라 CSV replay 입니다.**
   - 즉, 전략 입력은 기존 `FuturesFeed` 를 그대로 사용합니다.
   - 실시간 websocket -> `MarketSnapshot` 생성기는 아직 붙이지 않았습니다.

2. **기본값은 dry-run 입니다.**
   - 실수로 실주문이 나가는 것을 막기 위해 `--send-live` 를 줘야 실제 HTTP 호출을 합니다.

3. **기본값은 `/fapi/v1/order/test` 입니다.**
   - 즉, Binance test order endpoint 를 사용합니다.
   - `--real-order` 를 줘야 `/fapi/v1/order` 를 사용합니다.

4. **기본적으로 성공 응답을 fill 로 간주하도록 설정했습니다.**
   - replay 전략 상태가 계속 진행되도록 하기 위한 타협입니다.
   - 실제 운영 시에는 `--no-assume-fill` 로 끄고, 추후 user data stream 기반 체결 확인 계층을 붙이는 것이 맞습니다.

## 빌드

```bash
g++ -std=c++17 -O2 -Wall -Iinclude \
    main_live.cpp \
    src/futures_feed.cpp \
    src/live/order_mapper.cpp \
    src/live/binance_futures_gateway.cpp \
    src/live/curl_http_client.cpp \
    src/live/hmac_sha256_signer.cpp \
    src/live/live_trader.cpp \
    -lcurl -lssl -lcrypto \
    -o live_replay_trader
```

또는 Makefile 의 새 타깃:

```bash
make live
```

## 실행 예시

기본 dry-run:

```bash
./live_replay_trader \
  --futures-csv results/prepared/futures_is_data.csv \
  --strategy LiveFundingMomentum
```

실제 HTTP 호출 + test endpoint:

```bash
BINANCE_API_KEY=... BINANCE_API_SECRET=... \
./live_replay_trader \
  --futures-csv results/prepared/futures_is_data.csv \
  --strategy LiveFundingMomentum \
  --send-live
```

실주문 endpoint 사용:

```bash
BINANCE_API_KEY=... BINANCE_API_SECRET=... \
./live_replay_trader \
  --futures-csv results/prepared/futures_is_data.csv \
  --strategy LiveFundingMomentum \
  --send-live --real-order
```
