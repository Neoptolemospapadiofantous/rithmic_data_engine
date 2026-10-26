// mm_sim — replays the hl_recorder archive through the queue-aware market-making simulator
// (src/dex/hl_mm.hpp). Research only: reads files, writes a report, touches nothing else.
//
//   mm_sim --dir DIR [--coins A,B] [--from YYYY-MM-DD] [--to YYYY-MM-DD] [--order-usd 100]
//          [--max-inv 3] [--sweep] [--report FILE] [--dash DIR] [--daily FILE]
//
// Every coin runs four configurations side by side:
//   hl/pess  Hyperliquid  maker 1.5 bps, taker 4.5, 200 ms, cancels from behind us (pessimistic queue)
//   hl/opt   Hyperliquid  same, cancels from the whole level (optimistic queue)
//   lt/pess  Lighter std  maker 0, taker 0, 300 ms (its standard-account delay), pessimistic queue
//   lt/opt   Lighter std  same, optimistic queue
// Lighter is simulated on Hyperliquid's book — a stand-in: the two books differ.
// --sweep adds Hyperliquid at 50 ms and 500 ms (both queue bounds): how much the result depends on speed.
// --max-levels N keeps only N book levels per side (5 = what the Tardis history holds): run the live
// archive both ways to measure how much the thin history changes the result.
// --daily FILE: flat-to-flat mode — every coin-day runs from a fresh simulator and ends flat (taker), exactly
// like the backtests and the paper engine's UTC day roll; writes {"rows":[{day, coin, cfg, pnl_usd, volume_usd,
// fills, capture_bps, mk10_bps, hours, max_gap_min, books}]} (max_gap_min = the longest silence between two books, minutes;
// books = book events that coin-day — mm_gates.sh judges a day complete on the busiest coin's hours and gap) and prints a short summary instead of the continuous report. This is
// what the gate job (scripts/mm_gates.sh) reads for G1.
// --t0-ms / --t1-ms MS (epoch ms): replay only the events inside that window (books are rebuilt from the whole file
// first, so a Lighter book is correct at the window start) — scripts/mm_debug.sh replay compares it with mm_paper_fills.
// --params FILE runs one frozen strategy per coin (mm_opt's choice) instead of the fixed set: the
// forward test of an optimized setup on live recordings.
// --dash DIR writes the dashboard's four tables as {"rows":[...]} JSON: configs.json (totals + markout
// curve per configuration), equity.json (minute equity per configuration, all coins summed, each coin
// forward-filled), coins.json (coin × configuration) and markets.json (spread and activity per coin).

#include "dex/hl_events.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

struct Config {
    std::string name, venue;
    hl::Params p;
    std::map<std::string, hl::Params> per_coin;     // --params: each coin's own frozen strategy
    const hl::Params& for_coin(const std::string& c) const { auto it = per_coin.find(c); return it == per_coin.end() ? p : it->second; }
};

