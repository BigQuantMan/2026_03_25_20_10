#include "../include/unified_feed.hpp"
#include <stdexcept>
#include <algorithm>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// ─────────────────────────────────────────────────────────────────
//  빠른 CSV 파싱 헬퍼
//  FILE* + 8MB 버퍼 + in-place pointer split
//  → stringstream 없이 O(1) 필드 접근
// ─────────────────────────────────────────────────────────────────

static const int IO_BUF_SIZE = 8 * 1024 * 1024;  // 8MB

// 줄을 in-place 로 쪼갬: ',' → '\0' , fields[] 에 포인터 저장
// 반환값: 필드 수
static inline int csv_split(char* line, const char** fields, int max_f) {
    int n = 0;
    fields[n++] = line;
    for (char* p = line; *p && n < max_f; ++p) {
        if (*p == ',') { *p = '\0'; fields[n++] = p + 1; }
        else if (*p == '\n' || *p == '\r') { *p = '\0'; break; }
    }
    return n;
}

static inline double fast_dbl(const char* s) {
    if (!s || !*s || s[0] == 'n' || s[0] == 'N') return 0.0;
    return atof(s);
}


// ─────────────────────────────────────────────────────────────────
//  CSV 마지막 행의 타임스탬프를 빠르게 읽어 반환 (0 = 실패)
//  파일 끝에서 512 바이트를 읽어 마지막 완성된 줄을 파싱
// ─────────────────────────────────────────────────────────────────
static int64_t csv_last_ts(FILE* f) {
    if (fseek(f, 0, SEEK_END) != 0) return 0;
    long file_size = ftell(f);
    if (file_size <= 0) return 0;

    long peek = (file_size < 512) ? file_size : 512;
    if (fseek(f, -peek, SEEK_END) != 0) return 0;

    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';

    // 마지막 완성된 줄 찾기 (끝에서부터 두 번째 \n 이후)
    char* end = buf + n;
    // 끝의 개행 제거
    while (end > buf && (end[-1] == '\n' || end[-1] == '\r')) --end;
    // 그 앞의 줄 시작 찾기
    char* nl = end - 1;
    while (nl > buf && *nl != '\n') --nl;
    char* last_line = (*nl == '\n') ? nl + 1 : buf;

    int64_t ts = (int64_t)atoll(last_line);
    // 원래 위치로 복귀
    fseek(f, 0, SEEK_SET);
    return ts;
}


// ─────────────────────────────────────────────────────────────────
//  옵션 CSV 로더
// ─────────────────────────────────────────────────────────────────
static void load_options_csv(
    const std::string& path, int64_t start_ts, int64_t end_ts,
    std::unordered_map<int64_t, std::vector<OptionBar>>& out)
{
    FILE* f = fopen(path.c_str(), "r");
    if (!f) throw std::runtime_error("UnifiedFeed: cannot open options CSV: " + path);

    // ── 범위 체크: CSV 마지막 ts < start_ts → 데이터 없음, 즉시 반환 ──
    if (start_ts > 0) {
        int64_t last_ts = csv_last_ts(f);
        if (last_ts > 0 && last_ts < start_ts) {
            std::cerr << "[UnifiedFeed] options: start_ts is past CSV end, skipping\n";
            fclose(f);
            return;
        }
    }

    std::vector<char> iobuf(IO_BUF_SIZE);
    setvbuf(f, iobuf.data(), _IOFBF, iobuf.size());

    char line[1024];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return; }  // header

    size_t rows = 0;
    const char* fields[20];

    while (fgets(line, sizeof(line), f)) {
        // 타임스탬프만 먼저 체크 (파싱 최소화)
        int64_t ts = (int64_t)atoll(line);
        if (ts < start_ts) continue;
        if (ts > end_ts)   break;

        int n = csv_split(line, fields, 20);
        if (n < 15) continue;

        OptionBar bar;
        bar.timestamp     = ts;
        bar.symbol        = fields[1];
        bar.strike        = atoi(fields[2]);
        bar.expiry        = fields[3];
        bar.type          = fields[4][0];
        bar.mark_price    = fast_dbl(fields[5]);
        bar.mark_iv       = fast_dbl(fields[6]);
        bar.index_price   = fast_dbl(fields[7]);
        bar.delta         = fast_dbl(fields[8]);
        bar.gamma         = fast_dbl(fields[9]);
        bar.theta         = fast_dbl(fields[10]);
        bar.vega          = fast_dbl(fields[11]);
        bar.best_bid      = fast_dbl(fields[12]);
        bar.best_ask      = fast_dbl(fields[13]);
        bar.open_interest = fast_dbl(fields[14]);

        if (bar.mark_price <= 0.0) continue;
        out[ts].push_back(std::move(bar));
        ++rows;
    }
    fclose(f);

    std::cerr << "[UnifiedFeed] options: " << rows << " rows → "
              << out.size() << " timestamps\n";
}


