Visual Studio로 trackA_live_vs.sln을 열고 솔루션 탐색기 - trackA_live_vs우클릭 - 디버깅-명령 인수에 아래와 같이 입력


`interval 1m` 부분을 `5m`, `15m`, ... 등으로 바꿔서 전략의 타임 프레임을 변경할 수 있음


`--mode realtime --strategy LiveFundingMomentum --symbol BTCUSDT --interval 1m --poll-ms 1000 --market-url https://demo-fapi.binance.com --order-url https://demo-fapi.binance.com --send-test-order --advance-state-on-accept --api-key YOUR_TESTNET_API_KEY --api-secret YOUR_TESTNET_API_SECRET`


`YOUR_TESTNET_API_KEY`, `YOUR_TESTNET_API_KEY_SECRET` 부분에 자신의 Binance futures testnet API key를 입력
