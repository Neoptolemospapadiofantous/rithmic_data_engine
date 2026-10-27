#pragma once
// mm_lab.hpp — the deterministic research JUDGE for the DEX market maker (2026-10-26). Research only: reads archives
// and the frozen params files, writes report cards and an append-only registry. No network, no keys, no orders, no DB.
//
// An IDEA (from a human or an agent) is a small JSON spec: a venue, markets, overrides of the frozen strategy's
// parameters, a quote size, a fair-price model and a list of RULES over a causal FEATURE LIBRARY that pull, shift or
// widen the quotes. Everything else — fill model, fees, latency, data split, baseline, pass rule — is fixed here, in
// code, before any idea is seen. That is the point: an idea can only change what the strategy does, never how it is
// judged. mm_lab_main.cpp is the CLI, tests/dex/test_mm_lab.cpp the tests, scripts/starnet/desk_mm.sh the desk wrapper.
//
// ── Idea JSON ────────────────────────────────────────────────────────────────────────────────────────────────────
//   {"ideas":[{
//      "id": "lower_snake",                      required, [a-z0-9_]{3,64}
//      "venue": "lighter"|"extended"|"aster"|"paradex"|"hyperliquid_hist",   required
//      "markets": ["AAVE",...] | "all",          default "all" (= every coin of the venue's params file with data)
//      "params": {<field>: value},               overrides of the frozen setting, fields:
//                  max_inv_units, half_width_ticks, skew_ticks (inventory lean), fair ("mid"|"micro"),
//                  min_spread_ticks, imb_pull, pause_move_ticks, pause_ms, requote_ticks
//                  (refused: fees, fill model, queue model, latency, ticks — the evaluation owns those)
//      "size_usd": 25,                           $ per quote; rounded UP to the venue's minimum order; default = frozen $100
//      "fair": {"type":"own_mid"} | {"type":"hl_anchor","k":0.5} | {"type":"ext_anchor","k":0.5} | {"type":"bn_anchor","k":0.5} |
//              {"type":"blend","weights":{"mid":1,"micro":0,"hl":1,"ext":1,"bn":1}}
//      "rules": [{"when":[[feature, op, value], ...], "action": "pull_bid"|"pull_ask"|"pull_both"|"skew_ticks"|"widen_ticks",
//                 "value": <ticks, for skew_ticks / widen_ticks>, "hold_ms": <keep the action this long after the
//                 condition was last true, default 0>}]
//      "rate_limit": {"per_min": 60, "accounts": 1},   optional; per_min ≤ the venue's standard, accounts 1..10
//      "latency": {"place_ms": 145, "cancel_ms": 445}, optional; never faster than the venue's standard
//      "execution": {"modify": true, "batch_linger_ms": 50},   optional (2026-10-27): HOW our system sends orders —
//                  modify = requote with one modify tx instead of cancel + create (rests at the old price until it
//                  lands, cancel latency, then the back of the new level's queue); batch_linger_ms 0..2000 = a new
//                  request waits this long to collect more transactions (rate-limited venues only). The same-size
//                  baseline keeps the live system's way (cancel + create, linger 0), so Δ is what the change is worth.
//                  Not varied by the neighbour check (a system choice, not a tuned knob).
//      "why": "...", "notes": "..."             free text (not part of the spec hash)
//   }]}
//   feature = a name from FEATURES below, windowed ones optionally "name@<ms>" (100 .. 3,600,000).
//   op = < <= > >= == != abs< abs>  (abs* compare |feature|). value = a number, or a strategy-parameter reference:
//   "half_width_ticks", "-half_width_ticks", "0.5*half_width_ticks" (params: half_width_ticks, skew_ticks,
//   min_spread_ticks, requote_ticks, pause_move_ticks, imb_pull, max_inv_units). All conditions of a rule must hold
//   (AND); a condition on an UNAVAILABLE value (warm-up, stale Hyperliquid, no book yet) is false. Pulls are OR-ed,
//   shifts and widenings add. Anything unknown is refused with a message — never silently ignored.
//
// ── Causality ───────────────────────────────────────────────────────────────────────────────────────────────────
// Features are computed in ONE forward pass over the venue's events in simulation order; the value at event i uses
// events ≤ i only. Cross-venue features use a REFERENCE venue's live archive — Hyperliquid (hl_*, cross_venue_lead_bps)
// or Extended (ext_*; since 2026-10-27, on every venue but Extended itself) or Binance futures (bn_*, 2026-10-27) — by
// RECEIVE time, cut at the DECISION time. Binance: the published trade history (data.binance.vision aggTrades, the last
// trade price, stamped Binance match time + kBinanceDelayMs = 135 ms, our measured Binance → box delay: p10–p90
// 132–136 ms on 27 Oct) for any day it exists, else the live archive (rithmic_dex/binance, bookTicker mids, our receive
// time). History first so every day is built the same way; the card names the source.
// the simulator acts on a venue event at its exchange time t, so only Hyperliquid books we had received by t count
// (hl_r ≤ t, a running max of t drives the pointer; stale when t − hl_r > 5 s → unavailable; Extended the same way).
// Using our receive time
// of the venue event instead would let a decision placed at t see up to ~150 ms of Hyperliquid's future (review
// 2026-10-26: 21 % of Lighter events arrive > 145 ms after t). Rules are evaluated at every venue event (books and
// trades) — not at Hyperliquid events — so a reaction waits for the next venue message.
//
// ── Pre-registered evaluation (constants below; changing one is a new judge, log it in the decision log) ────────
//   * fill model NoCross, pessimistic queue (hl_mm.hpp) — forced, whatever MM_FILLS says
//   * fees: lighter/extended/aster 0 maker, paradex 0.3, hyperliquid 1.5; taker as the params files (flattens only)
//   * latency: lighter place 145 / cancel 445 ms (Standard account from this box); extended/aster/paradex 100/100;
//     hyperliquid_hist 200/200. Rate limit ALWAYS ON at the venue's standard where one is modelled (Lighter Standard:
//     60 requests/min/account, ≤ 15 tx per batch, 1 account unless the idea asks for more or for a stricter limit);
//     such a venue replays every coin of a day together, books cut to 10 levels per side (memory).
//   * flat-to-flat UTC day segments (fresh simulator per coin-day, taker flatten at the end), like mm_sim --daily
//   * baselines: the venue's frozen setting (data/mm_gates/params_<venue>.json) on the same coins, window, latency
//     and rate limit, (a) at the idea's own size — the one every criterion compares against — and (b) at $100,
//     shown for information
//   * split per venue: the FIRST run on a venue freezes its holdout boundary in data/mm_lab/holdout_boundaries.json
//     (≥ 4 recorded UTC days → the last ceil(30 %) of days; fewer → the last 30 % of the recorded time span, floored
//     to the hour; hyperliquid_hist: ≤ 2025-11-01). Every later run reads it: in-sample = before it, holdout = from
//     it to the run's --asof. Moving it is a deliberate edit of that committed file (a new judge epoch).
//     The in-sample run never loads an event at or after the boundary.
//     Epoch 2 (2026-10-27, founder): every venue re-frozen at 26 Oct 23:00 UTC after 25 Oct was excluded (entries keep
//     their epoch-1 values; docs/market_making_plan.md §8). Days in data/mm_lab/excluded_days.json (damaged archives)
//     are never loaded, in-sample or holdout; the card names each one inside its window.
//   * PASS (in-sample) = ALL of
//       net bps > 0 · net bps ≥ same-size baseline net bps + 0.5 · ≥ 30 fills · P&L > 0 on ≥ 55 % of coins that
//       traded · P&L > 0 on ≥ 55 % of units (UTC days once the window has ≥ 5 calendar days, else 4-hour blocks) ·
//       t of the per-unit P&L ≥ t_needed(k) with ≥ 5 units · every ±1-step neighbour of every numeric knob keeps
//       net bps > 0 AND does not lose to its own same-size baseline (Δ ≥ 0). Neighbour numbers are never published:
//       a card shows only how many failed, so the grid cannot be used to tune.
//     Multiple testing — alpha spending: the k-th entry of the registry (k counts every in-sample idea AND every
//     holdout look ever made) gets α_k = 0.025 / (k(k+1)), t_needed(k) = z(α_k). Σ α_k = 0.025 over all ideas ever,
//     so mass-testing cannot manufacture a winner; the bar rises with every idea. Re-testing = a new id, counted again.
//
// ── Holdout seal ────────────────────────────────────────────────────────────────────────────────────────────────
//   * --asof may not be later than now − 5 min (a growing file must not be read past what is complete).
//   * --holdout reads ONLY the pinned registry <repo>/data/mm_lab/registry.jsonl (no override), needs the id's
//     in-sample PASS with the same spec hash, allows ONE look per id, and logs the look to the registry (no result
//     in it) — the look counts in k. Results go to the holdout directory only.
//   * Ops (not done by code, needs root): make the registry append-only so no line can be edited or removed:
//       sudo chattr +a <repo>/data/mm_lab/registry.jsonl      (lsattr shows 'a'; undo with chattr -a)

#include "dex/hl_events.hpp"
#include "dex/hl_mm.hpp"
#include "zstd_daily.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace mmlab {

namespace fs = std::filesystem;
using json = nlohmann::json;
constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

// ── pre-registered constants ────────────────────────────────────────────────────────────────────────────────────
constexpr double  kBeatBps        = 0.5;     // must beat the baseline's net bps by this much
constexpr int64_t kMinFills       = 30;
constexpr double  kCoinShare      = 0.55;
constexpr double  kUnitShare      = 0.55;
constexpr int     kMinUnits       = 5;       // day / 4-hour blocks needed for a t-stat
constexpr double  kAlpha          = 0.025;   // one-sided, spent as α/(k(k+1)) over the registry
constexpr int64_t kAsofLagMs      = 5 * 60'000;   // --asof may not be later than now − 5 min
constexpr double  kHoldoutFrac    = 0.30;
constexpr int     kDaySplitMin    = 4;       // recorded days needed for a calendar-day split
constexpr int     kDayUnitMin     = 5;       // tuning calendar days needed to judge consistency by day (else 4 h blocks)
constexpr int64_t kHlStaleMs      = 5'000;
constexpr int64_t kBinanceDelayMs = 135;   // Binance match time → usable here (measured 27 Oct, p50 132 ms)
constexpr int64_t kBasisWindowS   = 300;     // hl_basis trailing median: 1-second samples over 5 minutes …
constexpr int     kBasisMinSamples = 60;     // … at least 60 of them
constexpr int     kRateLimitLevels = 10;     // rate-limited runs replay every coin of a day at once: books cut to 10 levels
constexpr int     kMaxIdeasPerRun  = 40;

// ── small utilities ─────────────────────────────────────────────────────────────────────────────────────────────
inline int64_t day_start_ms(const std::string& day) {      // "YYYY-MM-DD" → UTC midnight, ms
    std::tm tm{};
    tm.tm_year = std::atoi(day.substr(0, 4).c_str()) - 1900;
    tm.tm_mon = std::atoi(day.substr(5, 2).c_str()) - 1;
    tm.tm_mday = std::atoi(day.substr(8, 2).c_str());
    return static_cast<int64_t>(timegm(&tm)) * 1000;
}
inline std::string iso_ms(int64_t ms) {
    std::time_t s = static_cast<std::time_t>(ms / 1000);
    char b[32]; std::strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&s));
    return b;
}
inline std::string day_of(int64_t ms) { return iso_ms(ms).substr(0, 10); }
// "2026-10-26T10:00", "2026-10-26T10:00:00Z", "2026-10-26" or epoch ms → epoch ms; -1 if unreadable.
inline int64_t parse_time(const std::string& s) {
    if (s.empty()) return -1;
    if (std::all_of(s.begin(), s.end(), ::isdigit)) return std::atoll(s.c_str());
    if (s.size() < 10) return -1;
    int64_t t = day_start_ms(s.substr(0, 10));
    if (s.size() >= 16) t += (std::atoi(s.substr(11, 2).c_str()) * 3600 + std::atoi(s.substr(14, 2).c_str()) * 60) * 1000LL;
    if (s.size() >= 19 && s[16] == ':') t += std::atoi(s.substr(17, 2).c_str()) * 1000LL;
    return t;
}
inline std::string hash_hex(const std::string& s) {         // FNV-1a 64
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    char b[17]; std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(h));
    return b;
}
// Upper-tail standard normal quantile: z with P(Z > z) = p (bisection — deterministic, no tables).
inline double z_upper(double p) {
    double lo = 0, hi = 40;
    for (int i = 0; i < 200; ++i) { const double m = (lo + hi) / 2; (0.5 * std::erfc(m / std::sqrt(2.0)) > p ? lo : hi) = m; }
    return (lo + hi) / 2;
}
// Alpha spending: the k-th registered test gets α_k = α / (k(k+1)); Σ_{k≥1} α_k = α.
inline double alpha_k(int64_t k) { const double kk = static_cast<double>(std::max<int64_t>(1, k)); return kAlpha / (kk * (kk + 1)); }
inline double t_needed(int64_t k) { return z_upper(alpha_k(k)); }
// --asof sealing: refuse a cut later than now − 5 min. Returns "" when fine, else the reason.
inline std::string check_asof(int64_t asof_ms, int64_t now_ms) {
    if (asof_ms > now_ms - kAsofLagMs) return "--asof " + iso_ms(asof_ms) + " is later than now − 5 min (" + iso_ms(now_ms - kAsofLagMs) + ") — refused";
    return "";
}