// --params FILE (written by mm_opt): {"venue","maker_bps","taker_bps","latency_ms","order_usd","max_inv",
// "coins":{"<COIN>":{"half_width_ticks","skew_ticks","fair":"mid"|"micro","min_spread_ticks","imb_pull",
// "pause_move_ticks","pause_ms","requote_ticks","venue_tick","tick_rule":"book"|"hyperliquid","hl_sz_decimals"}}} → "chosen/pess" + "chosen/opt".
static std::vector<Config> configs_from_params(const fs::path& file, std::set<std::string>& only) {
    std::ifstream in(file);
    json j = json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.contains("coins")) throw std::runtime_error("cannot read --params " + file.string());
    std::vector<Config> v;
    for (int opt = 0; opt < 2; ++opt) {
        Config c{std::string("chosen") + (opt ? "/opt" : "/pess"), j.value("venue", "hyperliquid"), {}, {}};
        c.p.maker_fee_bps = j.value("maker_bps", 1.5);
        c.p.taker_fee_bps = j.value("taker_bps", 4.5);
        c.p.latency_ms = j.value("latency_ms", int64_t{200});
        c.p.order_usd = j.value("order_usd", 100.0);
        c.p.max_inv_units = j.value("max_inv", 3);
        c.p.cancels = opt ? hl::CancelModel::Optimistic : hl::CancelModel::Pessimistic;
        for (auto& [coin, cj] : j["coins"].items()) {
            hl::Params q = c.p;
            q.half_width_ticks = cj.value("half_width_ticks", 0.5);
            q.skew_ticks = cj.value("skew_ticks", 0.0);
            q.fair = cj.value("fair", "mid") == "micro" ? hl::Fair::Micro : hl::Fair::Mid;
            q.min_spread_ticks = cj.value("min_spread_ticks", 0.0);
            q.imb_pull = cj.value("imb_pull", 0.0);
            q.pause_move_ticks = cj.value("pause_move_ticks", 0.0);
            q.pause_ms = cj.value("pause_ms", int64_t{0});
            q.requote_ticks = cj.value("requote_ticks", 0.0);
            q.venue_tick = cj.value("venue_tick", 0.0);
            q.strat_tick_rule = cj.value("tick_rule", "book") == "hyperliquid" ? hl::TickRule::Hyperliquid : hl::TickRule::Book;
            q.hl_sz_decimals = cj.value("hl_sz_decimals", 0);
            q.size_step = cj.value("size_step", 0.0);           // venue size rules (mm_paper status.json params; 0 = continuous)
            q.min_base = cj.value("min_base", 0.0);
            q.min_quote_usd = cj.value("min_quote_usd", 0.0);
            c.per_coin[coin] = q;
            if (!opt) only.insert(coin);
        }
        v.push_back(std::move(c));
    }
    return v;
}

static std::vector<Config> configs(double order_usd, int max_inv, bool sweep) {
    std::vector<Config> v;
    auto add = [&](const std::string& venue, double maker, double taker, int64_t lat, bool opt) {
        hl::Params p;
        p.order_usd = order_usd;
        p.max_inv_units = max_inv;
        p.maker_fee_bps = maker;
        p.taker_fee_bps = taker;
        p.latency_ms = lat;
        p.cancels = opt ? hl::CancelModel::Optimistic : hl::CancelModel::Pessimistic;
        std::string name = venue == "hyperliquid" ? "hl" : "lt";
        if (sweep && venue == "hyperliquid" && lat != 200) name += std::to_string(lat);
        v.push_back({name + (opt ? "/opt" : "/pess"), venue, p, {}});
    };
    for (int opt = 0; opt < 2; ++opt) add("hyperliquid", 1.5, 4.5, 200, opt);
    for (int opt = 0; opt < 2; ++opt) add("lighter", 0.0, 0.0, 300, opt);
    if (sweep)
        for (int64_t lat : {50, 500})
            for (int opt = 0; opt < 2; ++opt) add("hyperliquid", 1.5, 4.5, lat, opt);
    return v;
}

struct Row { std::string coin, cfg; hl::Result r; };
struct CoinStats { double spread_sum = 0; int64_t books = 0, trades = 0; double trade_usd = 0; int64_t first = 0, last = 0; };

static double safe_div(double a, double b) { return b > 0 ? a / b : 0; }

