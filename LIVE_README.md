Visual Studio로 trackA_live_vs.sln을 열고 솔루션 탐색기 - trackA_live_vs우클릭 - 디버깅-명령 인수에 아래와 같이 입력


`--strategy LiveFundingMomentum` 부분을 `GoldenCross` 등으로 바꿔서 전략을 변경할 수 있음


`interval 1m` 부분을 `5m`, `15m`, ... 등으로 바꿔서 전략의 타임 프레임을 변경할 수 있음


`--mode realtime --strategy LiveFundingMomentum --symbol BTCUSDT --interval 1m --poll-ms 1000 --market-url https://demo-fapi.binance.com --order-url https://demo-fapi.binance.com --send-test-order --advance-state-on-accept --api-key YOUR_TESTNET_API_KEY --api-secret YOUR_TESTNET_API_SECRET`


`YOUR_TESTNET_API_KEY`, `YOUR_TESTNET_API_KEY_SECRET` 부분에 자신의 Binance futures testnet API key를 입력


전략 추가 방법: `strategies/live_futures_strategies.hpp`에서 맨 마지막에 다음과 같은 코드를 입력:

```
class Strategy_name : public Strategy {//Strategy_name에 전략 이름 대입
public:
    int n = 3; // 예를 들어 3봉 전으로 설정
    std::string symbol = "BTCUSDT"; // 예를 들어 BTCUSDT
    double quantity = 0.01; // 수량, 예를 들어 0.01 BTC
    std::string name() const override { return "Strategy_name"; }

    void on_start() override {
        open_m_ = BarMatrix(Field::Open, 500);
        high_m_ = BarMatrix(Field::High, 500);
        low_m_ = BarMatrix(Field::Low, 500);
        close_m_ = BarMatrix(Field::Close, 500);
        vol_m_ = BarMatrix(Field::Volume, 500);
        position_ = 0;
        // 시작 시 초기화
    }

    std::vector<Order> on_bar(int64_t ts, const MarketSnapshot& snap) override {
        open_m_.update(snap);
        high_m_.update(snap);
        low_m_.update(snap);
        close_m_.update(snap);
        vol_m_.update(snap);       // 매 바마다 ohlcv 호출
        if (!close_m_.ready(n + 1) || !vol_m_.ready(n + 1)) {
            return {};
        }
        Column cur_open = get_column(open_m_, 0);   // 현재 봉 시가
        Column cur_high = get_column(high_m_, 0);   // 현재 봉 고가
        Column cur_low = get_column(low_m_, 0);    // 현재 봉 저가
        Column cur_close = get_column(close_m_, 0);  // 현재 봉 종가
        Column cur_volume = get_column(vol_m_, 0);    // 현재 봉 거래량

        Column past_open = get_column(open_m_, n);   // n봉 전 시가
        Column past_high = get_column(high_m_, n);   // n봉 전 고가
        Column past_low = get_column(low_m_, n);    // n봉 전 저가
        Column past_close = get_column(close_m_, n);  // n봉 전 종가
        Column past_volume = get_column(vol_m_, n);    // n봉 전 거래량 지금 예시에선 n=3

        double btc_now_close = cur_close[symbol]; // symbol = BTCUSDT의 현재 종가
        double btc_past_close = past_close[symbol];
        double btc_now_vol = cur_volume[symbol];
        double btc_past_vol = past_volume[symbol];
        
        std::vector<Order> orders;
        if (position_ == 0) {//포지션이 없을 때
            if (btc_now_close >= btc_past_close) {//현재 종가가 n=3 봉 전 종가보다 크면
                orders.push_back(Order::long_future(quantity, symbol)); // 롱 진입
            }
            else {
                orders.push_back(Order::short_future(quantity, symbol)); // 숏 진입
            }
        }
        else if (position_ == 1) {//롱 포지션 보유 중일 때
            if (btc_now_close >= btc_past_close) {
                orders.push_back(Order::long_future(quantity, symbol)); // 롱 진입
            }
            else {
                orders.push_back(Order::short_future(quantity, symbol)); // 숏 진입
            }
        }
        else if (position_ == -1) {//숏 포지션 보유 중일 때
            if (btc_now_close >= btc_past_close) {
                orders.push_back(Order::long_future(quantity, symbol)); // 롱 진입
            }
            else {
                orders.push_back(Order::short_future(quantity, symbol)); // 숏 진입
            }
        }

        
        
        
        return orders;
    }

    void on_fill(const std::vector<Order>& filled) override {
        // 실제 체결된 뒤 상태 갱신
        for (const auto& o : filled) {
            if (o.instrument != Instrument::FUTURE || o.symbol != symbol) continue;

            if (o.side == Side::LONG) {
                if (position_ == -1) position_ = 0;
                else                 position_ = 1;
            }
            else {
                if (position_ == 1)  position_ = 0;
                else                 position_ = -1;
            }
        }
    }

private:
    BarMatrix open_m_{ Field::Open,   500 };
    BarMatrix high_m_{ Field::High,   500 };
    BarMatrix low_m_{ Field::Low,    500 };
    BarMatrix close_m_{ Field::Close,  500 };
    BarMatrix vol_m_{ Field::Volume, 500 };
    int position_ = 0; // 1=롱, -1=숏, 0=없음    // 전략 내부 상태 변수
};
```


이후 `main_vs_dry_run.cpp`에서


```
    int fast = 5;
    int slow = 20;
    double notional = 100.0;
```
(지금 예시에서) 부분 밑에
```
int n = 3;
```
입력


```
        } else if (strategy_name == "LiveGoldenCross") {
            auto s = std::make_unique<LiveGoldenCrossStrategy>();
            s->fast_period = fast;
            s->slow_period = slow;
            s->notional_per_trade = notional;
            strategy = std::move(s);
            std::cout << "G.C.";
        }
```
부분 밑에
```
        else if (strategy_name == "Strategy_name") {
            auto s = std::make_unique<Strategy_name>();
            s->symbol = symbol;
            s->quantity = quantity;
            s->n = n;
            strategy = std::move(s);
        }
```
입력


`main_live.cpp`에서도
```
    // GoldenCross defaults
    int fast = 5;
    int slow = 20;
    double notional = 100.0;
```
밑에
```
int n=3;
```
입력
```
    } else if (strategy_name == "LiveGoldenCross") {
        auto s = std::make_unique<LiveGoldenCrossStrategy>();
        s->fast_period = fast;
        s->slow_period = slow;
        s->notional_per_trade = notional;
        strategy = std::move(s);
    }
```
밑에
```
        else if (strategy_name == "Strategy_name") {
            auto s = std::make_unique<Strategy_name>();
            s->symbol = symbol;
            s->quantity = quantity;
            s->n = n;
            strategy = std::move(s);
        }
```
입력