// ─────────────────────────────────────────────────────────────────
//  선물 CSV 로더
// ─────────────────────────────────────────────────────────────────
static void load_futures_csv(
    const std::string& path, int64_t start_ts, int64_t end_ts,
    std::unordered_map<int64_t,
        std::unordered_map<std::string, FuturesBar>>& out)
{
    FILE* f = fopen(path.c_str(), "r");
    if (!f) throw std::runtime_error("UnifiedFeed: cannot open futures CSV: " + path);

    // ── 범위 체크: CSV 마지막 ts < start_ts → 데이터 없음, 즉시 반환 ──
    if (start_ts > 0) {
        int64_t last_ts = csv_last_ts(f);
        if (last_ts > 0 && last_ts < start_ts) {
            std::cerr << "[UnifiedFeed] futures: start_ts is past CSV end ("
                      << last_ts << " < " << start_ts << "), skipping\n";
            fclose(f);
            return;
        }
    }

    std::vector<char> iobuf(IO_BUF_SIZE);
    setvbuf(f, iobuf.data(), _IOFBF, iobuf.size());

    char line[512];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return; }  // header

    size_t rows = 0;
    size_t skipped = 0;
    const char* fields[16];

    std::cerr << "[UnifiedFeed] Reading futures CSV...\n";

    while (fgets(line, sizeof(line), f)) {
        int64_t ts = (int64_t)atoll(line);
        if (ts < start_ts) { ++skipped; continue; }
        if (ts > end_ts)   break;

        int n = csv_split(line, fields, 16);
        if (n < 13) continue;

        double cl = fast_dbl(fields[5]);
        if (cl <= 0.0) continue;

        FuturesBar bar;
        bar.timestamp       = ts;
        bar.symbol          = fields[1];
        bar.open            = fast_dbl(fields[2]);
        bar.high            = fast_dbl(fields[3]);
        bar.low             = fast_dbl(fields[4]);
        bar.close           = cl;
        bar.volume          = fast_dbl(fields[6]);
        bar.quote_volume    = fast_dbl(fields[7]);
        bar.funding_rate    = fast_dbl(fields[8]);
        bar.open_interest   = fast_dbl(fields[9]);
        bar.long_ratio      = fast_dbl(fields[10]);
        bar.short_ratio     = fast_dbl(fields[11]);
        bar.taker_buy_ratio = fast_dbl(fields[12]);
        bar.is_active       = (n < 14 || fields[13][0] != '0');

        out[ts][bar.symbol] = std::move(bar);
        ++rows;

        // 2M 행마다 진행 상황 출력 (skipped 포함 전체 행 기준)
        size_t total = rows + skipped;
        if (total % 2000000 == 0) {
            std::cerr << "[UnifiedFeed] ... " << (total / 1000000) << "M rows scanned"
                      << " (" << rows << " in range)\n";
        }
    }
    fclose(f);

    std::cerr << "[UnifiedFeed] futures: " << rows << " rows → "
              << out.size() << " timestamps\n";
}


// ─────────────────────────────────────────────────────────────────
//  UnifiedFeed 구현
// ─────────────────────────────────────────────────────────────────

UnifiedFeed::UnifiedFeed(const std::string& options_csv,
                         const std::string& futures_csv,
                         const std::string& spot_symbol,
                         int64_t            start_ts_ms,
                         int64_t            end_ts_ms)
    : spot_symbol_(spot_symbol)
    , start_ts_filter_(start_ts_ms)
    , end_ts_filter_(end_ts_ms)
{
    build(options_csv, futures_csv);
}