int main(int argc, char** argv) {
    fs::path dir, report, dash_dir, params_file, daily_file;
    std::set<std::string> only;
    std::string from = "0000-00-00", to = "9999-99-99";
    double order_usd = 100;
    int max_inv = 3, max_levels = 0;
    int64_t t0_ms = 0, t1_ms = 0;          // --t0-ms / --t1-ms: replay only events inside [t0, t1] (0 = open end)
    bool sweep = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto val = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--dir") dir = val();
        else if (a == "--coins") { std::stringstream ss(val()); for (std::string c; std::getline(ss, c, ',');) only.insert(c); }
        else if (a == "--from") from = val();
        else if (a == "--to") to = val();
        else if (a == "--order-usd") order_usd = std::atof(val().c_str());
        else if (a == "--max-inv") max_inv = std::atoi(val().c_str());
        else if (a == "--sweep") sweep = true;
        else if (a == "--max-levels") max_levels = std::atoi(val().c_str());
        else if (a == "--t0-ms") t0_ms = std::atoll(val().c_str());
        else if (a == "--t1-ms") t1_ms = std::atoll(val().c_str());
        else if (a == "--report") report = val();
        else if (a == "--dash") dash_dir = val();
        else if (a == "--params") params_file = val();
        else if (a == "--daily") daily_file = val();
        else {
            std::fprintf(stderr, "usage: mm_sim --dir DIR [--coins A,B] [--from D] [--to D] [--order-usd N] [--max-inv N] "
                                 "[--sweep] [--params FILE] [--max-levels N] [--t0-ms MS] [--t1-ms MS] [--report FILE] [--dash DIR] [--daily FILE]\n");
            return 2;
        }
    }
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) { std::fprintf(stderr, "--dir must be an existing directory\n"); return 2; }

    std::vector<Config> cfgs;
    try { cfgs = params_file.empty() ? configs(order_usd, max_inv, sweep) : configs_from_params(params_file, only); }
    catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 2; }

    // Events kept from the full day file: the book is rebuilt from the file's own snapshot, then only the window is replayed.
    auto window = [&](std::vector<hl::Event>& ev) {
        if (!t0_ms && !t1_ms) return;
        std::erase_if(ev, [&](const hl::Event& e) { return (t0_ms && e.t_ms < t0_ms) || (t1_ms && e.t_ms > t1_ms); });
    };
    std::map<std::string, std::vector<hl::DayFile>> by_coin;
    for (auto& d : hl::list_days(dir))
        if ((only.empty() || only.count(d.coin)) && d.day >= from && d.day <= to) by_coin[d.coin].push_back(d);
    std::vector<std::string> coins;
    for (const auto& [c, v] : by_coin) coins.push_back(c);

    std::vector<Row> rows;
    std::map<std::string, CoinStats> stats;
    int64_t bad_lines = 0;
    if (!daily_file.empty()) {
        json drows = json::array();
        double tot_pnl = 0, tot_vol = 0; int64_t tot_fills = 0, n = 0;
        for (const auto& coin : coins)
            for (const auto& d : by_coin[coin]) {
                auto ev = hl::load_day(d, &bad_lines);
                window(ev);
                if (max_levels > 0)
                    for (auto& e : ev)
                        if (e.is_book) {
                            if ((int)e.book.bids.size() > max_levels) e.book.bids.resize(max_levels);
                            if ((int)e.book.asks.size() > max_levels) e.book.asks.resize(max_levels);
                        }
                int64_t first = 0, last = 0, max_gap = 0, nbooks = 0;
                for (const auto& e : ev) if (e.is_book) {
                    if (first) max_gap = std::max(max_gap, e.book.t_ms - last);   // longest silence between two books
                    if (!first) first = e.book.t_ms;
                    last = e.book.t_ms;
                    ++nbooks;
                }
                for (const auto& c : cfgs) {
                    hl::Simulator s(c.for_coin(coin));
                    hl::run_events(ev, s);
                    s.flatten_now(last);
                    const auto r = s.finish();
                    drows.push_back({{"day", d.day}, {"coin", coin}, {"cfg", c.name}, {"pnl_usd", r.flat_pnl}, {"volume_usd", r.volume_usd},
                                     {"fills", r.fills}, {"capture_bps", safe_div(r.capture_bps_sum, r.volume_usd)},
                                     {"mk10_bps", safe_div(r.mk10_bps_sum, r.mk_weight)}, {"hours", (last - first) / 3'600'000.0},
                                     {"max_gap_min", max_gap / 60'000.0}, {"books", nbooks}});
                    if (c.name == cfgs.front().name) { tot_pnl += r.flat_pnl; tot_vol += r.volume_usd; tot_fills += r.fills; ++n; }
                }
            }
        fs::create_directories(daily_file.parent_path(), ec);
        std::ofstream(daily_file) << json{{"rows", drows}}.dump();
        std::printf("daily (flat-to-flat): %lld coin-days, first config %s: P&L %.2f on $%.0f (%.2f bps), %lld fills\n",
                    (long long)n, cfgs.front().name.c_str(), tot_pnl, tot_vol, safe_div(tot_pnl, tot_vol) * 1e4, (long long)tot_fills);
        return 0;
    }
    for (const auto& coin : coins) {
        std::vector<hl::Simulator> sims;
        for (const auto& c : cfgs) sims.emplace_back(c.for_coin(coin));
        CoinStats& st = stats[coin];
        for (const auto& d : by_coin[coin]) {          // days run back to back through the same simulators
            auto ev = hl::load_day(d, &bad_lines);
                window(ev);
            if (max_levels > 0)                       // validation: show the live book as thin as Tardis's history
                for (auto& e : ev)
                    if (e.is_book) {
                        if ((int)e.book.bids.size() > max_levels) e.book.bids.resize(max_levels);
                        if ((int)e.book.asks.size() > max_levels) e.book.asks.resize(max_levels);
                    }
            for (const auto& e : ev) {
                if (e.is_book) {
                    st.spread_sum += (e.book.asks[0].px - e.book.bids[0].px) / e.book.mid() * 1e4;
                    ++st.books;
                    if (!st.first) st.first = e.book.t_ms;
                    st.last = e.book.t_ms;
                } else {
                    ++st.trades;
                    st.trade_usd += e.trade.px * e.trade.sz;
                }
            }
            for (auto& s : sims) hl::run_events(ev, s);
        }
        for (size_t k = 0; k < sims.size(); ++k) rows.push_back({coin, cfgs[k].name, sims[k].finish()});
    }

    // ── markdown report ─────────────────────────────────────────────────────
    std::ostringstream o;
    char b[512];
    o << "# mm_sim — queue-aware market making on recorded Hyperliquid books\n\n";
    std::snprintf(b, sizeof b, "order $%.0f, inventory cap %d orders each way, files %s..%s, %zu coins\n\n",
                  order_usd, max_inv, from.c_str(), to.c_str(), coins.size());
    o << b;
    o << "Columns: capture = spread earned vs mid at the fill; mk10 = mid move 10 s later in our favour "
         "(negative = picked off); net = P&L after fees and a taker flatten, per $ traded. bps are $-weighted.\n\n";
    o << "| coin | cfg | hours | fills | volume $ | capture bps | mk1 bps | mk10 bps | mk60 bps | fee bps | net bps | P&L $ | $/day | max DD $ |\n";
    o << "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n";
    std::map<std::string, hl::Result> tot;
    std::map<std::string, double> tot_hours;
    for (const auto& row : rows) {
        const auto& r = row.r;
        const double vol = r.volume_usd;
        const double w = r.mk_weight > 0 ? r.mk_weight : 1;
        std::snprintf(b, sizeof b, "| %s | %s | %.1f | %lld | %.0f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f |\n",
                      row.coin.c_str(), row.cfg.c_str(), r.days() * 24, (long long)r.fills, vol,
                      safe_div(r.capture_bps_sum, vol), r.mk1_bps_sum / w, r.mk10_bps_sum / w, r.mk60_bps_sum / w,
                      safe_div(r.fees, vol) * 1e4, safe_div(r.flat_pnl, vol) * 1e4, r.flat_pnl,
                      safe_div(r.flat_pnl, r.days()), r.max_dd);
        o << b;
        auto& t = tot[row.cfg];
        t.fills += r.fills; t.volume_usd += r.volume_usd; t.fees += r.fees; t.flat_pnl += r.flat_pnl;
        t.capture_bps_sum += r.capture_bps_sum; t.mk10_bps_sum += r.mk10_bps_sum; t.mk_weight += r.mk_weight;
        for (int k = 0; k < hl::N_MARKOUT; ++k) t.mk_curve_sum[k] += r.mk_curve_sum[k];
        t.max_dd += r.max_dd;
        tot_hours[row.cfg] = std::max(tot_hours[row.cfg], r.days() * 24);
    }
    o << "\n## All coins together\n\n| cfg | fills | volume $ | capture bps | mk10 bps | fee bps | net bps | P&L $ | $/day | Σ max DD $ |\n|---|---|---|---|---|---|---|---|---|---|\n";
    for (const auto& c : cfgs) {
        const auto& t = tot[c.name];
        const double vol = t.volume_usd, w = t.mk_weight > 0 ? t.mk_weight : 1;
        const double days = tot_hours[c.name] / 24;
        std::snprintf(b, sizeof b, "| %s | %lld | %.0f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f |\n", c.name.c_str(),
                      (long long)t.fills, vol, safe_div(t.capture_bps_sum, vol), t.mk10_bps_sum / w,
                      safe_div(t.fees, vol) * 1e4, safe_div(t.flat_pnl, vol) * 1e4, t.flat_pnl,
                      safe_div(t.flat_pnl, days), t.max_dd);
        o << b;
    }
    if (bad_lines) {
        std::snprintf(b, sizeof b, "\n%lld unreadable lines skipped.\n", (long long)bad_lines);
        o << b;
    }
    std::fputs(o.str().c_str(), stdout);
    if (!report.empty()) {
        fs::create_directories(report.parent_path(), ec);
        std::ofstream(report) << o.str();
    }

    // ── dashboard tables (flat {"rows":[...]} files, one per table) ───────────
    if (!dash_dir.empty()) {
        fs::create_directories(dash_dir, ec);
        auto iso = [](int64_t ms) {
            std::time_t s = static_cast<std::time_t>(ms / 1000);
            char t[32]; std::strftime(t, sizeof t, "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&s));
            return std::string(t);
        };
        auto write = [&](const char* name, const json& rows) { std::ofstream(dash_dir / name) << json{{"rows", rows}}.dump(); };
        json markets = json::array();
        for (const auto& [coin, st] : stats)
            markets.push_back({{"coin", coin}, {"avg_spread_bps", safe_div(st.spread_sum, st.books)}, {"books", st.books},
                               {"trades", st.trades}, {"trade_usd", st.trade_usd}, {"hours", (st.last - st.first) / 3'600'000.0}});
        json crows = json::array(), erows = json::array(), prow = json::array();
        for (const auto& c : cfgs) {
            const auto& t = tot[c.name];
            const double vol = t.volume_usd, w = t.mk_weight;
            json row = {{"cfg", c.name}, {"venue", c.venue}, {"latency_ms", c.p.latency_ms},
                        {"queue", c.p.cancels == hl::CancelModel::Optimistic ? "optimistic" : "pessimistic"},
                        {"maker_bps", c.p.maker_fee_bps}, {"fills", t.fills}, {"volume_usd", vol},
                        {"capture_bps", safe_div(t.capture_bps_sum, vol)}};
            for (int k = 0; k < hl::N_MARKOUT; ++k)
                row["mk_" + std::to_string(hl::MARKOUT_S[k]) + "s"] = safe_div(t.mk_curve_sum[k], w);
            row["fee_bps"] = safe_div(t.fees, vol) * 1e4;
            row["net_bps"] = safe_div(t.flat_pnl, vol) * 1e4;
            row["pnl_usd"] = t.flat_pnl;
            row["hours"] = tot_hours[c.name];
            crows.push_back(row);
            // Equity: each coin's minute samples forward-filled onto one minute grid, summed.
            std::map<int64_t, double> grid;
            std::vector<const hl::Result*> rs;
            for (const auto& r : rows) {
                if (r.cfg != c.name) continue;
                rs.push_back(&r.r);
                for (const auto& e : r.r.equity) grid[e.first / 60'000 * 60'000] = 0;
                prow.push_back({{"coin", r.coin}, {"cfg", r.cfg}, {"fills", r.r.fills}, {"volume_usd", r.r.volume_usd},
                                {"capture_bps", safe_div(r.r.capture_bps_sum, r.r.volume_usd)},
                                {"mk10_bps", safe_div(r.r.mk10_bps_sum, r.r.mk_weight)},
                                {"net_bps", safe_div(r.r.flat_pnl, r.r.volume_usd) * 1e4},
                                {"pnl_usd", r.r.flat_pnl}, {"max_dd", r.r.max_dd}});
            }
            for (const auto* r : rs) {
                size_t k = 0;
                double cur = 0;
                for (auto& [minute, sum] : grid) {
                    while (k < r->equity.size() && r->equity[k].first < minute + 60'000) cur = r->equity[k++].second;
                    sum += cur;
                }
            }
            for (const auto& [minute, sum] : grid)
                erows.push_back({{"cfg", c.name}, {"t", iso(minute)}, {"equity_usd", std::round(sum * 1e4) / 1e4}});
        }
        write("configs.json", crows);
        write("equity.json", erows);
        write("coins.json", prow);
        write("markets.json", markets);
    }
    return 0;
}