// ── venues ──────────────────────────────────────────────────────────────────────────────────────────────────────
struct Venue {
    std::string name;
    std::vector<fs::path> dirs;           // archive directories (Tardis layout for hyperliquid_hist)
    fs::path params_file;                 // frozen per-coin setting; empty = built-in (hyperliquid_hist)
    double maker_bps = 0, taker_bps = 0;
    int64_t place_ms = 0, cancel_ms = 0;
    double min_order_usd = 10;            // smallest quote the venue accepts (×1.05 safety margin where we know it)
    int rate_per_min = 0;                 // the venue's standard account limit (0 = not modelled)
    bool cross = false;                   // Hyperliquid live archive can serve cross-venue features
    bool ext = false;                     // Extended live archive can serve ext_* features (every venue but Extended)
    bool bn = false;                      // Binance futures reference (history or live archive) serves bn_* features
    std::string fixed_holdout_from;       // days ≥ this are always holdout ("" = none)
};
struct Env {
    fs::path root = "/media/theone/381C48C51C48803C/rithmic_dex";
    fs::path params_dir = "data/mm_gates";
    fs::path binance_hist = "/media/theone/381C48C51C48803C/rithmic_archive/history/binance";   // <SYM>USDT/<SYM>USDT-aggTrades-<day>.zip
    // venue → UTC day → reason: days whose archive is known to be damaged (data/mm_lab/excluded_days.json, committed).
    // Excluded days are never loaded, in-sample or holdout; the frozen split is not affected (it only fixes a time).
    std::map<std::string, std::map<std::string, std::string>> excluded_days;
    bool excluded(const std::string& venue, const std::string& day) const {
        const auto v = excluded_days.find(venue);
        return v != excluded_days.end() && v->second.count(day);
    }
};
// {"<venue>": {"YYYY-MM-DD": "reason", …}, …}; a missing file = nothing excluded; a malformed one throws.
inline std::map<std::string, std::map<std::string, std::string>> load_excluded_days(const fs::path& f) {
    std::map<std::string, std::map<std::string, std::string>> out;
    std::error_code ec;
    if (!fs::exists(f, ec)) return out;
    std::ifstream in(f);
    const json j = json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object()) throw std::runtime_error("cannot read excluded days " + f.string());
    static const std::regex day_re("^\\d{4}-\\d{2}-\\d{2}$");
    for (const auto& [venue, days] : j.items()) {
        if (!days.is_object()) throw std::runtime_error("excluded days: \"" + venue + "\" must map days to reasons");
        for (const auto& [day, why] : days.items()) {
            if (!std::regex_match(day, day_re) || !why.is_string() || why.get<std::string>().empty())
                throw std::runtime_error("excluded days: " + venue + "/" + day + " needs a YYYY-MM-DD day and a non-empty reason");
            out[venue][day] = why.get<std::string>();
        }
    }
    return out;
}
inline const std::vector<std::string>& venue_names() {
    static const std::vector<std::string> v = {"lighter", "extended", "aster", "paradex", "hyperliquid_hist"};
    return v;
}
// Minimum orders: Lighter min_quote_amount $10 × the gateway's 1.05 margin (src/dex/live/market_spec.hpp);
// Hyperliquid $10 (exchange rule); Extended $10, Aster $5, Paradex $10 are ASSUMED (not in the archives) — stated
// on every card that rounds a size up.
inline std::optional<Venue> venue_of(const std::string& name, const Env& env) {
    Venue v; v.name = name;
    if (name == "lighter")       { v.dirs = {env.root / "lighter"};  v.maker_bps = 0;   v.taker_bps = 0;   v.place_ms = 145; v.cancel_ms = 445; v.min_order_usd = 10.5; v.rate_per_min = 60; v.cross = true; v.ext = true; v.bn = true; }
    else if (name == "extended") { v.dirs = {env.root / "extended"}; v.maker_bps = 0;   v.taker_bps = 2.5; v.place_ms = 100; v.cancel_ms = 100; v.min_order_usd = 10;   v.cross = true; v.bn = true; }
    else if (name == "aster")    { v.dirs = {env.root / "aster"};    v.maker_bps = 0;   v.taker_bps = 3.5; v.place_ms = 100; v.cancel_ms = 100; v.min_order_usd = 5;    v.cross = true; v.ext = true; v.bn = true; }
    else if (name == "paradex")  { v.dirs = {env.root / "paradex"};  v.maker_bps = 0.3; v.taker_bps = 4.5; v.place_ms = 100; v.cancel_ms = 100; v.min_order_usd = 10;   v.cross = true; v.ext = true; v.bn = true; }
    else if (name == "hyperliquid_hist") { v.dirs = {env.root / "tardis", env.root / "tardis_extra"}; v.maker_bps = 1.5; v.taker_bps = 4.5;
                                           v.place_ms = 200; v.cancel_ms = 200; v.min_order_usd = 10; v.fixed_holdout_from = "2025-11-01"; }
    else return std::nullopt;
    if (name != "hyperliquid_hist") v.params_file = env.params_dir / ("params_" + name + ".json");
    return v;
}

// Frozen setting of one coin (src/dex/mm_paper.hpp frozen_params; per-coin ticks from the params file). eval = apply
// the pre-registered evaluation (venue fees + latency, NoCross). eval=false reproduces mm_sim --params exactly.
inline std::optional<hl::Params> frozen_params(const Venue& v, const json& params_file, const std::string& coin, bool eval) {
    hl::Params q;
    q.cancels = hl::CancelModel::Pessimistic;
    q.fills = hl::FillModel::NoCross;
    if (v.params_file.empty()) {          // hyperliquid_hist: the backtests' frozen setting on Hyperliquid's own grid
        q.order_usd = 100; q.max_inv_units = 3; q.maker_fee_bps = 1.5; q.taker_fee_bps = 4.5; q.latency_ms = 200;
        q.half_width_ticks = 5; q.min_spread_ticks = 4; q.imb_pull = 0.6; q.pause_move_ticks = 2; q.pause_ms = 2000; q.requote_ticks = 1;
        q.strat_tick_rule = hl::TickRule::Book;
    } else {
        if (!params_file.contains("coins") || !params_file["coins"].contains(coin)) return std::nullopt;
        const auto& j = params_file;
        q.maker_fee_bps = j.value("maker_bps", 1.5);
        q.taker_fee_bps = j.value("taker_bps", 4.5);
        q.latency_ms = j.value("latency_ms", int64_t{200});
        // Judge-owned, NOT read from the params file: mm_gates.sh rewrote params_lighter.json at $11 on 2026-10-26 (the
        // paper setting), and following it would silently move every later baseline and every idea without size_usd.
        // The registry stays comparable only with one fixed default; ideas choose their size with size_usd.
        q.order_usd = 100.0;
        q.max_inv_units = j.value("max_inv", 3);
        const auto& cj = j["coins"][coin];
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
    }
    if (eval) {
        q.maker_fee_bps = v.maker_bps;
        q.taker_fee_bps = v.taker_bps;
        q.place_latency_ms = v.place_ms;
        q.cancel_latency_ms = v.cancel_ms;
        q.latency_ms = v.place_ms;
    }
    return q;
}
inline double strat_tick(const hl::Params& p, const hl::Book& b) {    // mirrors hl::Simulator::strat_tick
    if (p.strat_tick_rule == hl::TickRule::Book) return p.venue_tick > 0 ? p.venue_tick : b.tick();
    return std::max(std::pow(10.0, std::floor(std::log10(b.mid())) - 4), std::pow(10.0, -(6 - p.hl_sz_decimals)));
}

