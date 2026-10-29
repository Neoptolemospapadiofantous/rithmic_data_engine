// mm_quote_rate — how many exchange transactions the frozen market-making setting would send per minute.
// Question (2026-10-25, before building the live order path): a Lighter Standard account allows 60 sendTx/sendTxBatch
// requests per rolling minute (apidocs.lighter.xyz/docs/rate-limits); a batch carries up to 15 transactions. Does the
// strategy fit, how many markets can one account run, and does batching per tick make it fit?
//
// Replays an archive with the same per-coin settings mm_gates uses (--params, e.g. data/mm_gates/params_lighter.json)
// and counts, per coin, every new quote (a create) and every cancel the strategy issues (a live order that is pulled or
// moved, i.e. every create beyond the first that did not end in a fill). Reports per-coin creates+cancels per minute,
// the busiest-minute total across coins, and the same with all actions in one book update batched together.
//   g++ -O2 -std=c++20 -Isrc research/programs/mm_quote_rate.cpp -o /tmp/mm_quote_rate -lzstd -lz -pthread
//   mm_quote_rate DIR PARAMS.json [ORDER_USD]
#include "dex/hl_events.hpp"
#include "dex/hl_mm.hpp"

#include <cstdio>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: mm_quote_rate DIR PARAMS.json [ORDER_USD]\n"); return 2; }
    std::ifstream in(argv[2]);
    const json P = json::parse(in);
    const double order_usd = argc > 3 ? std::atof(argv[3]) : P.value("order_usd", 100.0);
    std::map<int64_t, int64_t> per_min_tx, per_min_req;      // minute → transactions / requests (batched per update)
    std::map<std::string, std::pair<int64_t, double>> per_coin;   // coin → (tx, minutes)
    int64_t total_tx = 0;
    for (const auto& d : hl::list_days(argv[1])) {
        if (!P["coins"].contains(d.coin)) continue;
        const auto& cj = P["coins"][d.coin];
        hl::Params p;
        p.order_usd = order_usd; p.max_inv_units = P.value("max_inv", 3);
        p.maker_fee_bps = P.value("maker_bps", 0.0); p.taker_fee_bps = P.value("taker_bps", 0.0);
        p.latency_ms = P.value("latency_ms", int64_t{300}); p.track_markouts = false;
        p.half_width_ticks = cj.value("half_width_ticks", 5.0); p.min_spread_ticks = cj.value("min_spread_ticks", 4.0);
        p.imb_pull = cj.value("imb_pull", 0.6); p.pause_move_ticks = cj.value("pause_move_ticks", 2.0);
        p.pause_ms = cj.value("pause_ms", int64_t{2000}); p.requote_ticks = cj.value("requote_ticks", 1.0);
        p.venue_tick = cj.value("venue_tick", 0.0);
        p.strat_tick_rule = cj.value("tick_rule", "book") == "hyperliquid" ? hl::TickRule::Hyperliquid : hl::TickRule::Book;
        p.hl_sz_decimals = cj.value("hl_sz_decimals", 0);
        hl::Simulator sim(p);
        const auto ev = hl::load_day(d);
        int64_t first = 0, last = 0, tx = 0;
        double pb = 0, pa = 0;                                // quote prices after the previous event
        for (const auto& e : ev) {
            if (e.is_book && !e.book.valid()) continue;
            if (e.is_book) sim.on_book(e.book); else sim.on_trade(e.trade);
            if (!first) first = e.t_ms;
            last = e.t_ms;
            int n = 0;
            const double b = sim.quote_px(true), a = sim.quote_px(false);
            // a side changing price = cancel + create (2), appearing = create (1), disappearing = cancel or fill (1)
            for (auto [now, before] : {std::pair{b, pb}, std::pair{a, pa}}) {
                if (now == before) continue;
                n += (now > 0 && before > 0) ? 2 : 1;
            }
            pb = b; pa = a;
            if (n) { per_min_tx[e.t_ms / 60'000] += n; per_min_req[e.t_ms / 60'000] += 1; tx += n; }
        }
        // disappearances that were fills are not transactions: subtract the fills
        tx -= sim.running().fills;
        total_tx += tx;
        auto& pc = per_coin[d.coin]; pc.first += tx; pc.second += (last - first) / 60'000.0;
    }
    std::printf("coin  tx/min (creates+cancels)\n");
    double sum_rate = 0;
    for (auto& [c, v] : per_coin) { const double r = v.second > 0 ? v.first / v.second : 0; sum_rate += r; std::printf("%-6s %6.2f\n", c.c_str(), r); }
    auto pct = [](std::map<int64_t, int64_t>& m, double q) {
        std::vector<int64_t> v; for (auto& [k, x] : m) v.push_back(x); if (v.empty()) return int64_t{0};
        std::sort(v.begin(), v.end()); return v[std::min(v.size() - 1, (size_t)(q * v.size()))];
    };
    std::printf("\nall %zu coins: mean %.1f tx/min (fills subtracted); per-minute totals (fills included, upper bound): median %lld, p95 %lld, max %lld\n",
                per_coin.size(), sum_rate, (long long)pct(per_min_tx, 0.5), (long long)pct(per_min_tx, 0.95), (long long)pct(per_min_tx, 1.0));
    std::printf("requests if every book update's actions go in ONE batch (per coin; a cross-coin batcher needs fewer): median %lld, p95 %lld, max %lld per minute\n",
                (long long)pct(per_min_req, 0.5), (long long)pct(per_min_req, 0.95), (long long)pct(per_min_req, 1.0));
    std::printf("Lighter Standard: 60 requests/min (a batch = 1 request, up to 15 tx).\n");
    return 0;
}
