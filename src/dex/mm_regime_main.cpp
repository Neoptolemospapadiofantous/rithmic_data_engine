// mm_regime — regime filters on the frozen market-making setting (founder 2026-10-25: "test regime filter on the
// strategy to see performance metrics"). Research only, no orders.
//
//   mm_regime --dir DIR [--coins A,B] [--threads 8] [--split YYYY-MM-DD] [--report FILE] [--dash DIR] [--allow feature=i,j ...] [--params FILE]
//
// A regime is something known at quote time, measured from the same feed the maker sees:
//   tod       UTC time of day in 4-hour blocks (00-04 … 20-24)
//   vol       5-minute mid range in bps of the mid: <5, 5-10, 10-20, ≥20
//   activity  trades in the last 60 s: 0-5, 6-20, 21-60, >60
//   spread    top-of-book spread in bps: <2, 2-5, 5-10, ≥10
// Pass 1 runs every coin-day once per bucket with the maker HALTED outside that bucket (hl::Simulator::set_halted —
// resting quotes are cancelled, inventory is kept and marked), so each run is the setting's performance inside one
// regime. Pass 2 is the pre-registered test: for every feature the buckets with positive net bps on the TUNING days
// (before --split) become the filter, and the filter (and the AND of all four) is judged on the HOLDOUT days only,
// against the unfiltered setting on the same days. Net bps = flat-to-flat P&L / $ traded.
// --allow feature=i,j (repeatable; bucket indices from the FEATURES table) adds a PRE-REGISTERED filter to pass 2 — a
// rule written down before the run, judged on the holdout days of this directory; with --split before the first day,
// every day is holdout (the 15 unseen coins in tardis_extra). The Tardis history is in the book tick rule, the unit
// the backtests used.

#include "dex/hl_events.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

// --params FILE (the mm_sim / mm_gates format): per-coin tick unit + venue grid, and the file's fees and delay. Lets the
// regime test run on a venue whose grid is not the strategy's tick (Lighter: tick_rule "hyperliquid"). Without it the
// tool behaves as before (book tick rule, Lighter fees, 300 ms).
static json g_params;
static hl::Params frozen(const std::string& coin = "") {
    hl::Params p;
    p.order_usd = 100; p.max_inv_units = 3;
    p.maker_fee_bps = 0; p.taker_fee_bps = 0; p.latency_ms = 300;
    p.cancels = hl::CancelModel::Pessimistic;
    p.half_width_ticks = 5; p.min_spread_ticks = 4; p.imb_pull = 0.6;
    p.pause_move_ticks = 2; p.pause_ms = 2000; p.requote_ticks = 1;
    p.track_markouts = false;
    if (!g_params.is_null()) {
        p.maker_fee_bps = g_params.value("maker_bps", 0.0); p.taker_fee_bps = g_params.value("taker_bps", 0.0);
        p.latency_ms = g_params.value("latency_ms", int64_t{300});
        if (g_params["coins"].contains(coin)) {
            const auto& cj = g_params["coins"][coin];
            p.venue_tick = cj.value("venue_tick", 0.0);
            p.strat_tick_rule = cj.value("tick_rule", "book") == "hyperliquid" ? hl::TickRule::Hyperliquid : hl::TickRule::Book;
            p.hl_sz_decimals = cj.value("hl_sz_decimals", 0);
        }
    }
    return p;
}

// ── regime features, updated event by event ──────────────────────────────────────────────────────────────
struct Feature { std::string name; std::vector<std::string> buckets; };
static const std::vector<Feature> FEATURES = {
    {"tod", {"00-04", "04-08", "08-12", "12-16", "16-20", "20-24"}},
    {"vol", {"<5 bps", "5-10 bps", "10-20 bps", "≥20 bps"}},
    {"activity", {"0-5 trades/min", "6-20", "21-60", ">60"}},
    {"spread", {"<2 bps", "2-5 bps", "5-10 bps", "≥10 bps"}},
};
constexpr int NF = 4;