// ── feature library ─────────────────────────────────────────────────────────────────────────────────────────────
constexpr int kOwn = 0, kRefHl = 1, kRefExt = 2, kRefBn = 3;
struct FeatureDef {
    const char* name;
    int64_t default_window;   // 0 = not windowed
    int ref;                  // reference venue it needs (receive-time aligned): kOwn, kRefHl (Hyperliquid), kRefExt (Extended)
    bool state;               // read from the simulator's own state at the decision
    double step;              // robustness ±1 step of a threshold on this feature
    const char* desc;
};
inline const std::vector<FeatureDef>& features() {
    static const std::vector<FeatureDef> f = {
        {"spread_bps", 0, kOwn, false, 0.5, "best ask − best bid, bps of mid (current book)"},
        {"spread_ticks", 0, kOwn, false, 1, "best ask − best bid in strategy ticks"},
        {"book_imbalance", 0, kOwn, false, 0.1, "(bid size − ask size)/(sum), best level"},
        {"book_imbalance5", 0, kOwn, false, 0.1, "same over the top 5 levels per side"},
        {"trade_flow_imbalance", 5'000, kOwn, false, 0.1, "(buy-aggressor $ − sell-aggressor $)/(total) over the last W ms; 0 when no trade"},
        {"trades_per_min", 60'000, kOwn, false, 5, "trades in the last W ms, per minute"},
        {"realized_vol_bps", 60'000, kOwn, false, 1, "sqrt(sum of squared book-to-book mid log returns) over W ms, bps"},
        {"mid_move_bps", 1'000, kOwn, false, 1, "mid now vs the last mid at or before now − W, bps"},
        {"hl_basis_bps", 0, kRefHl, false, 1, "(our mid − Hyperliquid mid)/HL mid in bps, minus its own trailing 5-min median (1-s samples)"},
        {"hl_basis_ticks", 0, kRefHl, false, 1, "hl_basis_bps expressed in strategy ticks (our mid − HL-implied price)"},
        {"cross_venue_lead_bps", 1'000, kRefHl, false, 1, "Hyperliquid mid move over the last W ms of receive time, bps"},
        {"ext_basis_bps", 0, kRefExt, false, 1, "(our mid − Extended mid)/Extended mid in bps, minus its own trailing 5-min median (1-s samples)"},
        {"ext_basis_ticks", 0, kRefExt, false, 1, "ext_basis_bps expressed in strategy ticks (our mid − Extended-implied price)"},
        {"ext_lead_bps", 1'000, kRefExt, false, 1, "Extended mid move over the last W ms of receive time, bps"},
        {"bn_basis_bps", 0, kRefBn, false, 1, "(our mid − Binance price)/Binance price in bps, minus its own trailing 5-min median (1-s samples)"},
        {"bn_basis_ticks", 0, kRefBn, false, 1, "bn_basis_bps expressed in strategy ticks (our mid − Binance-implied price)"},
        {"bn_lead_bps", 1'000, kRefBn, false, 1, "Binance price move over the last W ms of usable time, bps"},
        {"time_of_day_utc", 0, kOwn, false, 1, "hours since 00:00 UTC (fractional)"},
        {"weekday", 0, kOwn, false, 1, "UTC weekday, 0 = Monday … 6 = Sunday"},
        {"inventory_usd", 0, kOwn, true, 25, "signed position × current mid, $"},
        {"minutes_since_last_fill", 0, kOwn, true, 1, "minutes since our last fill (since the segment start before the first)"},
        {"funding", 0, kOwn, false, 0, "UNAVAILABLE: funding is not recorded in any archive — refused"},
    };
    return f;
}
inline const FeatureDef* feature(const std::string& n) {
    for (const auto& f : features()) if (n == f.name) return &f;
    return nullptr;
}

// ── idea spec ───────────────────────────────────────────────────────────────────────────────────────────────────
struct Cond {
    std::string feat;          // feature name
    int64_t window = 0;        // ms (windowed features)
    std::string key;           // "name" or "name@W" — the frame column
    std::string op;
    int opc = 0;               // index of op in {<, <=, >, >=, ==, !=, abs<, abs>}
    double value = 0;
    std::string ref;           // param name when the value references one (value = ref_mult × param)
    double ref_mult = 1;
};
enum class Act { PullBid, PullAsk, PullBoth, Skew, Widen };
struct Rule { std::vector<Cond> when; Act act = Act::PullBoth; double value = 0; int64_t hold_ms = 0; };
enum class FairType { OwnMid, HlAnchor, ExtAnchor, BnAnchor, Blend };
struct FairSpec { FairType type = FairType::OwnMid; double k = 1; double w_mid = 1, w_micro = 0, w_hl = 0, w_ext = 0, w_bn = 0; };
struct Idea {
    std::string id, venue;
    std::vector<std::string> markets;      // empty = all
    json params = json::object();          // overrides
    double size_usd = 0;                   // 0 = frozen
    FairSpec fair;
    std::vector<Rule> rules;
    int rate_per_min = 0, rate_accounts = 0;
    bool modify = false;                   // execution.modify
    bool keep_fills = false;               // internal (fill_study): CoinRun keeps every resting fill; never parsed
    int64_t linger_ms = 0;                 // execution.batch_linger_ms
    int64_t place_ms = -1, cancel_ms = -1;
    json spec;                             // the idea as given (id included)
    std::string hash;                      // of the spec without id / why / notes
    bool needs_hl() const {
        if (fair.type == FairType::HlAnchor || (fair.type == FairType::Blend && fair.w_hl > 0)) return true;
        for (const auto& r : rules) for (const auto& c : r.when) if (feature(c.feat)->ref == kRefHl) return true;
        return false;
    }
    bool needs_ext() const {
        if (fair.type == FairType::ExtAnchor || (fair.type == FairType::Blend && fair.w_ext > 0)) return true;
        for (const auto& r : rules) for (const auto& c : r.when) if (feature(c.feat)->ref == kRefExt) return true;
        return false;
    }
    bool needs_bn() const {
        if (fair.type == FairType::BnAnchor || (fair.type == FairType::Blend && fair.w_bn > 0)) return true;
        for (const auto& r : rules) for (const auto& c : r.when) if (feature(c.feat)->ref == kRefBn) return true;
        return false;
    }
    bool plain() const { return rules.empty() && fair.type == FairType::OwnMid; }
    std::set<std::string> frame_keys() const {
        std::set<std::string> k;
        for (const auto& r : rules) for (const auto& c : r.when) if (!feature(c.feat)->state) k.insert(c.key);
        if (fair.type == FairType::HlAnchor || (fair.type == FairType::Blend && fair.w_hl > 0)) k.insert("hl_basis_bps");
        if (fair.type == FairType::ExtAnchor || (fair.type == FairType::Blend && fair.w_ext > 0)) k.insert("ext_basis_bps");
        if (fair.type == FairType::BnAnchor || (fair.type == FairType::Blend && fair.w_bn > 0)) k.insert("bn_basis_bps");
        return k;
    }
};

inline const std::set<std::string>& param_fields() {
    static const std::set<std::string> s = {"max_inv_units", "half_width_ticks", "skew_ticks", "fair", "min_spread_ticks",
                                            "imb_pull", "pause_move_ticks", "pause_ms", "requote_ticks"};
    return s;
}
inline const std::set<std::string>& fixed_fields() {
    static const std::set<std::string> s = {"order_usd", "maker_fee_bps", "taker_fee_bps", "latency_ms", "place_latency_ms",
                                            "cancel_latency_ms", "cancels", "fills", "venue_tick", "strat_tick_rule",
                                            "tick_rule", "hl_sz_decimals", "track_markouts"};
    return s;
}
inline const std::set<std::string>& ref_params() {
    static const std::set<std::string> s = {"half_width_ticks", "skew_ticks", "min_spread_ticks", "requote_ticks",
                                            "pause_move_ticks", "imb_pull", "max_inv_units"};
    return s;
}
inline double param_value(const hl::Params& p, const std::string& n) {
    if (n == "half_width_ticks") return p.half_width_ticks;
    if (n == "skew_ticks") return p.skew_ticks;
    if (n == "min_spread_ticks") return p.min_spread_ticks;
    if (n == "requote_ticks") return p.requote_ticks;
    if (n == "pause_move_ticks") return p.pause_move_ticks;
    if (n == "imb_pull") return p.imb_pull;
    if (n == "max_inv_units") return p.max_inv_units;
    return NaN;
}
inline void apply_params(hl::Params& p, const json& o) {
    for (auto& [k, v] : o.items()) {
        if (k == "max_inv_units") p.max_inv_units = v.get<int>();
        else if (k == "half_width_ticks") p.half_width_ticks = v.get<double>();
        else if (k == "skew_ticks") p.skew_ticks = v.get<double>();
        else if (k == "fair") p.fair = v.get<std::string>() == "micro" ? hl::Fair::Micro : hl::Fair::Mid;
        else if (k == "min_spread_ticks") p.min_spread_ticks = v.get<double>();
        else if (k == "imb_pull") p.imb_pull = v.get<double>();
        else if (k == "pause_move_ticks") p.pause_move_ticks = v.get<double>();
        else if (k == "pause_ms") p.pause_ms = v.get<int64_t>();
        else if (k == "requote_ticks") p.requote_ticks = v.get<double>();
    }
}

// Parse + validate one idea. Returns nullopt and sets err on anything unknown or not allowed.
inline std::optional<Idea> parse_idea(const json& j, const Env& env, std::string& err) {
    auto fail = [&](const std::string& m) { err = m; return std::optional<Idea>{}; };
    if (!j.is_object()) return fail("idea is not a JSON object");
    static const std::set<std::string> top = {"id", "venue", "markets", "params", "rules", "fair", "size_usd", "rate_limit",
                                              "latency", "execution", "why", "notes"};
    for (auto& [k, _] : j.items()) if (!top.count(k)) return fail("unknown field '" + k + "'");
    Idea I;
    I.spec = j;
    I.id = j.value("id", "");
    if (!std::regex_match(I.id, std::regex("[a-z0-9_]{3,64}"))) return fail("id must match [a-z0-9_]{3,64}");
    I.venue = j.value("venue", "");
    const auto V = venue_of(I.venue, env);
    if (!V) return fail("unknown venue '" + I.venue + "' (lighter|extended|aster|paradex|hyperliquid_hist)");
    if (j.contains("markets")) {
        const auto& m = j["markets"];
        if (m.is_string()) {
            if (m.get<std::string>() != "all") return fail("markets must be a list of coins or \"all\"");
        } else if (m.is_array()) {
            for (const auto& c : m) {
                if (!c.is_string() || c.get<std::string>().empty()) return fail("markets: every entry must be a coin name");
                I.markets.push_back(c.get<std::string>());
            }
            if (I.markets.empty()) return fail("markets: empty list");
            std::sort(I.markets.begin(), I.markets.end());
        } else return fail("markets must be a list of coins or \"all\"");
    }
    if (j.contains("params")) {
        if (!j["params"].is_object()) return fail("params must be an object");
        for (auto& [k, v] : j["params"].items()) {
            if (fixed_fields().count(k)) return fail("params." + k + " is fixed by the evaluation (fees, latency, fill/queue model, ticks, size → size_usd) — refused");
            if (!param_fields().count(k)) return fail("unknown params field '" + k + "'");
            if (k == "fair") { if (!v.is_string() || (v != "mid" && v != "micro")) return fail("params.fair must be \"mid\" or \"micro\""); continue; }
            if (!v.is_number()) return fail("params." + k + " must be a number");
            const double x = v.get<double>();
            if (!std::isfinite(x) || x < 0) return fail("params." + k + " must be ≥ 0");
            if ((k == "max_inv_units" || k == "pause_ms") && (x != std::floor(x))) return fail("params." + k + " must be an integer");
            if (k == "max_inv_units" && x < 1) return fail("params.max_inv_units must be ≥ 1");
            if (k == "imb_pull" && x > 1) return fail("params.imb_pull must be ≤ 1");
        }
        I.params = j["params"];
    }
    if (j.contains("size_usd")) {
        if (!j["size_usd"].is_number() || !(j["size_usd"].get<double>() > 0)) return fail("size_usd must be a positive number");
        I.size_usd = j["size_usd"].get<double>();
        if (I.size_usd > 100'000) return fail("size_usd above $100,000");
    }
    if (j.contains("fair")) {
        const auto& f = j["fair"];
        if (!f.is_object()) return fail("fair must be an object");
        for (auto& [k, _] : f.items()) if (k != "type" && k != "k" && k != "weights") return fail("unknown fair field '" + k + "'");
        const std::string t = f.value("type", "own_mid");
        if (t == "own_mid") I.fair.type = FairType::OwnMid;
        else if (t == "hl_anchor" || t == "ext_anchor" || t == "bn_anchor") {
            I.fair.type = t == "hl_anchor" ? FairType::HlAnchor : t == "ext_anchor" ? FairType::ExtAnchor : FairType::BnAnchor;
            if (f.contains("k")) { if (!f["k"].is_number()) return fail("fair.k must be a number"); I.fair.k = f["k"].get<double>(); }
            if (!(I.fair.k >= 0 && I.fair.k <= 2)) return fail("fair.k must be in [0, 2]");
        } else if (t == "blend") {
            I.fair.type = FairType::Blend;
            if (!f.contains("weights") || !f["weights"].is_object()) return fail("fair.weights must be an object {mid, micro, hl, ext, bn}");
            I.fair.w_mid = I.fair.w_micro = I.fair.w_hl = I.fair.w_ext = I.fair.w_bn = 0;
            for (auto& [k, v] : f["weights"].items()) {
                if (!v.is_number() || v.get<double>() < 0) return fail("fair.weights." + k + " must be a number ≥ 0");
                if (k == "mid") I.fair.w_mid = v.get<double>(); else if (k == "micro") I.fair.w_micro = v.get<double>();
                else if (k == "hl") I.fair.w_hl = v.get<double>(); else if (k == "ext") I.fair.w_ext = v.get<double>();
                else if (k == "bn") I.fair.w_bn = v.get<double>();
                else return fail("unknown fair weight '" + k + "' (mid|micro|hl|ext|bn)");
            }
            if (I.fair.w_mid + I.fair.w_micro + I.fair.w_hl + I.fair.w_ext + I.fair.w_bn <= 0) return fail("fair.weights must not all be 0");
            if (I.params.contains("fair")) return fail("fair.type blend sets the fair price itself: drop params.fair");
        } else return fail("unknown fair.type '" + t + "' (own_mid|hl_anchor|ext_anchor|bn_anchor|blend)");
    }
    if (j.contains("rules")) {
        if (!j["rules"].is_array()) return fail("rules must be a list");
        if (j["rules"].size() > 12) return fail("at most 12 rules");
        for (size_t ri = 0; ri < j["rules"].size(); ++ri) {
            const auto& rj = j["rules"][ri];
            const std::string at = "rules[" + std::to_string(ri) + "]";
            if (!rj.is_object()) return fail(at + " must be an object");
            for (auto& [k, _] : rj.items()) if (k != "when" && k != "action" && k != "value" && k != "hold_ms") return fail(at + ": unknown field '" + k + "'");
            Rule R;
            const std::string a = rj.value("action", "");
            if (a == "pull_bid") R.act = Act::PullBid; else if (a == "pull_ask") R.act = Act::PullAsk;
            else if (a == "pull_both") R.act = Act::PullBoth; else if (a == "skew_ticks") R.act = Act::Skew;
            else if (a == "widen_ticks") R.act = Act::Widen;
            else return fail(at + ": unknown action '" + a + "' (pull_bid|pull_ask|pull_both|skew_ticks|widen_ticks)");
            if (R.act == Act::Skew || R.act == Act::Widen) {
                if (!rj.contains("value") || !rj["value"].is_number()) return fail(at + ": " + a + " needs a numeric \"value\" (ticks)");
                R.value = rj["value"].get<double>();
                if (std::abs(R.value) > 50) return fail(at + ": value beyond ±50 ticks");
            } else if (rj.contains("value")) return fail(at + ": " + a + " takes no value");
            if (rj.contains("hold_ms")) {
                if (!rj["hold_ms"].is_number_integer() || rj["hold_ms"].get<int64_t>() < 0 || rj["hold_ms"].get<int64_t>() > 3'600'000) return fail(at + ": hold_ms must be an integer 0 .. 3600000");
                R.hold_ms = rj["hold_ms"].get<int64_t>();
            }
            if (!rj.contains("when") || !rj["when"].is_array() || rj["when"].empty()) return fail(at + ": \"when\" must be a non-empty list of [feature, op, value]");
            for (const auto& cj : rj["when"]) {
                if (!cj.is_array() || cj.size() != 3 || !cj[0].is_string() || !cj[1].is_string()) return fail(at + ": each condition is [feature, op, value]");
                Cond C;
                std::string f = cj[0].get<std::string>();
                const auto at_pos = f.find('@');
                C.feat = f.substr(0, at_pos);
                const FeatureDef* fd = feature(C.feat);
                if (!fd) return fail(at + ": unknown feature '" + C.feat + "'");
                if (C.feat == "funding") return fail(at + ": feature 'funding' is unavailable (not recorded in any archive)");
                if (fd->ref == kRefHl && !V->cross) return fail(at + ": feature '" + C.feat + "' needs Hyperliquid's live archive — unavailable on " + I.venue);
                if (fd->ref == kRefExt && !V->ext) return fail(at + ": feature '" + C.feat + "' needs Extended's live archive as a second venue — unavailable on " + I.venue);
                if (fd->ref == kRefBn && !V->bn) return fail(at + ": feature '" + C.feat + "' needs the Binance reference — unavailable on " + I.venue);
                if (at_pos != std::string::npos) {
                    if (!fd->default_window) return fail(at + ": feature '" + C.feat + "' takes no @window");
                    const std::string w = f.substr(at_pos + 1);
                    if (w.empty() || !std::all_of(w.begin(), w.end(), ::isdigit)) return fail(at + ": window must be integer ms in '" + f + "'");
                    C.window = std::atoll(w.c_str());
                    if (C.window < 100 || C.window > 3'600'000) return fail(at + ": window must be 100 .. 3600000 ms");
                } else C.window = fd->default_window;
                C.key = fd->default_window ? C.feat + "@" + std::to_string(C.window) : C.feat;
                C.op = cj[1].get<std::string>();
                static const std::vector<std::string> ops = {"<", "<=", ">", ">=", "==", "!=", "abs<", "abs>"};
                const auto oit = std::find(ops.begin(), ops.end(), C.op);
                if (oit == ops.end()) return fail(at + ": unknown op '" + C.op + "'");
                C.opc = static_cast<int>(oit - ops.begin());
                if (cj[2].is_number()) { C.value = cj[2].get<double>(); if (!std::isfinite(C.value)) return fail(at + ": value must be finite"); }
                else if (cj[2].is_string()) {
                    static const std::regex re(R"(^(-)?(?:([0-9]*\.?[0-9]+)\*)?([a-z_]+)$)");
                    std::smatch m; const std::string s = cj[2].get<std::string>();
                    if (!std::regex_match(s, m, re) || !ref_params().count(m[3].str())) return fail(at + ": value '" + s + "' is neither a number nor a parameter reference");
                    C.ref = m[3].str();
                    C.ref_mult = (m[1].matched ? -1.0 : 1.0) * (m[2].matched ? std::atof(m[2].str().c_str()) : 1.0);
                } else return fail(at + ": value must be a number or a parameter reference");
                R.when.push_back(C);
            }
            I.rules.push_back(R);
        }
    }
    if (j.contains("rate_limit")) {
        const auto& r = j["rate_limit"];
        if (!r.is_object()) return fail("rate_limit must be an object {per_min, accounts}");
        for (auto& [k, _] : r.items()) if (k != "per_min" && k != "accounts") return fail("unknown rate_limit field '" + k + "'");
        if (!V->rate_per_min) return fail("rate_limit: not modelled on " + I.venue);
        I.rate_per_min = r.value("per_min", V->rate_per_min);
        I.rate_accounts = r.value("accounts", 1);
        if (I.rate_per_min < 1 || I.rate_per_min > V->rate_per_min) return fail("rate_limit.per_min must be 1 .. " + std::to_string(V->rate_per_min) + " (the venue's standard)");
        if (I.rate_accounts < 1 || I.rate_accounts > 10) return fail("rate_limit.accounts must be 1 .. 10");
    } else if (V->rate_per_min > 0) {          // always on: the venue's standard, one account
        I.rate_per_min = V->rate_per_min;
        I.rate_accounts = 1;
    }
    if (j.contains("execution")) {
        const auto& x = j["execution"];
        if (!x.is_object()) return fail("execution must be an object {modify, batch_linger_ms}");
        for (auto& [k, v] : x.items()) {
            if (k == "modify") { if (!v.is_boolean()) return fail("execution.modify must be true/false"); I.modify = v.get<bool>(); }
            else if (k == "batch_linger_ms") {
                if (!v.is_number_integer() || v.get<int64_t>() < 0 || v.get<int64_t>() > 2000) return fail("execution.batch_linger_ms must be an integer 0 .. 2000");
                if (!V->rate_per_min) return fail("execution.batch_linger_ms: no rate limit is modelled on " + I.venue);
                I.linger_ms = v.get<int64_t>();
            } else return fail("unknown execution field '" + k + "'");
        }
    }
    if (j.contains("latency")) {
        const auto& l = j["latency"];
        if (!l.is_object()) return fail("latency must be an object {place_ms, cancel_ms}");
        for (auto& [k, _] : l.items()) if (k != "place_ms" && k != "cancel_ms") return fail("unknown latency field '" + k + "'");
        I.place_ms = l.value("place_ms", V->place_ms);
        I.cancel_ms = l.value("cancel_ms", V->cancel_ms);
        if (I.place_ms < V->place_ms || I.cancel_ms < V->cancel_ms)
            return fail("latency faster than the venue's standard (" + std::to_string(V->place_ms) + "/" + std::to_string(V->cancel_ms) + " ms) — refused");
        if (I.place_ms > 60'000 || I.cancel_ms > 60'000) return fail("latency above 60 s");
    }
    if ((I.fair.type == FairType::HlAnchor || (I.fair.type == FairType::Blend && I.fair.w_hl > 0)) && !V->cross)
        return fail("fair." + std::string(I.fair.type == FairType::HlAnchor ? "hl_anchor" : "blend weight hl") + " needs Hyperliquid's live archive — unavailable on " + I.venue);
    if ((I.fair.type == FairType::BnAnchor || (I.fair.type == FairType::Blend && I.fair.w_bn > 0)) && !V->bn)
        return fail("fair." + std::string(I.fair.type == FairType::BnAnchor ? "bn_anchor" : "blend weight bn") + " needs the Binance reference — unavailable on " + I.venue);
    if ((I.fair.type == FairType::ExtAnchor || (I.fair.type == FairType::Blend && I.fair.w_ext > 0)) && !V->ext)
        return fail("fair." + std::string(I.fair.type == FairType::ExtAnchor ? "ext_anchor" : "blend weight ext") + " needs Extended's live archive as a second venue — unavailable on " + I.venue);
    json h = j; h.erase("id"); h.erase("why"); h.erase("notes");
    I.hash = hash_hex(h.dump());
    return I;
}

// Robustness neighbours: each numeric knob the idea SETS, moved one pre-registered step down and up (a knob the idea
// inherits from the frozen setting is not the idea's). Returns (label, spec) pairs; never registered.
inline double param_step(const std::string& k) {
    if (k == "max_inv_units" || k == "half_width_ticks" || k == "min_spread_ticks" || k == "pause_move_ticks" || k == "requote_ticks") return 1;
    if (k == "skew_ticks") return 0.5;
    if (k == "imb_pull") return 0.1;
    if (k == "pause_ms") return 1000;
    return 0;
}
inline std::vector<std::pair<std::string, json>> neighbours(const Idea& I, const Venue& V) {
    std::vector<std::pair<std::string, json>> out;
    std::set<std::string> seen = {I.spec.dump()};
    auto add = [&](const std::string& label, json s) { if (seen.insert(s.dump()).second) out.push_back({label, std::move(s)}); };
    char b[64];
    auto num = [&](double x) { std::snprintf(b, sizeof b, "%g", x); return std::string(b); };
    for (auto& [k, v] : I.params.items()) {
        if (!v.is_number()) continue;
        const double st = param_step(k), x = v.get<double>();
        for (int d : {-1, 1}) {
            double y = x + d * st;
            if (k == "max_inv_units") y = std::max(1.0, y);
            if (k == "half_width_ticks") y = std::max(0.5, y);
            if (k == "imb_pull") y = std::min(1.0, std::max(0.05, y));
            y = std::max(0.0, y);
            json s = I.spec;
            if (k == "max_inv_units" || k == "pause_ms") s["params"][k] = static_cast<int64_t>(std::llround(y)); else s["params"][k] = y;
            add("params." + k + "=" + num(y), s);
        }
    }
    if (I.size_usd > 0)
        for (double f : {0.8, 1.25}) { json s = I.spec; s["size_usd"] = std::max(V.min_order_usd, I.size_usd * f); add("size_usd=" + num(s["size_usd"].get<double>()), s); }
    if (I.fair.type == FairType::HlAnchor || I.fair.type == FairType::ExtAnchor || I.fair.type == FairType::BnAnchor)
        for (int d : {-1, 1}) { json s = I.spec; const double y = std::min(2.0, std::max(0.0, I.fair.k + 0.25 * d)); s["fair"]["k"] = y; add("fair.k=" + num(y), s); }
    if (I.fair.type == FairType::Blend)
        for (const char* w : {"mid", "micro", "hl", "ext", "bn"})
            for (int d : {-1, 1}) {
                // "ext"/"bn" are varied only when the spec uses them, so blends judged before they existed keep their neighbours
                if ((std::string(w) == "ext" || std::string(w) == "bn") && !I.spec["fair"]["weights"].contains(w)) continue;
                json s = I.spec;
                const double x = s["fair"]["weights"].value(w, 0.0), y = std::max(0.0, x + 0.25 * d);
                s["fair"]["weights"][w] = y;
                double sum = 0; for (auto& [_, v] : s["fair"]["weights"].items()) sum += v.get<double>();
                if (sum > 0) add(std::string("fair.weights.") + w + "=" + num(y), s);
            }
    for (size_t ri = 0; ri < I.rules.size(); ++ri) {
        const Rule& R = I.rules[ri];
        const std::string at = "rules[" + std::to_string(ri) + "]";
        for (size_t ci = 0; ci < R.when.size(); ++ci) {
            const Cond& C = R.when[ci];
            const FeatureDef* fd = feature(C.feat);
            if (C.ref.empty())
                for (int d : {-1, 1}) { json s = I.spec; const double y = C.value + d * fd->step; s["rules"][ri]["when"][ci][2] = y; add(at + ".when[" + std::to_string(ci) + "]." + C.feat + "=" + num(y), s); }
            else   // a parameter reference: its multiplier is the knob, ±0.25
                for (int d : {-1, 1}) {
                    const double m = C.ref_mult + 0.25 * d;
                    if (std::abs(m) < 1e-12) continue;
                    json s = I.spec;
                    const std::string v = (m < 0 ? "-" : "") + num(std::abs(m)) + "*" + C.ref;
                    s["rules"][ri]["when"][ci][2] = v;
                    add(at + ".when[" + std::to_string(ci) + "]." + C.feat + "=" + v, s);
                }
            if (fd->default_window)
                for (double f : {0.5, 2.0}) {
                    json s = I.spec;
                    const int64_t w = std::min<int64_t>(3'600'000, std::max<int64_t>(100, std::llround(C.window * f)));
                    s["rules"][ri]["when"][ci][0] = C.feat + "@" + std::to_string(w);
                    add(at + ".when[" + std::to_string(ci) + "]." + C.feat + "@" + std::to_string(w), s);
                }
        }
        if (R.act == Act::Skew || R.act == Act::Widen)
            for (int d : {-1, 1}) { json s = I.spec; const double y = R.value + 0.5 * d; s["rules"][ri]["value"] = y; add(at + ".value=" + num(y), s); }
        if (R.hold_ms > 0)
            for (double f : {0.5, 2.0}) { json s = I.spec; const int64_t y = std::llround(R.hold_ms * f); s["rules"][ri]["hold_ms"] = y; add(at + ".hold_ms=" + std::to_string(y), s); }
    }
    return out;
}

// ── compact day data ────────────────────────────────────────────────────────────────────────────────────────────
struct CE { int64_t t, r; double tpx, tsz; uint32_t off; uint8_t nb, na; bool is_book, buy; };
struct DayData {
    std::string coin, day;
    std::vector<CE> ev;
    std::vector<hl::Level> pool;
    int64_t t0 = 0;                       // first event time (feature warm-up starts here)
};
inline DayData compact(const std::vector<hl::Event>& ev, int max_levels, int64_t w0, int64_t w1) {
    DayData d;
    d.ev.reserve(ev.size());
    for (const auto& e : ev) {
        if (e.t_ms < w0 || e.t_ms >= w1) continue;
        CE c{};
        c.t = e.t_ms; c.r = e.r_ms; c.is_book = e.is_book;
        if (e.is_book) {
            if (!e.book.valid()) continue;
            c.nb = static_cast<uint8_t>(std::min<size_t>(max_levels, e.book.bids.size()));
            c.na = static_cast<uint8_t>(std::min<size_t>(max_levels, e.book.asks.size()));
            c.off = static_cast<uint32_t>(d.pool.size());
            d.pool.insert(d.pool.end(), e.book.bids.begin(), e.book.bids.begin() + c.nb);
            d.pool.insert(d.pool.end(), e.book.asks.begin(), e.book.asks.begin() + c.na);
        } else { c.tpx = e.trade.px; c.tsz = e.trade.sz; c.buy = e.trade.buy_aggr; }
        d.ev.push_back(c);
    }
    if (!d.ev.empty()) d.t0 = d.ev.front().t;
    return d;
}
inline void book_of(const DayData& d, const CE& c, hl::Book& b) {
    b.t_ms = c.t;
    b.bids.assign(d.pool.begin() + c.off, d.pool.begin() + c.off + c.nb);
    b.asks.assign(d.pool.begin() + c.off + c.nb, d.pool.begin() + c.off + c.nb + c.na);
}
struct HlMid { int64_t r; double mid; };
inline std::vector<HlMid> hl_mids_from(const std::vector<hl::Event>& ev) {
    std::vector<HlMid> m;
    for (const auto& e : ev) if (e.is_book && e.book.valid() && e.r_ms > 0) m.push_back({e.r_ms, e.book.mid()});
    std::stable_sort(m.begin(), m.end(), [](const HlMid& a, const HlMid& b) { return a.r < b.r; });
    return m;
}

// Binance reference for one coin-day (see the header): the published trade history if present, else the live archive.
inline fs::path binance_hist_file(const Env& env, const std::string& coin, const std::string& day) {
    const std::string sym = coin + "USDT";
    return env.binance_hist / sym / (sym + "-aggTrades-" + day + ".zip");
}
inline bool binance_listed(const Env& env, const std::string& coin) {
    std::error_code ec;
    return fs::is_directory(env.binance_hist / (coin + "USDT"), ec) || fs::is_directory(env.root / "binance" / coin, ec);
}
inline std::vector<HlMid> binance_mids(const Env& env, const std::string& coin, const std::string& day, bool enabled) {
    std::vector<HlMid> m;
    if (!enabled) return m;
    std::error_code ec;
    if (const fs::path z = binance_hist_file(env, coin, day); fs::exists(z, ec)) {
        FILE* p = popen(("unzip -p '" + z.string() + "'").c_str(), "r");      // id,price,qty,first,last,time,is_buyer_maker
        if (!p) return m;
        char line[512];
        while (std::fgets(line, sizeof line, p)) {
            long long id = 0, ts = 0; double px = 0;
            if (std::sscanf(line, "%lld,%lf", &id, &px) != 2) continue;                        // the header line
            const char* q = line; int commas = 0;
            for (; *q && commas < 5; ++q) if (*q == ',') ++commas;
            if (commas == 5 && std::sscanf(q, "%lld", &ts) == 1 && px > 0) m.push_back({ts + kBinanceDelayMs, px});
        }
        pclose(p);
        std::stable_sort(m.begin(), m.end(), [](const HlMid& a, const HlMid& b) { return a.r < b.r; });
        return m;
    }
    if (const fs::path a = env.root / "binance" / coin / (day + ".jsonl.zst"); fs::exists(a, ec)) m = hl_mids_from(hl::load_archive(a));
    return m;
}

// ── feature frame: one forward pass, value at event i uses events ≤ i (and reference books received by its time t) ──
struct Frame {
    std::vector<std::string> keys;
    std::vector<std::vector<double>> cols;
    int col(const std::string& k) const { for (size_t i = 0; i < keys.size(); ++i) if (keys[i] == k) return static_cast<int>(i); return -1; }
};
// One reference venue's mids (Hyperliquid or Extended), consumed by RECEIVE time up to the decision time.
struct RefTrack {
    const std::vector<HlMid>* m = nullptr;
    size_t j = 0;
    double mid = 0;
    int64_t r = -1;
    std::map<int64_t, std::deque<std::pair<int64_t, double>>> lead;   // window → (receive time, mid)
    std::deque<std::pair<int64_t, double>> basis;                       // (second, raw bps)
    int64_t basis_sec = -1;
    double basis_med = NaN, raw = NaN;
    bool ok = false;
    // advance to the decision time dec_t; own_mid/have_book of the venue; sample the basis once per second on books
    void step(int64_t dec_t, bool have_book, double own_mid, bool is_book, int64_t t) {
        if (m)
            while (j < m->size() && (*m)[j].r <= dec_t) {
                mid = (*m)[j].mid; r = (*m)[j].r;
                for (auto& [w, q] : lead) q.push_back({r, mid});
                ++j;
            }
        ok = m && r >= 0 && dec_t - r >= 0 && dec_t - r <= kHlStaleMs && have_book;
        for (auto& [w, q] : lead) while (q.size() >= 2 && q[1].first <= dec_t - w) q.pop_front();
        raw = NaN;
        if (ok) {
            raw = (own_mid - mid) / mid * 1e4;
            if (is_book && t / 1000 != basis_sec) {
                basis_sec = t / 1000;
                basis.push_back({basis_sec, raw});
                while (!basis.empty() && basis.front().first <= basis_sec - kBasisWindowS) basis.pop_front();
                if (static_cast<int>(basis.size()) >= kBasisMinSamples) {
                    std::vector<double> v; v.reserve(basis.size());
                    for (const auto& x : basis) v.push_back(x.second);
                    std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
                    basis_med = v[v.size() / 2];
                } else basis_med = NaN;
            }
        }
    }
    double basis_bps() const { return ok && std::isfinite(basis_med) ? raw - basis_med : NaN; }
    double lead_bps(int64_t w, int64_t dec_t) const {
        const auto it = lead.find(w);
        if (!ok || it == lead.end() || it->second.empty() || it->second.front().first > dec_t - w) return NaN;
        return (mid - it->second.front().second) / it->second.front().second * 1e4;
    }
};
inline Frame compute_frame(const DayData& d, const std::vector<HlMid>* hl, const std::set<std::string>& want, const hl::Params& p,
                           const std::vector<HlMid>* ext = nullptr, const std::vector<HlMid>* bn = nullptr) {
    Frame F;
    for (const auto& k : want) { F.keys.push_back(k); F.cols.emplace_back(d.ev.size(), NaN); }
    if (F.keys.empty()) return F;
    // windowed state, one per distinct window
    struct TW { int64_t w; std::deque<std::tuple<int64_t, double, double>> q; double buy = 0, sell = 0; };   // trades
    struct VW { int64_t w; std::deque<std::pair<int64_t, double>> q; double sum = 0; };                     // squared returns
    struct MW { int64_t w; std::deque<std::pair<int64_t, double>> q; };                                     // mid history
    std::map<int64_t, TW> tw; std::map<int64_t, VW> vw; std::map<int64_t, MW> mw;
    RefTrack H, X, Bn;
    H.m = hl; X.m = ext; Bn.m = bn;
    struct Col { int idx; std::string feat; int64_t w; };
    std::vector<Col> cols;
    for (size_t i = 0; i < F.keys.size(); ++i) {
        const auto& k = F.keys[i];
        const auto a = k.find('@');
        Col c{static_cast<int>(i), k.substr(0, a), a == std::string::npos ? 0 : std::atoll(k.c_str() + a + 1)};
        if (c.feat == "trade_flow_imbalance" || c.feat == "trades_per_min") tw[c.w].w = c.w;
        if (c.feat == "realized_vol_bps") vw[c.w].w = c.w;
        if (c.feat == "mid_move_bps") mw[c.w].w = c.w;
        if (c.feat == "cross_venue_lead_bps") H.lead[c.w];
        if (c.feat == "ext_lead_bps") X.lead[c.w];
        if (c.feat == "bn_lead_bps") Bn.lead[c.w];
        cols.push_back(c);
    }
    hl::Book book; bool have_book = false; double mid = 0, last_mid = 0;
    int64_t dec_t = INT64_MIN;                              // decision time on the exchange clock (running max of t)
    for (size_t i = 0; i < d.ev.size(); ++i) {
        const CE& e = d.ev[i];
        if (e.is_book) {
            book_of(d, e, book); have_book = true;
            mid = book.mid();
            if (last_mid > 0) {
                const double lr = std::log(mid / last_mid);
                for (auto& [w, v] : vw) { v.q.push_back({e.t, lr * lr}); v.sum += lr * lr; }
            }
            last_mid = mid;
            for (auto& [w, m] : mw) m.q.push_back({e.t, mid});
        } else {
            const double usd = e.tpx * e.tsz;
            for (auto& [w, t] : tw) { t.q.push_back({e.t, e.buy ? usd : 0, e.buy ? 0 : usd}); (e.buy ? t.buy : t.sell) += usd; }
        }
        for (auto& [w, t] : tw) while (!t.q.empty() && std::get<0>(t.q.front()) <= e.t - w) { t.buy -= std::get<1>(t.q.front()); t.sell -= std::get<2>(t.q.front()); t.q.pop_front(); }
        for (auto& [w, v] : vw) while (!v.q.empty() && v.q.front().first <= e.t - w) { v.sum -= v.q.front().second; v.q.pop_front(); }
        for (auto& [w, m] : mw) while (m.q.size() >= 2 && m.q[1].first <= e.t - w) m.q.pop_front();
        // reference books we had received by the DECISION time: the simulator acts on this event at its exchange
        // time t, so only r ≤ t may count — never our (later) receive time of the venue event.
        dec_t = std::max(dec_t, e.t);
        H.step(dec_t, have_book, mid, e.is_book, e.t);
        X.step(dec_t, have_book, mid, e.is_book, e.t);
        Bn.step(dec_t, have_book, mid, e.is_book, e.t);
        for (const auto& c : cols) {
            double x = NaN;
            const bool warm = e.t - d.t0 >= c.w;
            if (c.feat == "time_of_day_utc") x = static_cast<double>(((e.t % 86'400'000) + 86'400'000) % 86'400'000) / 3'600'000.0;
            else if (c.feat == "weekday") x = static_cast<double>(((e.t / 86'400'000) + 3) % 7);   // 1970-01-01 was a Thursday
            else if (!have_book) x = NaN;
            else if (c.feat == "spread_bps") x = (book.asks[0].px - book.bids[0].px) / mid * 1e4;
            else if (c.feat == "spread_ticks") x = (book.asks[0].px - book.bids[0].px) / strat_tick(p, book);
            else if (c.feat == "book_imbalance") { const double bs = book.bids[0].sz, as = book.asks[0].sz; x = bs + as > 0 ? (bs - as) / (bs + as) : 0; }
            else if (c.feat == "book_imbalance5") {
                double bs = 0, as = 0;
                for (size_t k = 0; k < std::min<size_t>(5, book.bids.size()); ++k) bs += book.bids[k].sz;
                for (size_t k = 0; k < std::min<size_t>(5, book.asks.size()); ++k) as += book.asks[k].sz;
                x = bs + as > 0 ? (bs - as) / (bs + as) : 0;
            }
            else if (c.feat == "trade_flow_imbalance") { if (warm) { const auto& t = tw[c.w]; const double s = t.buy + t.sell; x = s > 1e-12 ? (t.buy - t.sell) / s : 0; } }
            else if (c.feat == "trades_per_min") { if (warm) x = static_cast<double>(tw[c.w].q.size()) * 60'000.0 / c.w; }
            else if (c.feat == "realized_vol_bps") { if (warm) x = std::sqrt(std::max(0.0, vw[c.w].sum)) * 1e4; }
            else if (c.feat == "mid_move_bps") { const auto& q = mw[c.w].q; if (!q.empty() && q.front().first <= e.t - c.w) x = (mid - q.front().second) / q.front().second * 1e4; }
            else if (c.feat == "hl_basis_bps" || c.feat == "ext_basis_bps" || c.feat == "bn_basis_bps") x = (c.feat[0] == 'h' ? H : c.feat[0] == 'e' ? X : Bn).basis_bps();
            else if (c.feat == "hl_basis_ticks" || c.feat == "ext_basis_ticks" || c.feat == "bn_basis_ticks") {
                x = (c.feat[0] == 'h' ? H : c.feat[0] == 'e' ? X : Bn).basis_bps();
                if (std::isfinite(x)) x = x * mid / 1e4 / strat_tick(p, book);
            }
            else if (c.feat == "cross_venue_lead_bps") x = H.lead_bps(c.w, dec_t);
            else if (c.feat == "ext_lead_bps") x = X.lead_bps(c.w, dec_t);
            else if (c.feat == "bn_lead_bps") x = Bn.lead_bps(c.w, dec_t);
            F.cols[c.idx][i] = x;
        }
    }
    return F;
}

// ── one coin through one variant ────────────────────────────────────────────────────────────────────────────────
struct SegResult {
    std::string coin, day;
    int64_t fills = 0, quotes = 0, pulled = 0, decisions = 0;
    double vol = 0, pnl = 0, capture = 0, mk1 = 0, mk10 = 0, mk60 = 0, mkw = 0;
    std::map<int64_t, double> hours;      // UTC hour index → P&L $ (every hour the segment covered, zeros included)
    std::vector<std::array<double, 4>> fill_rows;   // Idea::keep_fills: (t ms, px, qty, side) of every resting fill
};

// A rate limiter shared by every coin an account trades (research/programs/mm_rate_limit.cpp): rolling 60 s window,
// up to 15 transactions batched into one request.
using Gate = hl::RequestGate;   // the shared limiter (hl_mm.hpp) — the paper engine runs the same one

// Final strategy parameters of an idea on one coin, or nullopt when the coin has no frozen setting.
inline std::optional<hl::Params> idea_params(const Idea& I, const Venue& V, const json& pf, const std::string& coin) {
    auto p = frozen_params(V, pf, coin, true);
    if (!p) return p;
    apply_params(*p, I.params);
    if (I.size_usd > 0) p->order_usd = std::max(I.size_usd, V.min_order_usd);
    if (I.place_ms >= 0) { p->place_latency_ms = I.place_ms; p->latency_ms = I.place_ms; }
    if (I.cancel_ms >= 0) p->cancel_latency_ms = I.cancel_ms;
    if (I.fair.type == FairType::Blend) p->fair = hl::Fair::Mid;
    p->modify = I.modify;
    return p;
}

class CoinRun {
public:
    CoinRun(const Idea& I, hl::Params p, const DayData& d, const Frame& F) : I_(I), p_(p), d_(d), F_(F), sim_(p) {
        for (const auto& R : I.rules) {
            std::vector<std::pair<int, double>> cs;
            for (const auto& C : R.when) {
                const double v = C.ref.empty() ? C.value : C.ref_mult * param_value(p, C.ref);
                cs.push_back({feature(C.feat)->state ? -1 : F.col(C.key), v});
            }
            conds_.push_back(cs);
        }
        last_true_.assign(I.rules.size(), std::numeric_limits<int64_t>::min() / 2);
        basis_col_ = F.col("hl_basis_bps");
        ext_col_ = F.col("ext_basis_bps");
        bn_col_ = F.col("bn_basis_bps");
        seg_t0_ = d.t0;
    }
    hl::Simulator& sim() { return sim_; }
    void step(size_t i) {
        const CE& e = d_.ev[i];
        if (e.is_book) {
            book_of(d_, e, book_);
            mid_ = book_.mid(); micro_ = book_.microprice(); have_ = true;
            if (!I_.plain()) { const auto ov = overlay(i, e.t); note(ov); sim_.set_overlay(ov); }
            sim_.on_book(book_);
        } else {
            hl::Trade t; t.t_ms = e.t; t.px = e.tpx; t.sz = e.tsz; t.buy_aggr = e.buy;
            sim_.on_trade(t);
            if (!I_.plain() && have_) {
                const auto ov = overlay(i, e.t);
                const auto& cur = sim_.overlay();
                if (ov.pull_bid != cur.pull_bid || ov.pull_ask != cur.pull_ask || ov.fair_off != cur.fair_off ||
                    ov.shift_ticks != cur.shift_ticks || ov.widen_ticks != cur.widen_ticks) { note(ov); sim_.set_overlay(ov); sim_.requote_now(e.t); }
            }
        }
    }
    SegResult finish() {
        SegResult s;
        s.coin = d_.coin; s.day = d_.day;
        sim_.flatten_now(sim_.last_book_ms());
        const hl::Result r = sim_.finish();
        s.fills = r.fills; s.quotes = r.quotes_placed + r.quotes_modified; s.vol = r.volume_usd; s.pnl = r.flat_pnl; s.capture = r.capture_bps_sum;
        s.mk1 = r.mk1_bps_sum; s.mk10 = r.mk10_bps_sum; s.mk60 = r.mk60_bps_sum; s.mkw = r.mk_weight;
        s.pulled = pulled_; s.decisions = decisions_;
        if (I_.keep_fills)
            for (const auto& f : sim_.fills()) if (!f.taker) s.fill_rows.push_back({static_cast<double>(f.t_ms), f.px, f.qty, static_cast<double>(f.side)});
        // A day file can open a few ms before its UTC midnight (the recorder's / Tardis's first message): those
        // moments belong to the file's own day, not to an hour of the previous one.
        const int64_t floor_h = d_.day.size() == 10 ? day_start_ms(d_.day) / 3'600'000 : INT64_MIN;
        if (!d_.ev.empty()) for (int64_t h = std::max(floor_h, d_.ev.front().t / 3'600'000); h <= d_.ev.back().t / 3'600'000; ++h) s.hours[h] = 0;
        double prev = 0;
        std::map<int64_t, double> end_eq;
        for (const auto& [t, eq] : r.equity) end_eq[std::max(floor_h, t / 3'600'000)] = eq;
        for (const auto& [h, eq] : end_eq) { s.hours[h] += eq - prev; prev = eq; }
        return s;
    }

private:
    bool cond_true(const std::pair<int, double>& c, const Cond& C, size_t i, int64_t t) const {
        double x;
        if (c.first >= 0) x = F_.cols[c.first][i];
        else if (C.feat == "inventory_usd") x = sim_.running().inv * mid_;
        else {   // minutes_since_last_fill
            int64_t last = seg_t0_;
            const auto& f = sim_.fills();
            for (auto it = f.rbegin(); it != f.rend(); ++it) if (!it->taker) { last = it->t_ms; break; }
            x = (t - last) / 60'000.0;
        }
        if (!std::isfinite(x)) return false;
        const double v = c.second;
        switch (C.opc) {
            case 0: return x < v;
            case 1: return x <= v;
            case 2: return x > v;
            case 3: return x >= v;
            case 4: return x == v;
            case 5: return x != v;
            case 6: return std::abs(x) < v;
            default: return std::abs(x) > v;
        }
    }
    hl::Simulator::Overlay overlay(size_t i, int64_t t) {
        hl::Simulator::Overlay ov;
        for (size_t r = 0; r < I_.rules.size(); ++r) {
            const Rule& R = I_.rules[r];
            bool ok = true;
            for (size_t c = 0; c < R.when.size() && ok; ++c) ok = cond_true(conds_[r][c], R.when[c], i, t);
            if (ok) last_true_[r] = t;
            if (!(ok || (R.hold_ms > 0 && t - last_true_[r] <= R.hold_ms))) continue;
            switch (R.act) {
                case Act::PullBid: ov.pull_bid = true; break;
                case Act::PullAsk: ov.pull_ask = true; break;
                case Act::PullBoth: ov.pull_bid = ov.pull_ask = true; break;
                case Act::Skew: ov.shift_ticks += R.value; break;
                case Act::Widen: ov.widen_ticks += R.value; break;
            }
        }
        // a reference venue's implied price = our mid less its basis deviation; unavailable → that term drops out
        auto implied = [&](int col) { const double bb = col >= 0 ? F_.cols[col][i] : NaN; return std::isfinite(bb) ? mid_ * (1 - bb / 1e4) : NaN; };
        if (I_.fair.type == FairType::HlAnchor || I_.fair.type == FairType::ExtAnchor || I_.fair.type == FairType::BnAnchor) {
            const double px = implied(I_.fair.type == FairType::HlAnchor ? basis_col_ : I_.fair.type == FairType::ExtAnchor ? ext_col_ : bn_col_);
            if (std::isfinite(px)) ov.fair_off = I_.fair.k * (px - mid_);
        } else if (I_.fair.type == FairType::Blend) {
            double w = I_.fair.w_mid + I_.fair.w_micro, num = I_.fair.w_mid * mid_ + I_.fair.w_micro * micro_;
            if (I_.fair.w_hl > 0) { const double px = implied(basis_col_); if (std::isfinite(px)) { w += I_.fair.w_hl; num += I_.fair.w_hl * px; } }
            if (I_.fair.w_ext > 0) { const double px = implied(ext_col_); if (std::isfinite(px)) { w += I_.fair.w_ext; num += I_.fair.w_ext * px; } }
            if (I_.fair.w_bn > 0) { const double px = implied(bn_col_); if (std::isfinite(px)) { w += I_.fair.w_bn; num += I_.fair.w_bn * px; } }
            if (w > 0) ov.fair_off = num / w - mid_;
        }
        return ov;
    }
    void note(const hl::Simulator::Overlay& ov) { ++decisions_; if (ov.pull_bid || ov.pull_ask) ++pulled_; }

    const Idea& I_;
    hl::Params p_;
    const DayData& d_;
    const Frame& F_;
    hl::Simulator sim_;
    hl::Book book_;
    double mid_ = 0, micro_ = 0;
    bool have_ = false;
    std::vector<std::vector<std::pair<int, double>>> conds_;
    std::vector<int64_t> last_true_;
    int basis_col_ = -1, ext_col_ = -1, bn_col_ = -1;
    int64_t seg_t0_ = 0;
    int64_t pulled_ = 0, decisions_ = 0;
};

// ── data split ──────────────────────────────────────────────────────────────────────────────────────────────────
struct Split {
    std::string mode;                     // "days" | "time"
    int recorded_days = 0;
    int64_t start_ms = 0, boundary_ms = 0, asof_ms = 0;
    json to_json() const { return {{"mode", mode}, {"recorded_days", recorded_days}, {"start", iso_ms(start_ms)}, {"boundary", iso_ms(boundary_ms)}, {"asof", iso_ms(asof_ms)},
                                   {"start_ms", start_ms}, {"boundary_ms", boundary_ms}, {"asof_ms", asof_ms}}; }
};
inline std::vector<hl::DayFile> venue_days(const Venue& V) {
    std::vector<hl::DayFile> out;
    std::error_code ec;
    for (const auto& d : V.dirs) if (fs::is_directory(d, ec)) for (auto& f : hl::list_days(d)) out.push_back(f);
    std::sort(out.begin(), out.end(), [](const hl::DayFile& a, const hl::DayFile& b) { return a.coin != b.coin ? a.coin < b.coin : a.day < b.day; });
    return out;
}
// First receive time in a live archive file (the recorder's start), ms; 0 if unreadable.
inline int64_t first_r_ms(const fs::path& f) {
    ZstdLines in(f);
    std::string line;
    for (int k = 0; k < 50 && in.next(line); ++k) {
        json j = json::parse(line, nullptr, false);
        if (!j.is_discarded() && j.contains("r") && j["r"].is_number()) return j["r"].get<int64_t>() / 1000;
    }
    return 0;
}
inline Split compute_split(const Venue& V, const std::vector<hl::DayFile>& files, int64_t asof) {
    Split s; s.asof_ms = asof;
    std::set<std::string> days;
    for (const auto& f : files) if (day_start_ms(f.day) <= asof) days.insert(f.day);
    s.recorded_days = static_cast<int>(days.size());
    if (days.empty()) { s.mode = "none"; return s; }
    s.start_ms = day_start_ms(*days.begin());
    if (s.recorded_days >= kDaySplitMin || !V.fixed_holdout_from.empty()) {
        s.mode = "days";
        const int ho = static_cast<int>(std::ceil(kHoldoutFrac * s.recorded_days - 1e-9));
        auto it = days.begin(); std::advance(it, s.recorded_days - ho);
        s.boundary_ms = day_start_ms(*it);
        if (!V.fixed_holdout_from.empty()) s.boundary_ms = std::min(s.boundary_ms, day_start_ms(V.fixed_holdout_from));
    } else {
        s.mode = "time";
        int64_t first = 0;
        for (const auto& f : files)
            if (f.day == *days.begin() && !f.tardis) { const int64_t r = first_r_ms(f.a); if (r > 0 && (!first || r < first)) first = r; }
        if (first) s.start_ms = first;
        s.boundary_ms = (s.start_ms + static_cast<int64_t>((1 - kHoldoutFrac) * (asof - s.start_ms))) / 3'600'000 * 3'600'000;
    }
    return s;
}

// ── evaluation ──────────────────────────────────────────────────────────────────────────────────────────────────
// A variant = one parsed idea (or a neighbour, or a baseline) run on its coin set inside one window.
struct Variant {
    Idea idea;
    std::string label;                    // "idea", neighbour label, "baseline", "baseline@$N"
    int owner = -1;                       // index of the idea it belongs to (-1 = a baseline)
    std::set<std::string> coins;
    std::vector<SegResult> segs;          // filled by run(), sorted (coin, day)
};

struct Agg {
    int64_t fills = 0, quotes = 0;
    double vol = 0, pnl = 0, capture = 0, mk1 = 0, mk10 = 0, mk60 = 0, mkw = 0;
    std::map<std::string, std::pair<double, int64_t>> coin;      // coin → (P&L, fills)
    std::map<int64_t, double> hours;
    double bps(double x) const { return vol > 0 ? x / vol * 1e4 : 0; }
    double net_bps() const { return vol > 0 ? pnl / vol * 1e4 : 0; }
    double mk(double s) const { return mkw > 0 ? s / mkw : 0; }
};
inline Agg aggregate(const std::vector<SegResult>& segs, const std::set<std::string>* only = nullptr) {
    Agg a;
    for (const auto& s : segs) {
        if (only && !only->count(s.coin)) continue;
        a.fills += s.fills; a.quotes += s.quotes; a.vol += s.vol; a.pnl += s.pnl; a.capture += s.capture;
        a.mk1 += s.mk1; a.mk10 += s.mk10; a.mk60 += s.mk60; a.mkw += s.mkw;
        auto& c = a.coin[s.coin]; c.first += s.pnl; c.second += s.fills;
        for (const auto& [h, v] : s.hours) a.hours[h] += v;
    }
    return a;
}
inline double t_stat(const std::vector<double>& x) {
    if (x.size() < 2) return 0;
    double m = 0; for (double v : x) m += v; m /= x.size();
    double ss = 0; for (double v : x) ss += (v - m) * (v - m);
    const double sd = std::sqrt(ss / (x.size() - 1));
    return sd > 0 ? m / (sd / std::sqrt(static_cast<double>(x.size()))) : (m > 0 ? 1e9 : 0);
}

// Load + run every variant over [w0, w1) of the venue. Variants without a rate limit run coin-day by coin-day on
// `threads` threads; rate-limited variants replay each day's coins together (books cut to kRateLimitLevels).
struct RunStats { double load_s = 0, sim_s = 0; int64_t coin_days = 0; };
inline RunStats run_variants(std::vector<Variant>& vars, const Venue& V, const json& pf, const Env& env, int64_t w0, int64_t w1, int threads) {
    RunStats st;
    std::mutex mu;
    const auto files = venue_days(V);
    std::set<std::string> all_coins;
    for (const auto& v : vars) all_coins.insert(v.coins.begin(), v.coins.end());
    struct Item { hl::DayFile f; };
    std::vector<Item> items;
    for (const auto& f : files) {
        if (!all_coins.count(f.coin) || env.excluded(V.name, f.day)) continue;
        const int64_t ds = day_start_ms(f.day), de = ds + 86'400'000;
        if (de <= w0 || ds >= w1) continue;
        items.push_back({f});
    }
    auto hl_mids_for = [&](const std::string& coin, const std::string& day) {
        std::vector<HlMid> m;
        const fs::path p = env.root / "hyperliquid" / coin / (day + ".jsonl.zst");
        std::error_code ec;
        if (V.cross && fs::exists(p, ec)) m = hl_mids_from(hl::load_archive(p));
        return m;
    };
    auto bn_mids_for = [&](const std::string& coin, const std::string& day) { return binance_mids(env, coin, day, V.bn); };
    auto ext_mids_for = [&](const std::string& coin, const std::string& day) {
        std::vector<HlMid> m;
        const fs::path p = env.root / "extended" / coin / (day + ".jsonl.zst");
        std::error_code ec;
        if (V.ext && fs::exists(p, ec)) m = hl_mids_from(hl::load_archive(p));
        return m;
    };
    auto params_for = [&](const Variant& v, const std::string& coin) { return idea_params(v.idea, V, pf, coin); };
    std::vector<int> plain_vars, rl_vars;
    for (size_t k = 0; k < vars.size(); ++k) (vars[k].idea.rate_per_min > 0 ? rl_vars : plain_vars).push_back(static_cast<int>(k));
    std::vector<std::vector<SegResult>> res(vars.size());
    // ── no rate limit: coin-day work items ──
    if (!plain_vars.empty()) {
        std::atomic<size_t> next{0};
        auto worker = [&]() {
            for (size_t k; (k = next++) < items.size();) {
                const auto& f = items[k].f;
                std::vector<int> mine;
                bool need_hl = false, need_ext = false, need_bn = false;
                std::set<std::string> keys;
                for (int v : plain_vars) if (vars[v].coins.count(f.coin)) { mine.push_back(v); need_hl |= vars[v].idea.needs_hl(); need_ext |= vars[v].idea.needs_ext(); need_bn |= vars[v].idea.needs_bn(); auto ks = vars[v].idea.frame_keys(); keys.insert(ks.begin(), ks.end()); }
                if (mine.empty()) continue;
                const auto c0 = std::chrono::steady_clock::now();
                DayData d;
                {
                    auto ev = hl::load_day(f);
                    d = compact(ev, 20, w0, w1);
                }
                d.coin = f.coin; d.day = f.day;
                std::vector<HlMid> hm, xm, bm;
                if (need_hl) hm = hl_mids_for(f.coin, f.day);
                if (need_ext) xm = ext_mids_for(f.coin, f.day);
                if (need_bn) bm = bn_mids_for(f.coin, f.day);
                const auto c1 = std::chrono::steady_clock::now();
                std::vector<SegResult> out(mine.size());
                bool any = !d.ev.empty();
                if (any) {
                    const auto p0 = params_for(vars[mine[0]], f.coin);
                    const Frame F = compute_frame(d, hm.empty() ? nullptr : &hm, keys, *p0, xm.empty() ? nullptr : &xm, bm.empty() ? nullptr : &bm);
                    for (size_t m = 0; m < mine.size(); ++m) {
                        auto p = params_for(vars[mine[m]], f.coin);
                        CoinRun cr(vars[mine[m]].idea, *p, d, F);
                        for (size_t i = 0; i < d.ev.size(); ++i) cr.step(i);
                        out[m] = cr.finish();
                    }
                }
                const auto c2 = std::chrono::steady_clock::now();
                std::lock_guard lk(mu);
                st.load_s += std::chrono::duration<double>(c1 - c0).count();
                st.sim_s += std::chrono::duration<double>(c2 - c1).count();
                ++st.coin_days;
                if (any) for (size_t m = 0; m < mine.size(); ++m) res[mine[m]].push_back(std::move(out[m]));
            }
        };
        std::vector<std::thread> th;
        for (int t = 0; t < std::max(1, threads); ++t) th.emplace_back(worker);
        for (auto& t : th) t.join();
    }
    // ── rate-limited: whole days, every coin of a variant's set replayed together ──
    if (!rl_vars.empty()) {
        std::map<std::string, std::vector<hl::DayFile>> by_day;
        for (const auto& it : items) by_day[it.f.day].push_back(it.f);
        for (auto& [day, fl] : by_day) {
            std::set<std::string> coins_needed; bool need_hl = false, need_ext = false, need_bn = false; std::set<std::string> keys;
            for (int v : rl_vars) { coins_needed.insert(vars[v].coins.begin(), vars[v].coins.end()); need_hl |= vars[v].idea.needs_hl(); need_ext |= vars[v].idea.needs_ext(); need_bn |= vars[v].idea.needs_bn(); auto ks = vars[v].idea.frame_keys(); keys.insert(ks.begin(), ks.end()); }
            const auto c0 = std::chrono::steady_clock::now();
            // load the day's coins on `threads` threads into fixed slots (file order), then keep the non-empty ones in order
            std::vector<hl::DayFile> lf;
            for (const auto& f : fl) if (coins_needed.count(f.coin)) lf.push_back(f);
            std::vector<DayData> slot_d(lf.size());
            std::vector<Frame> slot_f(lf.size());
            {
                std::atomic<size_t> next{0};
                auto loader = [&]() {
                    for (size_t q; (q = next++) < lf.size();) {
                        const auto& f = lf[q];
                        DayData d;
                        { auto ev = hl::load_day(f); d = compact(ev, kRateLimitLevels, w0, w1); }
                        d.coin = f.coin; d.day = f.day;
                        if (d.ev.empty()) continue;
                        std::vector<HlMid> hm, xm, bm;
                        if (need_hl) hm = hl_mids_for(f.coin, f.day);
                        if (need_ext) xm = ext_mids_for(f.coin, f.day);
                        if (need_bn) bm = bn_mids_for(f.coin, f.day);
                        const hl::Params p0 = *params_for(vars[rl_vars[0]], f.coin);
                        slot_f[q] = compute_frame(d, hm.empty() ? nullptr : &hm, keys, p0, xm.empty() ? nullptr : &xm, bm.empty() ? nullptr : &bm);
                        slot_d[q] = std::move(d);
                    }
                };
                std::vector<std::thread> th;
                for (int t = 0; t < std::max(1, threads); ++t) th.emplace_back(loader);
                for (auto& t : th) t.join();
            }
            std::vector<DayData> dd; std::vector<Frame> fr;
            for (size_t q = 0; q < lf.size(); ++q)
                if (!slot_d[q].ev.empty()) { dd.push_back(std::move(slot_d[q])); fr.push_back(std::move(slot_f[q])); ++st.coin_days; }
            slot_d.clear(); slot_f.clear();
            struct Ix { int64_t t; int c; uint32_t i; };
            std::vector<Ix> order;
            for (size_t c = 0; c < dd.size(); ++c) for (size_t i = 0; i < dd[c].ev.size(); ++i) order.push_back({dd[c].ev[i].t, static_cast<int>(c), static_cast<uint32_t>(i)});
            std::stable_sort(order.begin(), order.end(), [](const Ix& a, const Ix& b) { return a.t < b.t; });
            const auto c1 = std::chrono::steady_clock::now();
            // variants are independent (own limiter, own simulators) over shared read-only day data: one per thread
            std::atomic<size_t> nextv{0};
            auto runner = [&]() {
                for (size_t q; (q = nextv++) < rl_vars.size();) {
                    const int v = rl_vars[q];
                    std::vector<Gate> gates(vars[v].idea.rate_accounts);
                    for (auto& g : gates) { g.limit = vars[v].idea.rate_per_min; g.linger_ms = vars[v].idea.linger_ms; }
                    std::vector<std::unique_ptr<CoinRun>> runs(dd.size());
                    int k = 0;
                    for (size_t c = 0; c < dd.size(); ++c) {
                        if (!vars[v].coins.count(dd[c].coin)) continue;
                        runs[c] = std::make_unique<CoinRun>(vars[v].idea, *params_for(vars[v], dd[c].coin), dd[c], fr[c]);
                        Gate* g = &gates[k++ % gates.size()];
                        runs[c]->sim().set_send_gate([g](int64_t now) { return (*g)(now); });
                    }
                    for (const auto& x : order) if (runs[x.c]) runs[x.c]->step(x.i);
                    for (auto& r : runs) if (r) res[v].push_back(r->finish());   // res[v] is this thread's alone
                }
            };
            std::vector<std::thread> th;
            for (int t = 0; t < std::max(1, threads); ++t) th.emplace_back(runner);
            for (auto& t : th) t.join();
            const auto c2 = std::chrono::steady_clock::now();
            st.load_s += std::chrono::duration<double>(c1 - c0).count();
            st.sim_s += std::chrono::duration<double>(c2 - c1).count();
        }
    }
    for (size_t k = 0; k < vars.size(); ++k) {
        auto& s = res[k];
        std::sort(s.begin(), s.end(), [](const SegResult& a, const SegResult& b) { return a.coin != b.coin ? a.coin < b.coin : a.day < b.day; });
        vars[k].segs = std::move(s);
    }
    return st;
}

// Coins of a venue that an idea runs on: requested ∩ frozen params ∩ data in the window (∩ Hyperliquid archive when
// a feature or the fair price needs it; ∩ Extended archive likewise).
inline std::set<std::string> idea_coins(const Idea& I, const Venue& V, const json& pf, const Env& env, const std::vector<hl::DayFile>& files, int64_t w0, int64_t w1) {
    std::set<std::string> have;
    for (const auto& f : files) {
        const int64_t ds = day_start_ms(f.day);
        if (ds + 86'400'000 <= w0 || ds >= w1 || env.excluded(V.name, f.day)) continue;
        if (!frozen_params(V, pf, f.coin, true)) continue;
        if (I.needs_hl()) { std::error_code ec; if (!fs::is_directory(env.root / "hyperliquid" / f.coin, ec)) continue; }
        if (I.needs_ext()) { std::error_code ec; if (!fs::is_directory(env.root / "extended" / f.coin, ec)) continue; }
        if (I.needs_bn() && !binance_listed(env, f.coin)) continue;
        have.insert(f.coin);
    }
    if (I.markets.empty()) return have;
    std::set<std::string> out;
    for (const auto& m : I.markets) if (have.count(m)) out.insert(m);
    return out;
}

// ── verdict ─────────────────────────────────────────────────────────────────────────────────────────────────────
struct Verdict { bool pass = false; std::vector<std::string> fails; json card; };

inline json agg_json(const Agg& a) {
    return {{"fills", a.fills}, {"quotes", a.quotes}, {"volume_usd", a.vol}, {"pnl_usd", a.pnl}, {"net_bps", a.net_bps()},
            {"capture_bps", a.bps(a.capture) / 1e4}, {"mk1_bps", a.mk(a.mk1)}, {"mk10_bps", a.mk(a.mk10)}, {"mk60_bps", a.mk(a.mk60)}};
}

// unit_hours: 24 = calendar days, 4 = 4-hour blocks.
// A neighbour of the idea (±1 step of one knob) together with the frozen baseline at THAT neighbour's size.
struct Neighbour { const Variant* v; const Variant* base; };

// idea vs `base` = the frozen setting at the idea's own size (every criterion); `base100` = frozen at $100 (shown for
// information). k = the idea's position in the registry (alpha spending). unit_hours: 24 = calendar days, 4 = blocks.
inline Verdict judge(const Variant& idea, const Variant& base, const Variant* base100, const std::vector<Neighbour>& nbrs,
                     int64_t k, int unit_hours, bool holdout) {
    Verdict v;
    const Agg a = aggregate(idea.segs), b = aggregate(base.segs, &idea.coins);
    const double tn = t_needed(k);
    // per coin
    int traded = 0, pos = 0;
    json coins = json::array();
    for (const auto& [c, pf] : a.coin) {
        if (pf.second > 0) { ++traded; pos += pf.first > 0; }
        const auto bit = b.coin.find(c);
        double cvol = 0; for (const auto& s : idea.segs) if (s.coin == c) cvol += s.vol;
        coins.push_back({{"coin", c}, {"fills", pf.second}, {"pnl_usd", pf.first}, {"volume_usd", cvol}, {"net_bps", cvol > 0 ? pf.first / cvol * 1e4 : 0},
                         {"baseline_pnl_usd", bit == b.coin.end() ? 0 : bit->second.first}});
    }
    // per unit (day or 4-h block): every unit the window covered (zeros included) feeds the t-stat; a unit counts for
    // the consistency criterion when the idea traded in it
    std::map<int64_t, double> unit_pnl, bunit;
    for (const auto& [h, x] : a.hours) unit_pnl[h / unit_hours] += x;
    for (const auto& [h, x] : b.hours) bunit[h / unit_hours] += x;
    std::set<int64_t> active_units;
    for (const auto& s : idea.segs) if (s.fills > 0) for (const auto& [h, x] : s.hours) if (x != 0) active_units.insert(h / unit_hours);
    int units = 0, units_pos = 0;
    json ujs = json::array();
    std::vector<double> up;
    for (const auto& [u, x] : unit_pnl) {
        const bool act = active_units.count(u) > 0;
        if (act) { ++units; units_pos += x > 0; }
        up.push_back(x);
        ujs.push_back({{"unit", unit_hours == 24 ? iso_ms(u * 86'400'000).substr(0, 10) : iso_ms(u * unit_hours * 3'600'000LL).substr(0, 13) + "h"},
                       {"pnl_usd", x}, {"baseline_pnl_usd", bunit[u]}, {"traded", act}});
    }
    const double t = t_stat(up);
    const double d_bps = a.net_bps() - b.net_bps();
    auto need = [&](bool ok, const std::string& m) { if (!ok) v.fails.push_back(m); };
    char buf[256];
    const char* uname = unit_hours == 24 ? "days" : "4h blocks";
    std::snprintf(buf, sizeof buf, "net %.2f bps ≤ 0", a.net_bps()); need(a.net_bps() > 0, buf);
    std::snprintf(buf, sizeof buf, "beats the same-size baseline by %.2f bps < %.2f", d_bps, kBeatBps); need(d_bps >= kBeatBps, buf);
    std::snprintf(buf, sizeof buf, "%lld fills < %lld", static_cast<long long>(a.fills), static_cast<long long>(kMinFills)); need(a.fills >= kMinFills, buf);
    std::snprintf(buf, sizeof buf, "coins positive %d/%d < %.0f%%", pos, traded, kCoinShare * 100); need(traded > 0 && pos >= kCoinShare * traded - 1e-9, buf);
    std::snprintf(buf, sizeof buf, "%s positive %d/%d < %.0f%%", uname, units_pos, units, kUnitShare * 100);
    need(units > 0 && units_pos >= kUnitShare * units - 1e-9, buf);
    int rob_bad = 0;
    if (!holdout) {
        std::snprintf(buf, sizeof buf, "only %zu %s (< %d) for a t-stat", up.size(), uname, kMinUnits); need(static_cast<int>(up.size()) >= kMinUnits, buf);
        std::snprintf(buf, sizeof buf, "t %.2f < t_needed %.2f (registry k=%lld)", t, tn, static_cast<long long>(k)); need(t >= tn, buf);
        // neighbours: net > 0 AND no worse than their own same-size baseline. Only the count is ever published.
        for (const auto& n : nbrs) {
            const Agg na = aggregate(n.v->segs), nb = aggregate(n.base->segs, &n.v->coins);
            rob_bad += !(na.net_bps() > 0 && na.net_bps() - nb.net_bps() >= 0);
        }
        std::snprintf(buf, sizeof buf, "robustness: %d of %zu neighbours fail (net ≤ 0 or lose to their baseline)", rob_bad, nbrs.size()); need(rob_bad == 0, buf);
    }
    v.pass = v.fails.empty();
    v.card = {{"id", idea.idea.id}, {"venue", idea.idea.venue}, {"hash", idea.idea.hash}, {"kind", holdout ? "holdout" : "in_sample"},
              {"verdict", v.pass ? "PASS" : "FAIL"}, {"fails", v.fails}, {"spec", idea.idea.spec},
              {"coins_run", std::vector<std::string>(idea.coins.begin(), idea.coins.end())},
              {"idea", agg_json(a)}, {"baseline", agg_json(b)}, {"delta_bps", d_bps},
              {"criteria", {{"coins_traded", traded}, {"coins_positive", pos}, {"unit", unit_hours == 24 ? "day" : "4h"}, {"units", units}, {"units_positive", units_pos},
                            {"t_units", up.size()}, {"t", t}, {"t_needed", tn}, {"k", k}, {"alpha_k", alpha_k(k)},
                            {"neighbours", nbrs.size()}, {"neighbours_bad", rob_bad}}},
              {"per_coin", coins}, {"per_unit", ujs}};
    if (base100) v.card["baseline_100"] = agg_json(aggregate(base100->segs, &idea.coins));
    return v;
}

inline std::string card_md(const json& c) {
    std::ostringstream o;
    char b[512];
    auto f2 = [&](double x) { std::snprintf(b, sizeof b, "%.2f", x); return std::string(b); };
    o << "# mm_lab card — " << c["id"].get<std::string>() << " (" << c["venue"].get<std::string>() << ", " << c["kind"].get<std::string>() << ")\n\n";
    o << "**Verdict: " << c["verdict"].get<std::string>() << "**";
    if (!c["fails"].empty()) { o << " — "; bool first = true; for (const auto& f : c["fails"]) { o << (first ? "" : "; ") << f.get<std::string>(); first = false; } }
    o << "\n\n";
    if (c.contains("split")) {
        const auto& s = c["split"];
        o << "Window: " << c.value("window", "") << " (split mode " << s["mode"].get<std::string>() << ", frozen boundary " << s["boundary"].get<std::string>()
          << ", asof " << s["asof"].get<std::string>() << ")\n\n";
    }
    if (c.contains("notes") && !c["notes"].empty()) { for (const auto& n : c["notes"]) o << "- " << n.get<std::string>() << "\n"; o << "\n"; }
    o << "Spec hash `" << c["hash"].get<std::string>() << "`\n\n```json\n" << c["spec"].dump(1) << "\n```\n\n";
    o << "| | fills | volume $ | P&L $ | net bps | capture bps | mk1 | mk10 | mk60 |\n|---|---|---|---|---|---|---|---|---|\n";
    auto row = [&](const char* name, const json& a) {
        o << "| " << name << " | " << a["fills"].get<int64_t>() << " | " << f2(a["volume_usd"].get<double>()) << " | " << f2(a["pnl_usd"].get<double>())
          << " | " << f2(a["net_bps"].get<double>()) << " | " << f2(a["capture_bps"].get<double>()) << " | " << f2(a["mk1_bps"].get<double>())
          << " | " << f2(a["mk10_bps"].get<double>()) << " | " << f2(a["mk60_bps"].get<double>()) << " |\n";
    };
    row("idea", c["idea"]);
    row("baseline: frozen at the idea's size (judged against)", c["baseline"]);
    if (c.contains("baseline_100")) row("frozen at $100 (information)", c["baseline_100"]);
    const auto& cr = c["criteria"];
    o << "\nΔ vs same-size baseline " << f2(c["delta_bps"].get<double>()) << " bps · coins positive " << cr["coins_positive"].get<int>() << "/" << cr["coins_traded"].get<int>()
      << " · " << cr["unit"].get<std::string>() << " units positive " << cr["units_positive"].get<int>() << "/" << cr["units"].get<int>();
    if (c["kind"] == "in_sample")
        o << " · t(" << cr["unit"].get<std::string>() << " units, n=" << cr["t_units"].get<size_t>() << ") " << f2(cr["t"].get<double>()) << " vs needed "
          << f2(cr["t_needed"].get<double>()) << " (registry k=" << cr["k"].get<int64_t>() << ")"
          << " · robustness: " << cr["neighbours_bad"].get<int>() << " of " << cr["neighbours"].get<size_t>() << " neighbours fail (their numbers are not published)";
    o << "\n\n## Per coin\n\n| coin | fills | volume $ | P&L $ | net bps | baseline P&L $ |\n|---|---|---|---|---|---|\n";
    for (const auto& x : c["per_coin"])
        o << "| " << x["coin"].get<std::string>() << " | " << x["fills"].get<int64_t>() << " | " << f2(x["volume_usd"].get<double>()) << " | " << f2(x["pnl_usd"].get<double>())
          << " | " << f2(x["net_bps"].get<double>()) << " | " << f2(x["baseline_pnl_usd"].get<double>()) << " |\n";
    o << "\n## Per " << cr["unit"].get<std::string>() << " unit\n\n| unit | P&L $ | baseline P&L $ | traded |\n|---|---|---|---|\n";
    for (const auto& x : c["per_unit"])
        o << "| " << x["unit"].get<std::string>() << " | " << f2(x["pnl_usd"].get<double>()) << " | " << f2(x["baseline_pnl_usd"].get<double>()) << " | " << (x["traded"].get<bool>() ? "yes" : "no") << " |\n";
    return o.str();
}

// ── registry (append-only JSONL) ────────────────────────────────────────────────────────────────────────────────
// Lines: {"kind":"in_sample", id, hash, k, verdict, split, summary, spec, …} and {"kind":"holdout_look", id, hash, k,
// window} (a look carries NO result — researchers read this file). Both kinds count in k (alpha spending).
inline std::vector<json> read_registry(const fs::path& p) {
    std::vector<json> v;
    std::ifstream in(p);
    std::string line;
    while (std::getline(in, line)) { json j = json::parse(line, nullptr, false); if (!j.is_discarded() && j.is_object()) v.push_back(j); }
    return v;
}
inline int64_t registry_tests(const std::vector<json>& reg) {
    int64_t n = 0;
    for (const auto& j : reg) { const std::string k = j.value("kind", ""); n += k == "in_sample" || k == "holdout_look"; }
    return n;
}
inline const json* registry_find(const std::vector<json>& reg, const std::string& id) {
    const json* f = nullptr;
    for (const auto& j : reg) if (j.value("kind", "") == "in_sample" && j.value("id", "") == id) f = &j;
    return f;
}
inline bool registry_looked(const std::vector<json>& reg, const std::string& id) {
    for (const auto& j : reg) if (j.value("kind", "") == "holdout_look" && j.value("id", "") == id) return true;
    return false;
}
inline void registry_append(const fs::path& p, const json& line) {
    std::error_code ec;
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::app);
    out << line.dump() << "\n";
    out.flush();
    if (!out) throw std::runtime_error("cannot append to registry " + p.string());
}
inline bool same_path(const fs::path& a, const fs::path& b) {
    std::error_code ec1, ec2;
    return fs::weakly_canonical(fs::absolute(a), ec1) == fs::weakly_canonical(fs::absolute(b), ec2);
}

// ── frozen holdout boundaries (data/mm_lab/holdout_boundaries.json) ─────────────────────────────────────────────
// {"<venue>": {mode, recorded_days, start_ms, boundary_ms, frozen_at, frozen_asof_ms}}. Written ONCE per venue, by the
// first in-sample run on it; every later run (in-sample and holdout) reads it. A holdout run never creates one.
inline json read_boundaries(const fs::path& p) {
    std::ifstream in(p);
    if (!in) return json::object();
    json j = json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object()) throw std::runtime_error("unreadable holdout boundaries file " + p.string() + " — refusing to guess a new split");
    return j;
}
inline std::optional<Split> frozen_split(const Venue& V, const std::vector<hl::DayFile>& files, int64_t asof, const fs::path& path,
                                         bool may_write, const std::string& now_iso) {
    json all = read_boundaries(path);
    Split s;
    if (all.contains(V.name)) {
        const auto& e = all[V.name];
        s.mode = e.value("mode", "time"); s.recorded_days = e.value("recorded_days", 0);
        s.start_ms = e.value("start_ms", int64_t{0}); s.boundary_ms = e.value("boundary_ms", int64_t{0});
    } else {
        if (!may_write) return std::nullopt;
        s = compute_split(V, files, asof);
        if (s.mode == "none") return std::nullopt;
        all[V.name] = {{"mode", s.mode}, {"recorded_days", s.recorded_days}, {"start_ms", s.start_ms}, {"boundary_ms", s.boundary_ms},
                       {"boundary", iso_ms(s.boundary_ms)}, {"frozen_at", now_iso}, {"frozen_asof_ms", asof}};
        std::error_code ec;
        if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
        const fs::path tmp = path.string() + ".tmp";
        std::ofstream(tmp) << all.dump(1) << "\n";
        fs::rename(tmp, path, ec);
        if (ec) throw std::runtime_error("cannot write " + path.string());
    }
    s.asof_ms = asof;
    return s;
}

// ── the whole judge for one batch of ideas ──────────────────────────────────────────────────────────────────────
struct Options {
    Env env;
    fs::path out_dir = "data/mm_lab";
    fs::path registry = "data/mm_lab/registry.jsonl";
    fs::path pinned_registry = "data/mm_lab/registry.jsonl";        // the only registry a holdout look accepts
    fs::path boundaries = "data/mm_lab/holdout_boundaries.json";
    int64_t asof_ms = 0;          // 0 = derived by the caller
    int threads = 3;
    bool holdout = false;
    std::string now_iso;          // registry timestamp (tests pin it)
};
struct BatchResult {
    std::vector<json> cards;      // one per evaluated idea
    std::vector<std::string> refused;
    double seconds = 0;
    json runtime = json::object();
};

inline json load_params_file(const Venue& V) {
    if (V.params_file.empty()) return json::object();
    std::ifstream in(V.params_file);
    json j = json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.contains("coins")) throw std::runtime_error("cannot read params file " + V.params_file.string());
    return j;
}

// ── daily baseline (G1, 2026-10-27): the venue's frozen setting over [w0, w1) under the venue's standard execution —
// rate limit shared by every coin, measured place/cancel delays — one row per coin-day. No split, no registry, no
// verdict: this is the gate's data check, not a judgement of an idea. Excluded days are skipped like everywhere.
inline json daily_baseline(const Env& env, const std::string& venue, double size_usd, int64_t w0, int64_t w1, int threads) {
    const auto V = venue_of(venue, env);
    if (!V) throw std::runtime_error("unknown venue " + venue);
    const json pf = load_params_file(*V);
    json spec = {{"id", "baseline"}, {"venue", venue}};
    if (size_usd > 0) spec["size_usd"] = size_usd;
    std::string err;
    auto I = parse_idea(spec, env, err);
    if (!I) throw std::runtime_error(err);
    const auto files = venue_days(*V);
    std::vector<Variant> vars(1);
    vars[0].idea = *I; vars[0].label = "baseline";
    vars[0].coins = idea_coins(*I, *V, pf, env, files, w0, w1);
    run_variants(vars, *V, pf, env, w0, w1, threads);
    json rows = json::array();
    for (const auto& g : vars[0].segs)
        rows.push_back({{"day", g.day}, {"coin", g.coin}, {"pnl_usd", g.pnl}, {"volume_usd", g.vol}, {"fills", g.fills},
                        {"quotes", g.quotes}, {"capture_bps", g.vol > 0 ? g.capture / g.vol : 0.0},
                        {"mk10_bps", g.mkw > 0 ? g.mk10 / g.mkw : 0.0}});
    return {{"venue", venue}, {"size_usd", size_usd}, {"rate_per_min", I->rate_per_min}, {"place_ms", I->place_ms < 0 ? V->place_ms : I->place_ms},
            {"cancel_ms", I->cancel_ms < 0 ? V->cancel_ms : I->cancel_ms}, {"from", iso_ms(w0)}, {"to", iso_ms(w1)}, {"rows", rows}};
}

// ── fill study (2026-10-27): which signals, known BEFORE our cancel could land, predict a fill's markout ────────────
// Runs the frozen baseline (the venue's standard execution, rate limit included) over [w0, w1) and keeps every resting
// fill. For each fill it reads the feature frame at the last event ≤ fill time − the venue's cancel latency (the latest
// moment a pull could still have removed the order) and the fill's side-signed markouts 1 / 10 / 60 s (bps; fills less
// than 60 s before w1 are dropped so no markout reads past the window). Per feature: correlation of side × feature
// with the 10 s markout (direction-less features — spread, activity, volatility — are used as they are: a pull of
// both sides, not of one), and an honest "avoid" test — the worse tail (top or bottom decile of side × feature) and its
// threshold are chosen on the FIRST half of the window, then measured on the SECOND half: share of fills avoided, their
// mean markout vs the rest, and the $ the avoided fills lost (or made) at 10 s. Research only; no registry, no verdict.
inline json fill_study(const Env& env, const std::string& venue, double size_usd, int64_t w0, int64_t w1, int threads) {
    const auto V = venue_of(venue, env);
    if (!V) throw std::runtime_error("unknown venue " + venue);
    const json pf = load_params_file(*V);
    json spec = {{"id", "baseline"}, {"venue", venue}};
    if (size_usd > 0) spec["size_usd"] = size_usd;
    std::string err;
    auto I = parse_idea(spec, env, err);
    if (!I) throw std::runtime_error(err);
    I->keep_fills = true;
    const auto files = venue_days(*V);
    std::vector<Variant> vars(1);
    vars[0].idea = *I; vars[0].label = "baseline";
    vars[0].coins = idea_coins(*I, *V, pf, env, files, w0, w1);
    run_variants(vars, *V, pf, env, w0, w1, threads);
    const std::vector<std::string> keys = {"book_imbalance", "book_imbalance5", "spread_ticks", "trade_flow_imbalance@1000",
        "trade_flow_imbalance@5000", "trades_per_min@60000", "realized_vol_bps@60000", "mid_move_bps@1000",
        "hl_basis_bps", "cross_venue_lead_bps@500", "cross_venue_lead_bps@1000", "ext_basis_bps", "ext_lead_bps@500",
        "ext_lead_bps@1000", "bn_basis_bps", "bn_lead_bps@500", "bn_lead_bps@1000"};
    const int64_t lag = V->cancel_ms, mid_t = w0 + (w1 - w0) / 2;
    struct Row { int64_t t; int side; double usd, mk1, mk10, mk60; std::vector<double> x; std::string coin; };
    std::vector<Row> rows;
    for (const auto& g : vars[0].segs) {
        if (g.fill_rows.empty()) continue;
        const hl::DayFile* df = nullptr;
        for (const auto& f : files) if (f.coin == g.coin && f.day == g.day) df = &f;
        if (!df) continue;
        DayData d;
        { auto ev = hl::load_day(*df); d = compact(ev, 20, w0, w1); }
        d.coin = g.coin; d.day = g.day;
        std::vector<HlMid> hm, xm, bm;
        std::error_code ec;
        if (V->cross) if (const fs::path p = env.root / "hyperliquid" / g.coin / (g.day + ".jsonl.zst"); fs::exists(p, ec)) hm = hl_mids_from(hl::load_archive(p));
        if (V->ext) if (const fs::path p = env.root / "extended" / g.coin / (g.day + ".jsonl.zst"); fs::exists(p, ec)) xm = hl_mids_from(hl::load_archive(p));
        bm = binance_mids(env, g.coin, g.day, V->bn);
        const auto p0 = idea_params(*I, *V, pf, g.coin);
        if (!p0) continue;
        const Frame F = compute_frame(d, hm.empty() ? nullptr : &hm, std::set<std::string>(keys.begin(), keys.end()), *p0,
                                      xm.empty() ? nullptr : &xm, bm.empty() ? nullptr : &bm);
        std::vector<std::pair<int64_t, double>> mids;
        hl::Book bk;
        for (const auto& e : d.ev) if (e.is_book) { book_of(d, e, bk); if (bk.valid()) mids.push_back({e.t, bk.mid()}); }
        auto mid_at = [&](int64_t t) {
            auto it = std::upper_bound(mids.begin(), mids.end(), t, [](int64_t v, const auto& m) { return v < m.first; });
            return it == mids.begin() ? NaN : std::prev(it)->second;
        };
        for (const auto& fr : g.fill_rows) {
            const int64_t tf = static_cast<int64_t>(fr[0]);
            if (tf + 60'000 >= w1) continue;
            auto it = std::upper_bound(d.ev.begin(), d.ev.end(), tf - lag, [](int64_t v, const CE& e) { return v < e.t; });
            if (it == d.ev.begin()) continue;
            const size_t idx = static_cast<size_t>(std::prev(it) - d.ev.begin());
            Row r; r.t = tf; r.side = static_cast<int>(fr[3]); r.usd = fr[1] * fr[2]; r.coin = g.coin;
            auto mk = [&](int64_t h) { const double m = mid_at(tf + h); return std::isfinite(m) ? r.side * (m - fr[1]) / fr[1] * 1e4 : NaN; };
            r.mk1 = mk(1'000); r.mk10 = mk(10'000); r.mk60 = mk(60'000);
            if (!std::isfinite(r.mk10)) continue;
            for (const auto& k : keys) { const int c = F.col(k); r.x.push_back(c >= 0 ? F.cols[c][idx] : NaN); }
            rows.push_back(std::move(r));
        }
    }
    json feats = json::array();
    double all_mk = 0, all_usd = 0;
    for (const auto& r : rows) { all_mk += r.usd * r.mk10; all_usd += r.usd; }
    auto unsigned_feature = [](const std::string& k) {
        return k == "spread_ticks" || k.rfind("trades_per_min", 0) == 0 || k.rfind("realized_vol_bps", 0) == 0;
    };
    for (size_t k = 0; k < keys.size(); ++k) {
        const bool uns = unsigned_feature(keys[k]);
        std::vector<std::pair<double, const Row*>> A, B;     // (side × feature, or the feature itself, row): halves
        double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0, n = 0;
        for (const auto& r : rows) {
            if (!std::isfinite(r.x[k])) continue;
            const double v = uns ? r.x[k] : r.side * r.x[k];
            (r.t < mid_t ? A : B).push_back({v, &r});
            sx += v; sy += r.mk10; sxx += v * v; syy += r.mk10 * r.mk10; sxy += v * r.mk10; n += 1;
        }
        json f = {{"feature", keys[k]}, {"fills", n}};
        if (n < 50 || A.size() < 30 || B.size() < 30) { feats.push_back(f); continue; }
        const double cov = sxy / n - sx / n * sy / n, vx = sxx / n - (sx / n) * (sx / n), vy = syy / n - (sy / n) * (sy / n);
        f["corr_mk10"] = vx > 0 && vy > 0 ? cov / std::sqrt(vx * vy) : 0;
        // first half: which decile tail is worse, and its threshold
        std::vector<double> va; for (auto& [v, _] : A) va.push_back(v);
        std::sort(va.begin(), va.end());
        const double lo = va[va.size() / 10], hi = va[va.size() - 1 - va.size() / 10];
        double mlo = 0, nlo = 0, mhi = 0, nhi = 0;
        for (auto& [v, r] : A) { if (v <= lo) { mlo += r->mk10; nlo += 1; } if (v >= hi) { mhi += r->mk10; nhi += 1; } }
        const bool top = (nhi ? mhi / nhi : 0) < (nlo ? mlo / nlo : 0);
        const double thr = top ? hi : lo;
        double m_av = 0, n_av = 0, m_keep = 0, n_keep = 0, usd_av = 0;
        for (auto& [v, r] : B) {
            if (top ? v >= thr : v <= thr) { m_av += r->mk10; n_av += 1; usd_av += r->usd * r->mk10 / 1e4; }
            else { m_keep += r->mk10; n_keep += 1; }
        }
        f["avoid_tail"] = top ? "top" : "bottom";
        f["signed_by_side"] = !uns;
        f["threshold_first_half"] = thr;                  // in the feature's own units (× side for signed features)
        f["avoid_share_test"] = n_av / B.size();
        f["avoided_mk10_test"] = n_av ? m_av / n_av : 0;
        f["kept_mk10_test"] = n_keep ? m_keep / n_keep : 0;
        f["avoided_usd_at_10s_test"] = usd_av;            // negative = the avoided fills lost this much at 10 s
        feats.push_back(f);
    }
    return {{"venue", venue}, {"from", iso_ms(w0)}, {"to", iso_ms(w1)}, {"split", iso_ms(mid_t)}, {"cancel_lag_ms", lag},
            {"fills", rows.size()}, {"mean_mk10_all", all_usd > 0 ? all_mk / all_usd : 0}, {"features", feats}};
}


inline BatchResult judge_batch(const json& doc, const Options& opt) {
    BatchResult br;
    const auto t_start = std::chrono::steady_clock::now();
    if (!doc.is_object() || !doc.contains("ideas") || !doc["ideas"].is_array()) { br.refused.push_back("input: expected {\"ideas\":[...]}"); return br; }
    if (opt.holdout && !same_path(opt.registry, opt.pinned_registry)) {
        br.refused.push_back("holdout: only the pinned registry " + opt.pinned_registry.string() + " is accepted — refused");
        return br;
    }
    const auto reg = read_registry(opt.registry);
    std::vector<Idea> ideas;
    std::set<std::string> ids;
    for (const auto& j : doc["ideas"]) {
        std::string err;
        std::optional<Idea> I;
        try { I = parse_idea(j, opt.env, err); } catch (const json::exception& e) { err = std::string("malformed value: ") + e.what(); }
        const std::string id = j.is_object() && j.contains("id") && j["id"].is_string() ? j["id"].get<std::string>() : "?";
        if (!I) { br.refused.push_back(id + ": " + err); continue; }
        if (ids.count(I->id)) { br.refused.push_back(I->id + ": duplicate id in this batch"); continue; }
        const json* prev = registry_find(reg, I->id);
        if (opt.holdout) {
            if (!prev) { br.refused.push_back(I->id + ": not in the registry — the holdout is only for in-sample PASSes"); continue; }
            if (prev->value("verdict", "") != "PASS") { br.refused.push_back(I->id + ": in-sample verdict " + prev->value("verdict", "?") + " — no holdout without an in-sample PASS"); continue; }
            if (prev->value("hash", "") != I->hash) { br.refused.push_back(I->id + ": spec differs from the registered one (hash) — refused"); continue; }
            if (registry_looked(reg, I->id)) { br.refused.push_back(I->id + ": the holdout was already looked at once for this id — refused"); continue; }
        } else if (prev) { br.refused.push_back(I->id + ": id already tested — a re-test needs a new id (and counts again)"); continue; }
        if (!opt.holdout && static_cast<int>(ideas.size()) >= kMaxIdeasPerRun) { br.refused.push_back(I->id + ": over the " + std::to_string(kMaxIdeasPerRun) + "-idea cap per run"); continue; }
        ids.insert(I->id);
        ideas.push_back(std::move(*I));
    }
    // alpha spending: the i-th accepted idea of this batch is entry k = (entries so far) + i + 1
    const int64_t k0 = registry_tests(reg);
    std::map<std::string, std::vector<int>> by_venue;
    for (size_t i = 0; i < ideas.size(); ++i) by_venue[ideas[i].venue].push_back(static_cast<int>(i));
    std::vector<json> cards(ideas.size());
    for (auto& [vname, idx] : by_venue) {
        const Venue V = *venue_of(vname, opt.env);
        json pf;
        try { pf = load_params_file(V); } catch (const std::exception& e) { for (int i : idx) br.refused.push_back(ideas[i].id + ": " + e.what()); continue; }
        const auto files = venue_days(V);
        std::optional<Split> sp;
        try { sp = frozen_split(V, files, opt.asof_ms, opt.boundaries, !opt.holdout, opt.now_iso); }
        catch (const std::exception& e) { for (int i : idx) br.refused.push_back(ideas[i].id + ": " + e.what()); continue; }
        if (!sp) { for (int i : idx) br.refused.push_back(ideas[i].id + ": no frozen holdout boundary / no data for " + vname); continue; }
        if (opt.holdout && opt.asof_ms <= sp->boundary_ms) { for (int i : idx) br.refused.push_back(ideas[i].id + ": --asof is not after the frozen boundary — nothing to look at"); continue; }
        // Window: in-sample = everything before the frozen boundary; holdout = [boundary, asof].
        const int64_t w0 = opt.holdout ? sp->boundary_ms : 0, w1 = opt.holdout ? opt.asof_ms + 1 : std::min(sp->boundary_ms, opt.asof_ms + 1);
        std::vector<Variant> vars;
        std::map<std::string, int> base_idx;                    // (rate limit, latency, size, [coins]) → baseline variant
        auto base_for = [&](const Idea& I, const std::set<std::string>& coins, bool at_own_size) {
            const double size = at_own_size ? I.size_usd : 0;
            std::string key = std::to_string(I.rate_per_min) + "/" + std::to_string(I.rate_accounts) + "/" + std::to_string(I.place_ms) + "/" +
                              std::to_string(I.cancel_ms) + "/" + std::to_string(size > 0 ? std::max(size, V.min_order_usd) : 0.0);
            if (I.rate_per_min > 0) for (const auto& c : coins) key += "," + c;   // a shared limiter couples the coins
            auto it = base_idx.find(key);
            if (it == base_idx.end()) {
                json bj = {{"id", "baseline"}, {"venue", vname}};
                if (I.rate_per_min > 0) bj["rate_limit"] = {{"per_min", I.rate_per_min}, {"accounts", I.rate_accounts}};
                if (I.place_ms >= 0) bj["latency"] = {{"place_ms", I.place_ms}, {"cancel_ms", I.cancel_ms}};
                if (size > 0) bj["size_usd"] = size;
                std::string err;
                Variant b; b.idea = *parse_idea(bj, opt.env, err); b.label = "baseline";
                it = base_idx.emplace(key, static_cast<int>(vars.size())).first;
                vars.push_back(b);
            }
            vars[it->second].coins.insert(coins.begin(), coins.end());
            return it->second;
        };
        std::vector<int> idea_var(ideas.size(), -1), base_of(ideas.size(), -1), base100_of(ideas.size(), -1);
        std::vector<std::vector<std::pair<int, int>>> nbr_var(ideas.size());       // (neighbour, its same-size baseline)
        for (int i : idx) {
            Variant v; v.idea = ideas[i]; v.label = "idea"; v.owner = i; v.coins = idea_coins(ideas[i], V, pf, opt.env, files, w0, w1);
            idea_var[i] = static_cast<int>(vars.size());
            vars.push_back(v);
        }
        for (int i : idx) {
            const auto coins = vars[idea_var[i]].coins;
            base_of[i] = base_for(ideas[i], coins, true);
            base100_of[i] = base_for(ideas[i], coins, false);
            if (opt.holdout) continue;
            for (auto& [label, spec] : neighbours(ideas[i], V)) {
                std::string err;
                auto N = parse_idea(spec, opt.env, err);
                if (!N) continue;
                Variant nv; nv.idea = *N; nv.label = label; nv.owner = i; nv.coins = coins;
                const int nb = base_for(*N, coins, true);
                nbr_var[i].push_back({static_cast<int>(vars.size()), nb});
                vars.push_back(nv);
            }
        }
        const RunStats rs = run_variants(vars, V, pf, opt.env, w0, w1, opt.threads);
        br.runtime[vname] = {{"window_end", iso_ms(w1)}, {"variants", vars.size()}, {"coin_days", rs.coin_days}, {"load_s", rs.load_s}, {"sim_s", rs.sim_s}};
        // unit for consistency and the t-stat: calendar days when the window holds ≥ kDayUnitMin days, else 4-hour blocks
        std::set<std::string> wdays;
        for (const auto& v : vars) for (const auto& s : v.segs) wdays.insert(s.day);
        const int unit_hours = static_cast<int>(wdays.size()) >= kDayUnitMin ? 24 : 4;
        for (int i : idx) {
            std::vector<Neighbour> nb;
            for (auto [n, b] : nbr_var[i]) nb.push_back({&vars[n], &vars[b]});
            const int64_t k = opt.holdout ? 0 : k0 + i + 1;
            const Variant* b100 = base100_of[i] != base_of[i] ? &vars[base100_of[i]] : nullptr;
            Verdict vd = judge(vars[idea_var[i]], vars[base_of[i]], b100, nb, std::max<int64_t>(1, k), unit_hours, opt.holdout);
            vd.card["split"] = sp->to_json();
            vd.card["window"] = (w0 ? iso_ms(w0) : iso_ms(sp->start_ms)) + " → " + iso_ms(w1);
            json notes = json::array();
            const Idea& I = ideas[i];
            if (I.size_usd > 0 && I.size_usd < V.min_order_usd) notes.push_back("size $" + std::to_string(I.size_usd) + " rounded up to the venue minimum $" + std::to_string(V.min_order_usd) +
                                                                                 (V.name == "lighter" || V.name == "hyperliquid_hist" ? "" : " (assumed minimum)"));
            if (I.rate_per_min > 0) notes.push_back("rate limit " + std::to_string(I.rate_per_min) + " requests/min × " + std::to_string(I.rate_accounts) +
                                                    " account(s): every coin of a day replayed together, books cut to " + std::to_string(kRateLimitLevels) +
                                                    " levels per side; the baselines run under the same limiter");
            if (I.needs_hl()) notes.push_back("cross-venue: Hyperliquid books received by the decision time (exchange clock); coins without a Hyperliquid archive are skipped");
            if (I.needs_ext()) notes.push_back("cross-venue: Extended books received by the decision time (exchange clock); coins without an Extended archive are skipped");
            if (I.needs_bn()) notes.push_back("cross-venue: Binance futures — published trade history (last price, Binance match time + 135 ms) where it exists, else the live bookTicker archive (receive time); coins Binance does not list are skipped");
            if (const auto ex = opt.env.excluded_days.find(V.name); ex != opt.env.excluded_days.end())
                for (const auto& [day, why] : ex->second) {
                    const int64_t ds = day_start_ms(day);
                    if (ds + 86'400'000 > w0 && ds < w1) notes.push_back("excluded day " + day + " (damaged archive): " + why);
                }
            if (vars[idea_var[i]].coins.empty()) notes.push_back("no coin with data in the window");
            if (unit_hours == 4) notes.push_back("consistency and t-stat on 4-hour UTC blocks (fewer than " + std::to_string(kDayUnitMin) + " calendar days in the window)");
            vd.card["notes"] = notes;
            cards[i] = vd.card;
        }
    }
    for (size_t i = 0; i < ideas.size(); ++i) {
        if (cards[i].is_null()) continue;
        br.cards.push_back(cards[i]);
        const auto& c = cards[i];
        if (!opt.holdout) {
            registry_append(opt.registry, {{"kind", "in_sample"}, {"id", c["id"]}, {"hash", c["hash"]}, {"venue", c["venue"]}, {"ts", opt.now_iso},
                                           {"verdict", c["verdict"]}, {"split", c["split"]}, {"k", c["criteria"]["k"]}, {"t_needed", c["criteria"]["t_needed"]},
                                           {"summary", {{"net_bps", c["idea"]["net_bps"]}, {"delta_bps", c["delta_bps"]}, {"pnl_usd", c["idea"]["pnl_usd"]},
                                                        {"fills", c["idea"]["fills"]}, {"t", c["criteria"]["t"]}}},
                                           {"spec", c["spec"]}});
        } else {   // the look itself is logged and counted; its result is NOT written here
            registry_append(opt.registry, {{"kind", "holdout_look"}, {"id", c["id"]}, {"hash", c["hash"]}, {"venue", c["venue"]}, {"ts", opt.now_iso},
                                           {"k", registry_tests(read_registry(opt.registry)) + 1}, {"window", c["window"]}});
        }
    }
    br.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count();
    return br;
}

}  // namespace mmlab
