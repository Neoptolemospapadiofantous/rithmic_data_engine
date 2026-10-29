// Tests for src/dex/funding_lab.hpp — the F0 funding-carry backtest (research/preregistered/2026-10-28_funding_carry.md).
#include "dex/funding_lab.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

static int tests_run = 0, tests_failed = 0;
#define TEST(name) void test_##name()
#define RUN(name) do { ++tests_run; \
    try { test_##name(); std::cout << "PASS " #name "\n"; } \
    catch (std::exception& e) { ++tests_failed; std::cout << "FAIL " #name ": " << e.what() << "\n"; } \
} while (0)
#define ASSERT(c) do { if (!(c)) throw std::runtime_error("Assert failed: " #c); } while (0)
#define ASSERT_EQ(a, b) do { if ((a) != (b)) throw std::runtime_error("ASSERT_EQ failed: " #a " != " #b); } while (0)
#define ASSERT_NEAR(a, b, e) do { if (std::abs((a) - (b)) > (e)) throw std::runtime_error("ASSERT_NEAR failed: " #a " vs " #b); } while (0)

using namespace flab;
static constexpr int64_t T0 = 1'761'609'600;   // a UTC midnight
static constexpr int64_t H = kHour;

// hourly series: rate(h) for hours [from, to)
template <class F> static Series hourly(int64_t from, int64_t to, F rate) { Series s; for (int64_t t = from; t < to; t += H) s.pay[t] = rate(t); return s; }

TEST(lighter_rate_is_percent_per_hour_signed_by_direction_and_duplicates_keep_the_first) {
    auto s = parse_lighter(json::parse(R"([{"t":1761609600,"rate":"0.0012","dir":"long"},{"t":1761613200,"rate":"0.0500","dir":"short"},
                                           {"t":1761613200,"rate":"9.9","dir":"long"},{"t":1761616799,"rate":"0.01","dir":"long"}])"));
    ASSERT_EQ(s.pay.size(), size_t{2});                        // 3rd and 4th rows fall in an hour already present
    ASSERT_NEAR(s.pay.at(1761609600), 0.000012, 1e-12);
    ASSERT_NEAR(s.pay.at(1761613200), -0.0005, 1e-12);          // shorts pay; the later duplicate is dropped
    ASSERT(s.pay.count(1761613200) && !s.pay.count(1761616799));  // floored to the hour (and that hour already exists)
    auto p = parse_plain(json::parse(R"([{"t":1761609600,"rate":"0.0000125"},{"t":1761638400,"rate":-0.0001}])"));
    ASSERT_NEAR(p.pay.at(1761609600), 0.0000125, 1e-15);
    ASSERT_NEAR(p.pay.at(1761638400), -0.0001, 1e-15);
}

TEST(binance_settlements_spread_to_hourly_equivalents_and_pay_only_at_settlement) {
    Series bn;
    for (int64_t t = T0; t < T0 + 10 * 8 * H; t += 8 * H) bn.pay[t] = 0.0008;   // 0.08 % per 8 h
    Equiv e(bn, true);
    ASSERT_NEAR(e.at(T0 + 8 * H + 3 * H), 0.0001, 1e-12);       // 0.0008 / 8 h
    ASSERT(std::isnan(e.at(T0 - H)));                            // nothing settled yet
    ASSERT(std::isnan(e.at(T0 + 9 * 8 * H + 25 * H)));           // stale > 24 h
    // carry vs a flat hourly venue: the position collects only at settlements
    Coin c; c.name = "X";
    c.v[HL] = hourly(T0 - 100 * H, T0 + 200 * H, [](int64_t) { return 0.0; });
    for (int64_t t = T0 - 96 * H; t < T0 + 200 * H; t += 8 * H) c.v[BN].pay[t] = 0.0008;   // ≈ 87.6 % a year to shorts
    Costs k;
    auto r = run_pair(c, HL, BN, {24, 0.2}, k, T0, T0 + 160 * H);
    ASSERT_EQ(r.trades.size(), size_t{1});
    ASSERT_EQ(r.trades[0].dir, -1);                              // S = HL − BN < 0 → long HL, short BN
    int settlements = 0;
    for (int64_t t = r.trades[0].open + H; t <= r.trades[0].close; t += H) if (c.v[BN].pay.count(t)) ++settlements;
    ASSERT(settlements > 0);
    ASSERT_NEAR(r.trades[0].funding, settlements * 0.0008, 1e-12);
}

TEST(constant_gap_is_collected_every_hour_and_costs_are_charged_on_entry_and_exit) {
    Coin c; c.name = "X";
    c.v[LT] = hourly(T0 - 100 * H, T0 + 100 * H, [](int64_t) { return 0.0; });
    c.v[HL] = hourly(T0 - 100 * H, T0 + 100 * H, [](int64_t) { return 0.0001; });   // HL longs pay 0.01 %/h ≈ 87.6 %/yr
    Costs k;
    auto r = run_pair(c, LT, HL, {24, 0.2}, k, T0, T0 + 50 * H);
    ASSERT_EQ(r.trades.size(), size_t{1});
    const auto& t = r.trades[0];
    ASSERT_EQ(t.dir, -1);                                        // long Lighter, short Hyperliquid
    ASSERT_EQ(t.open, T0);                                       // signal is already known at the first hour
    ASSERT_EQ(t.close, T0 + 49 * H);                             // forced flat at the end of the half
    ASSERT_NEAR(t.funding, 49 * 0.0001, 1e-12);                  // payments stamped (open, close]
    ASSERT_NEAR(t.cost, 2 * k.one_way(LT, HL), 1e-15);
    ASSERT_NEAR(k.one_way(LT, HL), (0 + 4.5 + 2 * 2 + 2.5) / 1e4, 1e-15);
    ASSERT_NEAR(r.venue_funding[HL], 49 * 0.0001, 1e-12);
    ASSERT_NEAR(r.net(), t.funding - t.cost, 1e-15);
}

TEST(signal_never_sees_a_payment_before_it_is_stamped) {
    Coin c; c.name = "X";
    const int64_t jump = T0 + 30 * H;
    c.v[LT] = hourly(T0 - 100 * H, T0 + 100 * H, [](int64_t) { return 0.0; });
    c.v[HL] = hourly(T0 - 100 * H, T0 + 100 * H, [&](int64_t t) { return t >= jump ? 0.01 : 0.0; });   // huge from `jump` on
    auto r = run_pair(c, LT, HL, {8, 0.2}, Costs{}, T0, T0 + 60 * H);
    ASSERT(!r.trades.empty());
    ASSERT(r.trades[0].open >= jump);                            // cannot enter before the first big payment exists
    ASSERT(r.trades[0].funding > 0);
    for (const auto& [h, p] : r.hourly) ASSERT(h >= T0 && h < T0 + 60 * H);
}

TEST(exit_on_flip_and_on_fade_below_half_the_entry_threshold) {
    Coin c; c.name = "X";
    c.v[LT] = hourly(T0 - 100 * H, T0 + 200 * H, [](int64_t) { return 0.0; });
    // +0.0001/h (87.6 %/yr) until T0+40h, then −0.0001/h: the position must flip direction
    c.v[HL] = hourly(T0 - 100 * H, T0 + 200 * H, [](int64_t t) { return t < T0 + 40 * H ? 0.0001 : -0.0001; });
    auto r = run_pair(c, LT, HL, {8, 0.2}, Costs{}, T0, T0 + 120 * H);
    ASSERT(r.trades.size() >= 2);
    ASSERT_EQ(r.trades[0].dir, -1);
    ASSERT_EQ(r.trades[1].dir, 1);
    ASSERT(r.trades[0].close > T0 + 40 * H && r.trades[0].close <= T0 + 48 * H);
    // a gap that fades to 30 %/yr (< θ/2 = 40 % of an 80 % threshold) exits
    Coin d = c;
    d.v[HL] = hourly(T0 - 100 * H, T0 + 200 * H, [](int64_t t) { return t < T0 + 40 * H ? 0.0002 : 0.0000342; });
    auto q = run_pair(d, LT, HL, {8, 0.8}, Costs{}, T0, T0 + 120 * H);
    ASSERT_EQ(q.trades.size(), size_t{1});
    ASSERT(q.trades[0].close < T0 + 60 * H);
}

TEST(no_trade_when_the_gap_is_below_threshold_or_data_is_missing) {
    Coin c; c.name = "X";
    c.v[LT] = hourly(T0 - 100 * H, T0 + 100 * H, [](int64_t) { return 0.0; });
    c.v[HL] = hourly(T0 - 100 * H, T0 + 100 * H, [](int64_t) { return 0.00001; });   // 8.8 %/yr < 10 %
    ASSERT(run_pair(c, LT, HL, {24, 0.1}, Costs{}, T0, T0 + 80 * H).trades.empty());
    Coin m; m.name = "M";
    m.v[LT] = hourly(T0 - 100 * H, T0 + 100 * H, [](int64_t) { return 0.0; });
    ASSERT(run_all({m}, {24, 0.1}, Costs{}, T0, T0 + 80 * H).empty());                // a venue without data: no pair
}

TEST(summary_capital_apr_blocks_and_verdict) {
    Coin c; c.name = "A";
    c.v[LT] = hourly(T0 - 200 * H, T0 + 400 * H, [](int64_t) { return 0.0; });
    c.v[HL] = hourly(T0 - 200 * H, T0 + 400 * H, [](int64_t) { return 0.0001; });
    Costs k;
    const int64_t t1 = T0 + 300 * H;
    auto runs = std::vector<PairRun>{run_pair(c, LT, HL, {24, 0.2}, k, T0, t1)};
    auto s = summarize(runs, k, T0, t1);
    ASSERT_EQ(s.round_trips, 1);
    ASSERT_NEAR(s.avg_capital, 299.0 * k.capital() / 300.0, 1e-9);   // held 299 of 300 hours
    ASSERT_NEAR(s.peak_capital, k.capital(), 1e-12);
    double years = 300.0 / kHoursYear;
    ASSERT_NEAR(s.apr_avg, s.net / s.avg_capital / years, 1e-9);
    ASSERT_EQ(int(s.blocks.size()), 6);
    double bsum = 0; for (double b : s.blocks) bsum += b;
    ASSERT_NEAR(bsum, s.net, 1e-12);
    ASSERT_EQ(s.coins_traded, 1);
    ASSERT_EQ(s.coins_positive, 1);
    ASSERT_NEAR(s.net_without_best3, 0.0, 1e-12);                    // the only coin is the best one
    auto v = judge(s);
    ASSERT(v.apr && v.coins && v.blocks);
    ASSERT(!v.best3 && !v.pass());                                    // carried by its best coins → not a pass
}

int main() {
    RUN(lighter_rate_is_percent_per_hour_signed_by_direction_and_duplicates_keep_the_first);
    RUN(binance_settlements_spread_to_hourly_equivalents_and_pay_only_at_settlement);
    RUN(constant_gap_is_collected_every_hour_and_costs_are_charged_on_entry_and_exit);
    RUN(signal_never_sees_a_payment_before_it_is_stamped);
    RUN(exit_on_flip_and_on_fade_below_half_the_entry_threshold);
    RUN(no_trade_when_the_gap_is_below_threshold_or_data_is_missing);
    RUN(summary_capital_apr_blocks_and_verdict);
    std::cout << (tests_run - tests_failed) << "/" << tests_run << " passed\n";
    return tests_failed ? 1 : 0;
}