class Regime {
public:
    void on_book(const hl::Book& b) {
        const double mid = b.mid();
        mids_.push_back({b.t_ms, mid});
        while (!mids_.empty() && mids_.front().first < b.t_ms - 300'000) mids_.pop_front();
        double lo = mid, hi = mid;
        for (const auto& [t, m] : mids_) { lo = std::min(lo, m); hi = std::max(hi, m); }
        range_bps_ = mid > 0 ? (hi - lo) / mid * 1e4 : 0;
        spread_bps_ = mid > 0 ? (b.asks[0].px - b.bids[0].px) / mid * 1e4 : 0;
        trim(b.t_ms);
    }
    void on_trade(const hl::Trade& t) { trades_.push_back(t.t_ms); trim(t.t_ms); }
    // bucket index per feature at `now` (ms, exchange time)
    int bucket(int f, int64_t now) const {
        switch (f) {
            case 0: { const int64_t h = (now / 3'600'000) % 24; return (int)(h / 4); }
            case 1: return range_bps_ < 5 ? 0 : range_bps_ < 10 ? 1 : range_bps_ < 20 ? 2 : 3;
            case 2: { const size_t n = trades_.size(); return n <= 5 ? 0 : n <= 20 ? 1 : n <= 60 ? 2 : 3; }
            default: return spread_bps_ < 2 ? 0 : spread_bps_ < 5 ? 1 : spread_bps_ < 10 ? 2 : 3;
        }
    }
private:
    void trim(int64_t now) { while (!trades_.empty() && trades_.front() < now - 60'000) trades_.pop_front(); }
    std::deque<std::pair<int64_t, double>> mids_;
    std::deque<int64_t> trades_;
    double range_bps_ = 0, spread_bps_ = 0;
};

// One run: quote only while allow[f][bucket] holds for every feature in `use`.
struct Run { std::string name; std::vector<std::vector<bool>> allow; };   // allow[f] empty = feature not used
struct Cell { double pnl = 0, vol = 0, dd = 0; int64_t fills = 0; double active_ms = 0; };

static Cell run_one(const std::vector<hl::Event>& ev, const Run& r, const std::string& coin) {
    hl::Simulator sim(frozen(coin));
    Regime rg;
    bool halted = false;
    int64_t last_t = 0, active_ms = 0;
    bool have = false;
    for (const auto& e : ev) {
        if (e.is_book && !e.book.valid()) continue;
        if (e.is_book) rg.on_book(e.book); else rg.on_trade(e.trade);
        bool ok = true;
        for (int f = 0; f < NF && ok; ++f) if (!r.allow[f].empty() && !r.allow[f][rg.bucket(f, e.t_ms)]) ok = false;
        if (have && !halted) active_ms += e.t_ms - last_t;
        last_t = e.t_ms; have = true;
        if (ok == halted) { halted = !ok; sim.set_halted(halted, e.t_ms); }
        if (e.is_book) sim.on_book(e.book); else sim.on_trade(e.trade);
    }
    const auto res = sim.finish();
    return {res.flat_pnl, res.volume_usd, res.max_dd, res.fills, (double)active_ms};
}

static double safe_div(double a, double b) { return b != 0 ? a / b : 0; }

int main(int argc, char** argv) {
    fs::path dir, report, dash_dir;
    std::set<std::string> only;
    int threads = 8;
    std::string split = "2025-11-01";
    std::vector<std::vector<bool>> prereg(NF);
    bool have_prereg = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto val = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--dir") dir = val();
        else if (a == "--allow") {
            const std::string v = val();
            const auto eq = v.find('=');
            int f = -1;
            for (int q = 0; q < NF; ++q) if (FEATURES[q].name == v.substr(0, eq)) f = q;
            if (eq == std::string::npos || f < 0) { std::fprintf(stderr, "--allow wants feature=i,j with feature in tod|vol|activity|spread\n"); return 2; }
            prereg[f] = std::vector<bool>(FEATURES[f].buckets.size(), false);
            std::stringstream ss(v.substr(eq + 1));
            for (std::string t; std::getline(ss, t, ',');) { const int b = std::atoi(t.c_str()); if (b >= 0 && b < (int)prereg[f].size()) prereg[f][b] = true; }
            have_prereg = true;
        }
        else if (a == "--coins") { std::stringstream ss(val()); for (std::string c; std::getline(ss, c, ',');) only.insert(c); }
        else if (a == "--threads") threads = std::max(1, std::atoi(val().c_str()));
        else if (a == "--split") split = val();
        else if (a == "--report") report = val();
        else if (a == "--dash") dash_dir = val();
        else if (a == "--params") {
            std::ifstream in(val()); g_params = json::parse(in, nullptr, false);
            if (g_params.is_discarded() || !g_params.contains("coins")) { std::fprintf(stderr, "cannot read --params\n"); return 2; }
            for (auto& [c, _] : g_params["coins"].items()) only.insert(c);
        }
        else { std::fprintf(stderr, "usage: mm_regime --dir DIR [--coins A,B] [--threads N] [--split YYYY-MM-DD] [--report FILE] [--dash DIR] [--params FILE]\n"); return 2; }
    }
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) { std::fprintf(stderr, "--dir must be an existing directory\n"); return 2; }
    std::vector<hl::DayFile> jobs;
    for (auto& d : hl::list_days(dir)) if (only.empty() || only.count(d.coin)) jobs.push_back(d);
    if (jobs.empty()) { std::fprintf(stderr, "no coin-days under %s\n", dir.c_str()); return 1; }

    // pass-1 runs: baseline + one per (feature, bucket)
    std::vector<Run> runs;
    runs.push_back({"baseline", std::vector<std::vector<bool>>(NF)});
    for (int f = 0; f < NF; ++f)
        for (size_t b = 0; b < FEATURES[f].buckets.size(); ++b) {
            Run r{FEATURES[f].name + " · " + FEATURES[f].buckets[b], std::vector<std::vector<bool>>(NF)};
            r.allow[f] = std::vector<bool>(FEATURES[f].buckets.size(), false);
            r.allow[f][b] = true;
            runs.push_back(std::move(r));
        }
    std::vector<std::vector<Cell>> res(jobs.size(), std::vector<Cell>(runs.size()));
    std::vector<char> ok(jobs.size(), 0);
    std::atomic<size_t> next{0}, done{0};
    std::mutex mu;
    auto worker = [&]() {
        for (size_t j; (j = next++) < jobs.size();) {
            const auto ev = hl::load_day(jobs[j]);
            for (const auto& e : ev) if (e.is_book) { ok[j] = 1; break; }
            for (size_t k = 0; k < runs.size(); ++k) res[j][k] = run_one(ev, runs[k], jobs[j].coin);
            const size_t n = ++done;
            std::lock_guard lk(mu);
            std::fprintf(stderr, "[%zu/%zu] %s %s\n", n, jobs.size(), jobs[j].coin.c_str(), jobs[j].day.c_str());
        }
    };
    { std::vector<std::thread> pool; for (int t = 0; t < threads; ++t) pool.emplace_back(worker); for (auto& t : pool) t.join(); }

    // ── per bucket: tuning vs holdout ───────────────────────────────────────────────────────────────────
    struct Agg { double pnl = 0, vol = 0, act = 0; int64_t fills = 0; std::map<std::string, double> by_day; };
    auto agg = [&](size_t k, bool holdout) {
        Agg a;
        for (size_t j = 0; j < jobs.size(); ++j) {
            if (!ok[j] || (jobs[j].day >= split) != holdout) continue;
            a.pnl += res[j][k].pnl; a.vol += res[j][k].vol; a.fills += res[j][k].fills; a.act += res[j][k].active_ms;
            a.by_day[jobs[j].day] += res[j][k].pnl;
        }
        return a;
    };
    const Agg base_t = agg(0, false), base_h = agg(0, true);
    json brows = json::array();
    std::vector<std::vector<bool>> chosen(NF);
    size_t k = 1;
    for (int f = 0; f < NF; ++f) {
        chosen[f] = std::vector<bool>(FEATURES[f].buckets.size(), false);
        for (size_t b = 0; b < FEATURES[f].buckets.size(); ++b, ++k) {
            const Agg t = agg(k, false), h = agg(k, true);
            int64_t pos = 0; for (const auto& [d, p] : h.by_day) pos += p > 0;
            const bool pick = t.vol > 0 && t.pnl / t.vol > 0;        // chosen on tuning only
            chosen[f][b] = pick;
            brows.push_back({{"feature", FEATURES[f].name}, {"bucket", FEATURES[f].buckets[b]},
                             {"tune_pnl_usd", t.pnl}, {"tune_net_bps", safe_div(t.pnl, t.vol) * 1e4}, {"tune_vol_share_pct", safe_div(100 * t.vol, base_t.vol)},
                             {"hold_pnl_usd", h.pnl}, {"hold_net_bps", safe_div(h.pnl, h.vol) * 1e4}, {"hold_vol_share_pct", safe_div(100 * h.vol, base_h.vol)},
                             {"hold_fills", h.fills}, {"hold_positive_days", pos}, {"hold_days", (int64_t)h.by_day.size()},
                             {"time_share_pct", safe_div(100 * h.act, base_h.act)}, {"chosen", pick}});
        }
    }

    // ── pass 2: the chosen filters on the holdout days ───────────────────────────────────────────────────
    std::vector<Run> filters;
    for (int f = 0; f < NF; ++f) { Run r{FEATURES[f].name + " filter", std::vector<std::vector<bool>>(NF)}; r.allow[f] = chosen[f]; filters.push_back(r); }
    { Run r{"all four filters", std::vector<std::vector<bool>>(NF)}; for (int f = 0; f < NF; ++f) r.allow[f] = chosen[f]; filters.push_back(r); }
    if (have_prereg) filters.push_back({"pre-registered (--allow)", prereg});
    std::vector<std::vector<Cell>> fres(jobs.size(), std::vector<Cell>(filters.size()));
    next = 0;
    auto worker2 = [&]() {                       // holdout days are reloaded, not cached: a year of books does not fit in RAM
        for (size_t j; (j = next++) < jobs.size();) {
            if (!ok[j] || jobs[j].day < split) continue;
            const auto ev = hl::load_day(jobs[j]);
            for (size_t q = 0; q < filters.size(); ++q) fres[j][q] = run_one(ev, filters[q], jobs[j].coin);
        }
    };
    { std::vector<std::thread> pool; for (int t = 0; t < threads; ++t) pool.emplace_back(worker2); for (auto& t : pool) t.join(); }
    json frows = json::array();
    auto frow = [&](const std::string& name, auto get) {
        double pnl = 0, vol = 0, act = 0, worst = 0; int64_t fills = 0, pos = 0;
        std::map<std::string, double> by_day;
        for (size_t j = 0; j < jobs.size(); ++j) {
            if (!ok[j] || jobs[j].day < split) continue;
            const Cell& c = get(j);
            pnl += c.pnl; vol += c.vol; fills += c.fills; act += c.active_ms; by_day[jobs[j].day] += c.pnl;
        }
        for (const auto& [d, p] : by_day) { pos += p > 0; worst = std::min(worst, p); }
        frows.push_back({{"variant", name}, {"hold_pnl_usd", pnl}, {"hold_net_bps", safe_div(pnl, vol) * 1e4}, {"hold_vol_usd", vol},
                         {"hold_fills", fills}, {"positive_days", pos}, {"days", (int64_t)by_day.size()}, {"worst_day_usd", worst},
                         {"time_share_pct", safe_div(100 * act, base_h.act)}, {"mean_day_usd", safe_div(pnl, (double)by_day.size())}});
    };
    frow("unfiltered (frozen setting)", [&](size_t j) -> const Cell& { return res[j][0]; });
    for (size_t q = 0; q < filters.size(); ++q) frow(filters[q].name, [&](size_t j) -> const Cell& { return fres[j][q]; });

    // ── report ──────────────────────────────────────────────────────────────────────────────────────────
    std::ostringstream o;
    char b[512];
    std::snprintf(b, sizeof b, "# mm_regime — regime filters on the frozen setting\n\n%zu coin-days; tuning = days before %s, holdout = from %s. "
                  "Each bucket row = the setting quoting ONLY in that regime; a bucket is chosen when its tuning net bps > 0.\n\n", jobs.size(), split.c_str(), split.c_str());
    o << b << "| feature | bucket | tuning net bps | tuning vol share | holdout net bps | holdout P&L $ | holdout vol share | holdout +days | time share | chosen |\n|---|---|---|---|---|---|---|---|---|---|\n";
    for (const auto& r : brows) {
        std::snprintf(b, sizeof b, "| %s | %s | %.2f | %.0f%% | %.2f | %.0f | %.0f%% | %lld/%lld | %.0f%% | %s |\n", r["feature"].get<std::string>().c_str(),
                      r["bucket"].get<std::string>().c_str(), r["tune_net_bps"].get<double>(), r["tune_vol_share_pct"].get<double>(), r["hold_net_bps"].get<double>(),
                      r["hold_pnl_usd"].get<double>(), r["hold_vol_share_pct"].get<double>(), (long long)r["hold_positive_days"].get<int64_t>(),
                      (long long)r["hold_days"].get<int64_t>(), r["time_share_pct"].get<double>(), r["chosen"].get<bool>() ? "yes" : "no");
        o << b;
    }
    o << "\n## Filters chosen on tuning, judged on holdout\n\n| variant | holdout P&L $ | net bps | $ traded | fills | positive days | worst day $ | mean day $ | time quoting |\n|---|---|---|---|---|---|---|---|---|\n";
    for (const auto& r : frows) {
        std::snprintf(b, sizeof b, "| %s | %.0f | %.2f | %.0f | %lld | %lld/%lld | %.0f | %.1f | %.0f%% |\n", r["variant"].get<std::string>().c_str(),
                      r["hold_pnl_usd"].get<double>(), r["hold_net_bps"].get<double>(), r["hold_vol_usd"].get<double>(), (long long)r["hold_fills"].get<int64_t>(),
                      (long long)r["positive_days"].get<int64_t>(), (long long)r["days"].get<int64_t>(), r["worst_day_usd"].get<double>(),
                      r["mean_day_usd"].get<double>(), r["time_share_pct"].get<double>());
        o << b;
    }
    std::fputs(o.str().c_str(), stdout);
    if (!report.empty()) { fs::create_directories(report.parent_path(), ec); std::ofstream(report) << o.str(); }
    if (!dash_dir.empty()) {
        fs::create_directories(dash_dir, ec);
        std::ofstream(dash_dir / "regime_buckets.json") << json{{"rows", brows}}.dump();
        std::ofstream(dash_dir / "regime_filters.json") << json{{"rows", frows}}.dump();
    }
    return 0;
}
