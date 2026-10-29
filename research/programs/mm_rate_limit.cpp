// mm_rate_limit — the frozen market-making setting under Lighter's real execution rules (pre-registered
// research/preregistered/2026-10-25_mm_rate_limit.md). Lighter Standard: post-only placement 0 ms, cancel/modify 300 ms,
// 60 sendTx/sendTxBatch requests per rolling minute per account, ≤ 15 transactions per batch, 0 fees.
//
// Every coin of a day is replayed TOGETHER in exchange-time order, so one rate limiter is shared by all the markets an
// account trades. The limiter batches: a transaction decided while a request is still waiting for its slot joins that
// request (up to 15); otherwise it takes the next free slot of the rolling window. Network delay is added to both
// placements and cancels (this box: ~145 ms one way, measured on the Lighter feed; a server beside Lighter: ~5 ms).
//   g++ -O2 -std=c++20 -Isrc research/programs/mm_rate_limit.cpp -o /tmp/mm_rate_limit -lzstd -lz -pthread
//   mm_rate_limit DIR [PARAMS.json] [--split YYYY-MM-DD] [--threads N]
// Without PARAMS the book tick rule is used (Tardis history = Hyperliquid books, the unit the backtests used).
#include "dex/hl_events.hpp"
#include "dex/hl_mm.hpp"

#include <atomic>
#include <cstdio>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>

using json = nlohmann::json;

struct Gate {                       // one account's rolling-window, batching request limiter
    int limit = 60, batch = 15; int64_t window = 60'000;
    std::deque<int64_t> sent;       // request send times, ascending
    int64_t open_t = -1; int open_n = 0;
    double wait_sum = 0; int64_t tx = 0, requests = 0;
    int64_t operator()(int64_t now) {
        ++tx;
        if (open_t >= now && open_n < batch) { ++open_n; wait_sum += open_t - now; return open_t; }
        while (!sent.empty() && sent.front() <= now - window) sent.pop_front();
        int64_t t = now;
        if ((int)sent.size() >= limit) t = std::max(now, sent[sent.size() - limit] + window);
        sent.push_back(t); ++requests;
        open_t = t; open_n = 1; wait_sum += t - now;
        return t;
    }
};

// Compact event: a day of 30 coins does not fit in memory as hl::Event (two heap vectors + two strings each).
// Books keep their top LV levels (Tardis history has 5; mm_robust's check: 20 → 5 levels moves results ≤ 0.01 bps).
#ifndef LVN
#define LVN 5
#endif
constexpr int LV = LVN;
struct CE { int64_t t; double tpx, tsz; hl::Level b[LV], a[LV]; uint8_t nb, na; bool is_book, buy; };
static std::vector<CE> compact(const std::vector<hl::Event>& ev) {
    std::vector<CE> out; out.reserve(ev.size());
    for (const auto& e : ev) {
        CE c{}; c.t = e.t_ms; c.is_book = e.is_book;
        if (e.is_book) {
            if (!e.book.valid()) continue;
            c.nb = (uint8_t)std::min<size_t>(LV, e.book.bids.size()); c.na = (uint8_t)std::min<size_t>(LV, e.book.asks.size());
            for (int i = 0; i < c.nb; ++i) c.b[i] = e.book.bids[i];
            for (int i = 0; i < c.na; ++i) c.a[i] = e.book.asks[i];
        } else { c.tpx = e.trade.px; c.tsz = e.trade.sz; c.buy = e.trade.buy_aggr; }
        out.push_back(c);
    }
    return out;
}

struct Scenario { std::string name; int64_t place, cancel; int accounts; };   // accounts 0 = no limit

