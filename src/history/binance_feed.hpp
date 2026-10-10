#pragma once
/*  ═══════════════════════════════════════════════════════════════════════════
    binance_feed.hpp — Binance USDⓈ-M futures aggTrades files → replay ticks

    Files from data.binance.vision (futures/um/{monthly,daily}/aggTrades), as
    downloaded by scripts/binance_history_download.sh into data/history/binance/<SYM>/.
    Accepted: .zip (read with `unzip -p`), .csv.zst (`zstd -dc`), plain .csv.
    CSV: agg_trade_id,price,quantity,first_trade_id,last_trade_id,transact_time,is_buyer_maker
    (newer files carry that header row; older ones do not).

    Row semantics are exactly scripts/crypto_backfill.sh's (the Postgres `ticks` rows the
    crypto collector and backfill write), so a file replay equals a Postgres replay:
      ts      transact_time (ms; µs if the value is ≥ 1e14 — Binance's 2025 switch on spot)
      size    llround(quantity × 1000)       — contracts of 0.001 coin
      is_buy  NOT is_buyer_maker             — the aggressor bought
      order   file order = agg_trade_id order (the DB orders by ts_event, seq = agg id % 2e9)
    History has no quotes (Binance stopped publishing futures bookTicker files in 2024), so
    book-gated variants see no book here, as in the Postgres backfill.
    ═══════════════════════════════════════════════════════════════════════════ */
#include "history/mbo_feed.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace hist {

// Parses one aggTrades CSV line. Returns false for a header / malformed line.
inline bool parse_agg_line(const char* p, Tick& out, uint64_t* agg_id = nullptr) {
    if (*p < '0' || *p > '9') return false;
    char* e;
    const unsigned long long id = std::strtoull(p, &e, 10); if (*e != ',') return false; p = e + 1;
    const double price = std::strtod(p, &e);               if (*e != ',') return false; p = e + 1;
    const double qty   = std::strtod(p, &e);               if (*e != ',') return false; p = e + 1;
    std::strtoull(p, &e, 10);                              if (*e != ',') return false; p = e + 1;   // first_trade_id
    std::strtoull(p, &e, 10);                              if (*e != ',') return false; p = e + 1;   // last_trade_id
    const long long t  = std::strtoll(p, &e, 10);          if (*e != ',') return false; p = e + 1;
    bool maker;
    if (std::strncmp(p, "true", 4) == 0 || std::strncmp(p, "True", 4) == 0) maker = true;
    else if (std::strncmp(p, "false", 5) == 0 || std::strncmp(p, "False", 5) == 0) maker = false;
    else return false;
    out.ts_us  = t >= 100'000'000'000'000LL ? t : t * 1000LL;
    out.price  = price;
    out.size   = std::llround(qty * 1000.0);
    out.is_buy = !maker;
    if (agg_id) *agg_id = id;
    return true;
}

class AggTradesFeed : public TickFeed {
public:
    AggTradesFeed(std::vector<std::string> files, int64_t from_us, int64_t to_us)
        : files_(std::move(files)), from_us_(from_us), to_us_(to_us) {}
    ~AggTradesFeed() override { close_cur(); }
    AggTradesFeed(const AggTradesFeed&) = delete;
    AggTradesFeed& operator=(const AggTradesFeed&) = delete;

    bool next_batch(std::vector<Tick>& ticks, std::deque<Quote>&, size_t max_ticks) override {
        const size_t t0 = ticks.size();
        Tick t{};
        while (!done_ && ticks.size() - t0 < max_ticks) {
            if (!f_ && !open_next()) { done_ = true; break; }
            if (!std::fgets(line_, sizeof line_, f_)) { close_cur(); continue; }
            ++lines_;
            if (!parse_agg_line(line_, t)) { ++skipped_; continue; }
            if (t.ts_us <= from_us_) continue;
            if (t.ts_us > to_us_) { done_ = true; break; }
            if (t.ts_us < clk_) { t.ts_us = clk_; ++clamped_; }
            clk_ = t.ts_us;
            ++trades_; volume_ += t.size;
            ticks.push_back(t);
        }
        if (done_) close_cur();
        return ticks.size() > t0;
    }
    std::string summary() const override {
        char b[300];
        std::snprintf(b, sizeof b, "aggTrades feed: %llu lines, %llu trades (%llu × 0.001 coin), %llu header/malformed lines, "
                      "%llu clamped timestamps, %zu file(s)", (unsigned long long)lines_, (unsigned long long)trades_,
                      (unsigned long long)volume_, (unsigned long long)skipped_, (unsigned long long)clamped_, files_.size());
        return b;
    }
    uint64_t trades() const { return trades_; }

private:
    static bool ends_with(const std::string& s, const char* suf) {
        const size_t n = std::strlen(suf); return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
    }
    bool open_next() {
        while (fi_ < files_.size()) {
            const std::string& p = files_[fi_++];
            if (p.find('\'') != std::string::npos) throw std::runtime_error("aggTrades: quote in path " + p);
            if (ends_with(p, ".zip"))      { f_ = popen(("unzip -p '" + p + "'").c_str(), "r"); piped_ = true; }
            else if (ends_with(p, ".zst")) { f_ = popen(("zstd -dc '" + p + "'").c_str(), "r"); piped_ = true; }
            else                           { f_ = std::fopen(p.c_str(), "r"); piped_ = false; }
            if (!f_) throw std::runtime_error("aggTrades: cannot open " + p);
            if (log_) log_("[AGG] reading " + p);
            return true;
        }
        return false;
    }
    void close_cur() {
        if (!f_) return;
        if (piped_) {
            const int rc = pclose(f_);
            // An early stop (past to_us) closes the pipe while unzip still writes → SIGPIPE status; fine.
            if (rc != 0 && !done_ && log_) log_("[AGG] WARN decompressor exit status " + std::to_string(rc));
        } else std::fclose(f_);
        f_ = nullptr;
    }

    std::vector<std::string> files_;
    size_t fi_ = 0;
    FILE* f_ = nullptr; bool piped_ = false;
    int64_t from_us_, to_us_, clk_ = 0;
    bool done_ = false;
    char line_[512];
    uint64_t lines_ = 0, trades_ = 0, volume_ = 0, skipped_ = 0, clamped_ = 0;
};

} // namespace hist
