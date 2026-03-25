CXX      := g++
CXXFLAGS := -std=c++17 -O3 -march=native -Wall
TARGET   := backtester
SRCDIR   := src
INCDIR   := include

SRCS := main.cpp \
        $(SRCDIR)/data_feed.cpp \
        $(SRCDIR)/futures_feed.cpp \
        $(SRCDIR)/unified_feed.cpp \
        $(SRCDIR)/portfolio.cpp \
        $(SRCDIR)/backtester.cpp \
        $(SRCDIR)/metrics/sharpe.cpp \
        $(SRCDIR)/metrics/total_return.cpp \
        $(SRCDIR)/metrics/daily_turnover.cpp

# 커스텀 메트릭 추가 시 여기에 경로 추가:
# SRCS += $(SRCDIR)/metrics/my_metric.cpp

OBJS := $(SRCS:.cpp=.o)

LIVE_TARGET := live_replay_trader
LIVE_SRCS := main_live.cpp         $(SRCDIR)/futures_feed.cpp         $(SRCDIR)/live/order_mapper.cpp         $(SRCDIR)/live/binance_futures_gateway.cpp         $(SRCDIR)/live/curl_http_client.cpp         $(SRCDIR)/live/hmac_sha256_signer.cpp         $(SRCDIR)/live/live_trader.cpp
LIVE_OBJS := $(LIVE_SRCS:.cpp=.o)

.PHONY: all live run prepare clean help

all: $(TARGET)
live: $(LIVE_TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^
	@echo "✅ Build complete: ./$(TARGET)"

$(LIVE_TARGET): $(LIVE_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ -lcurl -lssl -lcrypto
	@echo "✅ Live build complete: ./$(LIVE_TARGET)"

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -I$(INCDIR) -c $< -o $@

# ── 데이터 준비 (parquet → CSV) ───────────────────────────────────
prepare:
	@echo "📦 Preparing options data..."
	pip install pandas pyarrow --break-system-packages -q
	python3 tools/prepare_data.py
	@echo "✅ Options data prepared in results/prepared/"

prepare-futures:
	@echo "📦 Preparing futures data..."
	pip install pandas pyarrow --break-system-packages -q
	python3 tools/prepare_futures_data.py
	@echo "✅ Futures data prepared in results/prepared/"

# ── 전체 파이프라인 실행 ──────────────────────────────────────────
run: all prepare
	@echo "🚀 Running options backtest..."
	./$(TARGET) results/prepared/is_data.csv --capital 10000 --verbose
	@echo "📊 Visualizing results..."
	python3 tools/visualize.py

run-futures: all prepare-futures
	@echo "🚀 Running futures backtest..."
	./$(TARGET) --futures-csv results/prepared/futures_is_data.csv --capital 10000 --verbose

run-unified: all prepare prepare-futures
	@echo "🚀 Running unified (options + futures) backtest..."
	./$(TARGET) --options-csv results/prepared/is_data.csv \
	            --futures-csv results/prepared/futures_is_data.csv --capital 10000 --verbose

# ── 빌드 정리 ─────────────────────────────────────────────────────
clean:
	rm -f $(TARGET) $(OBJS) $(LIVE_TARGET) $(LIVE_OBJS) main.o main_live.o
	@echo "🧹 Cleaned"

# ── 도움말 ────────────────────────────────────────────────────────
help:
	@echo ""
	@echo "  make          → C++ 빌드"
	@echo "  make prepare  → parquet → CSV 변환"
	@echo "  make live     → live replay bridge 빌드"
	@echo "  make run      → 빌드 + 데이터 준비 + 백테스트 + 시각화"
	@echo "  make clean    → 빌드 결과물 정리 (backtester + live)"
	@echo ""
	@echo "  ./$(TARGET) <csv> --capital 10000 --verbose"
	@echo ""