struct Acc { double pnl = 0, vol = 0, ho_pnl = 0, ho_vol = 0; int64_t fills = 0, tx = 0, req = 0; double wait = 0; std::map<std::string, double> day; };

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: mm_rate_limit DIR [PARAMS.json] [--split YYYY-MM-DD] [--threads N]\n"); return 2; }
    std::string dir = argv[1], split = "2025-11-01"; json P; int threads = 4;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--split" && i + 1 < argc) split = argv[++i];
        else if (a == "--threads" && i + 1 < argc) threads = std::max(1, std::atoi(argv[++i]));
        else { std::ifstream in(a); P = json::parse(in); }
    }
    const std::vector<Scenario> S = {
        {"backtest assumption (300 ms both, no limit)", 300, 300, 0},
        {"this box: place 145 / cancel 445, no limit", 145, 445, 0},
        {"this box + 60 req/min, 1 account", 145, 445, 1},
        {"this box + 60 req/min, 5 accounts", 145, 445, 5},
        {"near server: place 5 / cancel 305, no limit", 5, 305, 0},
        {"near server + 60 req/min, 1 account", 5, 305, 1},
        {"near server + 60 req/min, 5 accounts", 5, 305, 5},
    };
    // group the coin-days by day
    std::map<std::string, std::vector<hl::DayFile>> days;
    const std::string only = getenv("COINS") ? getenv("COINS") : "";   // optional comma list (checks)
    for (auto& d : hl::list_days(dir)) if ((P.is_null() || P["coins"].contains(d.coin)) && (only.empty() || ("," + only + ",").find("," + d.coin + ",") != std::string::npos)) days[d.day].push_back(d);
    std::vector<std::string> keys; for (auto& [k, _] : days) keys.push_back(k);
    std::vector<Acc> acc(S.size());
    std::mutex mu; std::atomic<size_t> next{0};
    auto params_for = [&](const std::string& coin, const Scenario& sc) {
        hl::Params p;
        p.order_usd = 100; p.max_inv_units = 3; p.maker_fee_bps = 0; p.taker_fee_bps = 0; p.track_markouts = false;
        p.cancels = hl::CancelModel::Pessimistic;
        p.half_width_ticks = 5; p.min_spread_ticks = 4; p.imb_pull = 0.6; p.pause_move_ticks = 2; p.pause_ms = 2000; p.requote_ticks = 1;
        p.latency_ms = 300; p.place_latency_ms = sc.place; p.cancel_latency_ms = sc.cancel;
        if (!P.is_null()) {
            const auto& cj = P["coins"][coin];
            p.venue_tick = cj.value("venue_tick", 0.0);
            p.strat_tick_rule = cj.value("tick_rule", "book") == "hyperliquid" ? hl::TickRule::Hyperliquid : hl::TickRule::Book;
            p.hl_sz_decimals = cj.value("hl_sz_decimals", 0);
        }
        return p;
    };
    auto worker = [&]() {
        for (size_t k; (k = next++) < keys.size();) {
            const auto& files = days[keys[k]];
            std::vector<std::vector<CE>> ev; ev.reserve(files.size());
            for (const auto& f : files) ev.push_back(compact(hl::load_day(f)));
            struct Ix { int64_t t; int c; size_t i; };
            std::vector<Ix> order;
            for (size_t c = 0; c < ev.size(); ++c) for (size_t i = 0; i < ev[c].size(); ++i) order.push_back({ev[c][i].t, (int)c, i});
            std::stable_sort(order.begin(), order.end(), [](const Ix& a, const Ix& b) { return a.t < b.t; });
            for (size_t s = 0; s < S.size(); ++s) {
                std::vector<Gate> gates(std::max(1, S[s].accounts));
                std::vector<hl::Simulator> sims;
                sims.reserve(ev.size());
                for (size_t c = 0; c < ev.size(); ++c) {
                    sims.emplace_back(params_for(files[c].coin, S[s]));
                    if (S[s].accounts > 0) { Gate* g = &gates[c % gates.size()]; sims.back().set_send_gate([g](int64_t now) { return (*g)(now); }); }
                }
                hl::Book bk; hl::Trade tr;
                for (const auto& x : order) {
                    const auto& e = ev[x.c][x.i];
                    if (e.is_book) {
                        bk.t_ms = e.t; bk.bids.assign(e.b, e.b + e.nb); bk.asks.assign(e.a, e.a + e.na);
                        sims[x.c].on_book(bk);
                    } else { tr.t_ms = e.t; tr.px = e.tpx; tr.sz = e.tsz; tr.buy_aggr = e.buy; sims[x.c].on_trade(tr); }
                }
                Acc a;
                for (auto& sim : sims) { const auto r = sim.finish(); a.pnl += r.flat_pnl; a.vol += r.volume_usd; a.fills += r.fills; }
                for (auto& g : gates) { a.tx += g.tx; a.req += g.requests; a.wait += g.wait_sum; }
                std::lock_guard lk(mu);
                auto& A = acc[s];
                A.pnl += a.pnl; A.vol += a.vol; A.fills += a.fills; A.tx += a.tx; A.req += a.req; A.wait += a.wait; A.day[keys[k]] += a.pnl;
                if (keys[k] >= split) { A.ho_pnl += a.pnl; A.ho_vol += a.vol; }
            }
            std::lock_guard lk(mu);
            std::fprintf(stderr, "day %s done (%zu coins)\n", keys[k].c_str(), files.size());
        }
    };
    std::vector<std::thread> th; for (int i = 0; i < threads; ++i) th.emplace_back(worker); for (auto& t : th) t.join();
    std::printf("# mm_rate_limit — %s, %zu days, holdout from %s\n\n", dir.c_str(), keys.size(), split.c_str());
    std::printf("| scenario | P&L $ | net bps | holdout P&L $ | holdout bps | days up | fills | tx/request | mean wait ms |\n|---|---|---|---|---|---|---|---|---|\n");
    for (size_t s = 0; s < S.size(); ++s) {
        const auto& A = acc[s];
        int up = 0; for (auto& [d, p] : A.day) up += p > 0;
        std::printf("| %s | %.0f | %.2f | %.0f | %.2f | %d/%zu | %lld | %s | %s |\n", S[s].name.c_str(), A.pnl, A.vol > 0 ? A.pnl / A.vol * 1e4 : 0,
                    A.ho_pnl, A.ho_vol > 0 ? A.ho_pnl / A.ho_vol * 1e4 : 0, up, A.day.size(), (long long)A.fills,
                    A.req ? std::to_string(A.tx / (double)A.req).substr(0, 4).c_str() : "—", A.tx ? std::to_string(A.wait / A.tx).substr(0, 6).c_str() : "—");
    }
    return 0;
}
