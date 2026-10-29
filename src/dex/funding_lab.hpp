#pragma once
// funding_lab — backtest of cross-venue funding carry (docs/funding_carry_plan.md, stage F0).
// Implements research/preregistered/2026-10-28_funding_carry.md exactly; nothing here may loosen it.
//
// For one coin and a venue pair (a, b): position +1 = SHORT on a, LONG on b; −1 = the opposite. A venue's
// "payment" at time t is the funding LONGS pay at t as a fraction of notional (negative = shorts pay). Holding +1
// over a payment at t earns pay_a(t) − pay_b(t).
//
// Timing (causal): the decision at hour h is made just after the payments stamped h; the signal uses only
// payments stamped ≤ h; a position chosen at h earns payments stamped > h while it is held. Each half starts
// flat (the signal may look back into earlier data) and ends flat (the closing costs are charged).
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace flab {

using json = nlohmann::json;
constexpr int64_t kHour = 3600;
constexpr double kHoursYear = 8760.0;
enum Venue { LT = 0, HL = 1, BN = 2 };
inline const char* venue_name(int v) { return v == LT ? "lighter" : v == HL ? "hyperliquid" : "binance"; }

struct Series {
    std::map<int64_t, double> pay;   // hour-aligned unix → fraction of notional paid by LONGS at that time
};
struct Candle { double o = 0, h = 0, l = 0, c = 0; };
struct Coin {
    std::string name;
    std::array<Series, 3> v;
    std::map<int64_t, Candle> day;   // day start (unix) → Hyperliquid daily candle
};

inline int64_t floor_hour(int64_t t) { return t - ((t % kHour) + kHour) % kHour; }
inline double num(const json& x) { return x.is_string() ? std::stod(x.get<std::string>()) : x.get<double>(); }

// Lighter: "rate" is % per hour, "dir" "long" = longs pay. Duplicate timestamps (window edges): the first is kept.
inline Series parse_lighter(const json& rows) {
    Series s;
    for (const auto& r : rows) {
        double sign = r.value("dir", std::string("long")) == "long" ? 1.0 : -1.0;
        s.pay.emplace(floor_hour(r.at("t").get<int64_t>()), sign * num(r.at("rate")) / 100.0);
    }
    return s;
}
// Hyperliquid (fraction per hour) and Binance (fraction per settlement): positive = longs pay.
inline Series parse_plain(const json& rows) {
    Series s;
    for (const auto& r : rows) s.pay.emplace(floor_hour(r.at("t").get<int64_t>()), num(r.at("rate")));
    return s;
}

struct Costs {
    std::array<double, 3> fee_bps{0.0, 4.5, 5.0};   // taker fee per leg each way: Lighter Standard, Hyperliquid, Binance
    double spread_bps = 2.0;                        // per leg each way
    double basis_rt_bps = 5.0;                      // per round trip (half charged at entry, half at exit)
    double leverage = 3.0;                          // capital per position = 2 × notional / leverage
    double one_way(int a, int b) const { return (fee_bps[a] + fee_bps[b] + 2 * spread_bps + basis_rt_bps / 2) / 1e4; }
    double capital() const { return 2.0 / leverage; }
};

struct Cell { int L = 24; double theta = 0.2; };   // trailing hours, entry threshold (fraction per year)

// Per-hour equivalent rate of venue v at hour k, known at k: hourly venues = the payment stamped k; Binance = the
// last settlement at or before k divided by its interval (default 8 h), for up to 24 h after it. NaN = unknown.
class Equiv {
public:
    Equiv(const Series& s, bool settlement) : s_(s), settle_(settlement) {}
    double at(int64_t k) const {
        if (!settle_) { auto it = s_.pay.find(k); return it == s_.pay.end() ? NAN : it->second; }
        auto it = s_.pay.upper_bound(k);
        if (it == s_.pay.begin()) return NAN;
        --it;
        if (k - it->first > 24 * kHour) return NAN;
        double interval = 8;
        if (it != s_.pay.begin()) { auto p = std::prev(it); interval = std::clamp(double(it->first - p->first) / kHour, 1.0, 8.0); }
        return it->second / interval;
    }
private:
    const Series& s_;
    bool settle_;
};

struct Trade { int64_t open = 0, close = 0; int dir = 0; double funding = 0, cost = 0; };
struct PairRun {
    std::string coin;
    int a = 0, b = 0;
    std::vector<Trade> trades;
    std::map<int64_t, double> hourly;        // hour → P&L (funding + costs), fraction of notional
    std::map<int64_t, int> open;             // hour → 1 if a position was held over that hour's payments
    std::array<double, 3> venue_funding{};   // funding P&L by the venue it came from
    int64_t held_hours = 0, opposite_sign_hours = 0;
    double net() const { double n = 0; for (const auto& t : trades) n += t.funding - t.cost; return n; }
};

// Simulates one coin-pair over [t0, t1). Signal = trailing mean of (equiv_a − equiv_b) over hours (h−L, h], per year.
inline PairRun run_pair(const Coin& c, int a, int b, const Cell& cell, const Costs& costs, int64_t t0, int64_t t1) {
    PairRun r; r.coin = c.name; r.a = a; r.b = b;
    Equiv ea(c.v[a], a == BN), eb(c.v[b], b == BN);
    auto pay = [&](int v, int64_t h) { auto it = c.v[v].pay.find(h); return it == c.v[v].pay.end() ? 0.0 : it->second; };
    // trailing window over hours (h − L, h]
    std::vector<double> diffs;
    int64_t start = t0 - int64_t(cell.L) * kHour;
    int pos = 0;
    Trade cur;
    const double one_way = costs.one_way(a, b);
    double sum = 0; int valid = 0;
    std::vector<std::pair<bool, double>> win;   // (valid, diff) ring of the last L hours
    for (int64_t h = start + kHour; h < t1; h += kHour) {
        double da = ea.at(h), db = eb.at(h);
        bool ok = !std::isnan(da) && !std::isnan(db);
        win.push_back({ok, ok ? da - db : 0.0});
        if (ok) { sum += da - db; ++valid; }
        if (int(win.size()) > cell.L) { auto [o, d] = win.front(); win.erase(win.begin()); if (o) { sum -= d; --valid; } }
        if (h < t0) continue;
        // 1) payments stamped h accrue to a position chosen before h
        if (pos != 0) {
            double pa = pay(a, h), pb = pay(b, h);
            double f = pos * (pa - pb);
            cur.funding += f;
            r.hourly[h] += f;
            r.open[h] = 1;
            r.venue_funding[a] += pos * pa;
            r.venue_funding[b] -= pos * pb;
            ++r.held_hours;
            if (pa * pb < 0) ++r.opposite_sign_hours;
        }
        // 2) decide at h (after its payments)
        bool last = h + kHour >= t1;
        double S = valid * 2 >= cell.L ? sum / valid * kHoursYear : NAN;
        int want = pos;
        if (pos != 0 && (last || (!std::isnan(S) && (S * pos <= 0 || std::abs(S) < cell.theta / 2)))) want = 0;
        if (!last && want == 0 && !std::isnan(S) && std::abs(S) >= cell.theta) want = S > 0 ? 1 : -1;
        if (want != pos) {
            if (pos != 0) { cur.close = h; cur.cost += one_way; r.hourly[h] -= one_way; r.trades.push_back(cur); }
            if (want != 0) { cur = Trade{}; cur.open = h; cur.dir = want; cur.cost = one_way; r.hourly[h] -= one_way; }
            pos = want;
        }
    }
    return r;
}

struct Summary {
    double net = 0, apr_avg = NAN, apr_peak = NAN, avg_capital = 0, peak_capital = 0, avg_hold_h = 0;
    int round_trips = 0, coins_traded = 0, coins_positive = 0, blocks_positive = 0;
    double net_without_best3 = 0;
    std::vector<double> blocks;                      // 6 equal blocks
    std::map<std::string, double> by_coin, by_pair;
    std::array<double, 3> venue_funding{};
    double funding = 0, costs = 0, opposite_share = NAN;
};

inline Summary summarize(const std::vector<PairRun>& runs, const Costs& costs, int64_t t0, int64_t t1, int nblocks = 6) {
    Summary s;
    double hours = double(t1 - t0) / kHour;
    std::map<int64_t, int> concurrent;
    int64_t held = 0, opp = 0;
    double hold_sum = 0;
    s.blocks.assign(nblocks, 0.0);
    for (const auto& r : runs) {
        for (const auto& [h, n] : r.open) concurrent[h] += n;
        for (const auto& [h, p] : r.hourly) {
            int k = std::clamp(int(double(h - t0) / (t1 - t0) * nblocks), 0, nblocks - 1);
            s.blocks[k] += p;
        }
        for (const auto& t : r.trades) { s.funding += t.funding; s.costs += t.cost; hold_sum += double(t.close - t.open) / kHour; }
        s.round_trips += int(r.trades.size());
        double n = r.net();
        s.net += n;
        if (!r.trades.empty()) s.by_coin[r.coin] += n;
        s.by_pair[std::string(venue_name(r.a)) + "-" + venue_name(r.b)] += n;
        for (int v = 0; v < 3; ++v) s.venue_funding[v] += r.venue_funding[v];
        held += r.held_hours; opp += r.opposite_sign_hours;
    }
    int peak = 0; for (const auto& [h, n] : concurrent) peak = std::max(peak, n);
    s.avg_capital = double(held) * costs.capital() / hours;
    s.peak_capital = peak * costs.capital();
    double years = hours / kHoursYear;
    if (s.avg_capital > 0) s.apr_avg = s.net / s.avg_capital / years;
    if (s.peak_capital > 0) s.apr_peak = s.net / s.peak_capital / years;
    s.avg_hold_h = s.round_trips ? hold_sum / s.round_trips : 0;
    s.coins_traded = int(s.by_coin.size());
    for (const auto& [c, n] : s.by_coin) if (n > 0) ++s.coins_positive;
    for (double b : s.blocks) if (b > 0) ++s.blocks_positive;
    std::vector<double> nets; for (const auto& [c, n] : s.by_coin) nets.push_back(n);
    std::sort(nets.rbegin(), nets.rend());
    s.net_without_best3 = s.net; for (size_t i = 0; i < nets.size() && i < 3; ++i) s.net_without_best3 -= nets[i];
    if (held > 0) s.opposite_share = double(opp) / double(held);
    return s;
}

struct Verdict { bool apr = false, coins = false, blocks = false, best3 = false; bool pass() const { return apr && coins && blocks && best3; } };
inline Verdict judge(const Summary& s) {
    Verdict v;
    v.apr = s.apr_avg >= 0.10;
    v.coins = s.coins_traded > 0 && double(s.coins_positive) >= 0.6 * s.coins_traded;
    v.blocks = s.blocks_positive >= 4;
    v.best3 = s.net_without_best3 > 0;
    return v;
}

inline std::vector<PairRun> run_all(const std::vector<Coin>& coins, const Cell& cell, const Costs& costs, int64_t t0, int64_t t1) {
    std::vector<PairRun> out;
    const std::array<std::pair<int, int>, 3> pairs{{{LT, HL}, {LT, BN}, {HL, BN}}};
    for (const auto& c : coins)
        for (auto [a, b] : pairs)
            if (!c.v[a].pay.empty() && !c.v[b].pay.empty()) out.push_back(run_pair(c, a, b, cell, costs, t0, t1));
    return out;
}

// Largest intraday excursion from the open (|high/open − 1|, |low/open − 1|) on any day a position on the coin was open.
inline std::map<std::string, double> hold_risk(const std::vector<Coin>& coins, const std::vector<PairRun>& runs) {
    std::map<std::string, std::set<int64_t>> days;
    for (const auto& r : runs) for (const auto& [h, n] : r.open) days[r.coin].insert(h - h % 86400);
    std::map<std::string, double> out;
    for (const auto& c : coins) {
        auto it = days.find(c.name);
        if (it == days.end()) continue;
        double m = 0;
        for (int64_t d : it->second) {
            auto k = c.day.find(d);
            if (k == c.day.end() || k->second.o <= 0) continue;
            m = std::max({m, std::abs(k->second.h / k->second.o - 1), std::abs(k->second.l / k->second.o - 1)});
        }
        out[c.name] = m;
    }
    return out;
}

inline std::vector<Coin> load(const std::filesystem::path& root) {
    std::vector<Coin> coins;
    std::ifstream cf(root / "coins.txt");
    auto read = [](const std::filesystem::path& p) { std::ifstream f(p); return f ? json::parse(f) : json::array(); };
    for (std::string name; cf >> name;) {
        Coin c; c.name = name;
        c.v[LT] = parse_lighter(read(root / "lighter" / (name + ".json")));
        c.v[HL] = parse_plain(read(root / "hl" / (name + ".json")));
        c.v[BN] = parse_plain(read(root / "bn" / (name + ".json")));
        for (const auto& r : read(root / "candles" / (name + ".json")))
            c.day[r.at("t").get<int64_t>() - r.at("t").get<int64_t>() % 86400] = {num(r.at("o")), num(r.at("h")), num(r.at("l")), num(r.at("c"))};
        coins.push_back(std::move(c));
    }
    return coins;
}

}  // namespace flab