void UnifiedFeed::build(const std::string& options_csv,
                        const std::string& futures_csv)
{
    // ── CSV 로드 (필터 범위 내 데이터만) ─────────────────────────
    if (!options_csv.empty()) {
        load_options_csv(options_csv, start_ts_filter_, end_ts_filter_, opts_map_);
        has_options_ = !opts_map_.empty();
    }
    if (!futures_csv.empty()) {
        load_futures_csv(futures_csv, start_ts_filter_, end_ts_filter_, futs_map_);
        has_futures_ = !futs_map_.empty();
    }

    if (!has_options_ && !has_futures_) {
        std::cerr << "[UnifiedFeed] ERROR: no data found in the requested date range "
                  << start_ts_filter_ << " ~ " << end_ts_filter_
                  << " (check that your CSV covers this period)\n";
        throw std::runtime_error("UnifiedFeed: no data in selected date range");
    }

    // ── 타임스탬프 합집합 정렬 ────────────────────────────────────
    all_ts_.reserve(opts_map_.size() + futs_map_.size());
    for (const auto& [ts, _] : opts_map_) all_ts_.push_back(ts);
    for (const auto& [ts, _] : futs_map_) {
        if (opts_map_.find(ts) == opts_map_.end())
            all_ts_.push_back(ts);
    }
    std::sort(all_ts_.begin(), all_ts_.end());
    all_ts_.erase(std::unique(all_ts_.begin(), all_ts_.end()), all_ts_.end());

    std::cerr << "[UnifiedFeed] " << all_ts_.size() << " timestamps loaded"
              << " [opts=" << (has_options_ ? "yes" : "no")
              << " futs=" << (has_futures_ ? "yes" : "no") << "]\n";
    // 스냅샷은 next() 호출 시 lazy 빌드 — 여기서는 pre-build 하지 않음
}

// ─────────────────────────────────────────────────────────────────
//  next() — lazy snapshot 빌드 + std::move semantics
//  핵심: futs_map_에서 move 로 꺼내므로 unordered_map 복사 없음
// ─────────────────────────────────────────────────────────────────
bool UnifiedFeed::next() {
    if (cursor_ == SIZE_MAX) cursor_ = 0;
    else ++cursor_;
    if (cursor_ >= all_ts_.size()) return false;

    int64_t ts = all_ts_[cursor_];

    // ── 옵션 forward-fill ──────────────────────────────────────
    auto oit = opts_map_.find(ts);
    if (oit != opts_map_.end()) {
        last_opts_ = std::move(oit->second);
        opts_map_.erase(oit);
    }

    // ── 선물: map에서 move (복사 없음), 없으면 이전 스냅샷에서 forward-fill ──
    std::unordered_map<std::string, FuturesBar> next_futs;
    auto fit = futs_map_.find(ts);
    if (fit != futs_map_.end()) {
        next_futs = std::move(fit->second);   // ← 핵심: copy 없음
        futs_map_.erase(fit);
    } else {
        // forward-fill: 이전 스냅샷의 선물 데이터 복사 (희귀 경로)
        next_futs = current_snap_.all_futures();
    }

    // ── spot price 결정 ────────────────────────────────────────
    double spot = 0.0;
    auto sit = next_futs.find(spot_symbol_);
    if (sit != next_futs.end()) spot = sit->second.close;
    if (spot <= 0.0) {
        for (const auto& o : last_opts_)
            if (o.index_price > 0) { spot = o.index_price; break; }
    }

    // ── 스냅샷 빌드 (move로 소유권 이전, 복사 없음) ───────────────
    current_snap_ = MarketSnapshot(ts, spot, last_opts_, std::move(next_futs));
    return true;
}

const MarketSnapshot& UnifiedFeed::current() const {
    if (cursor_ == SIZE_MAX || cursor_ >= all_ts_.size())
        throw std::runtime_error("UnifiedFeed: current() called before next() or after end");
    return current_snap_;
}

size_t UnifiedFeed::remaining() const {
    if (cursor_ == SIZE_MAX) return all_ts_.size();
    if (cursor_ >= all_ts_.size()) return 0;
    return all_ts_.size() - cursor_ - 1;
}

int64_t UnifiedFeed::start_ts() const {
    return all_ts_.empty() ? 0 : all_ts_.front();
}

int64_t UnifiedFeed::end_ts() const {
    return all_ts_.empty() ? 0 : all_ts_.back();
}

void UnifiedFeed::reset() {
    cursor_ = SIZE_MAX;
}
