/*  test_mm_lab.cpp — the market-making research judge (src/dex/mm_lab.hpp): idea parsing and refusals, feature
    causality, rule actions on the simulator's quotes, the registry and its rising bar, holdout sealing, the baseline
    against mm_sim's own procedure, determinism. Synthetic archives are written to a temp dir; one test also replays
    a real Lighter coin-day when the archive drive is mounted (skipped otherwise). No network, no DB.            */
#include "dex/mm_lab.hpp"

#include <unistd.h>

#include <cmath>
#include <cstring>
#include <filesystem>
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

using namespace mmlab;
namespace fs = std::filesystem;

static const fs::path REPO = fs::path(__FILE__).parent_path().parent_path().parent_path();
static fs::path tmpdir(const std::string& tag) {
    fs::path d = fs::temp_directory_path() / ("test_mm_lab_" + tag + "_" + std::to_string(::getpid()));
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}
static Env test_env(const fs::path& root) { Env e; e.root = root; e.params_dir = root / "params"; return e; }
static std::optional<Idea> parse(const std::string& s, std::string* err_out = nullptr) {
    std::string err;
    Env e;
    auto I = parse_idea(json::parse(s), e, err);
    if (err_out) *err_out = err;
    return I;
}
static bool same(double a, double b) { return (std::isnan(a) && std::isnan(b)) || a == b; }

// ── synthetic market ────────────────────────────────────────────────────────────────────────────────────────────
struct Lcg { uint64_t s; double u() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return static_cast<double>(s >> 11) / 9007199254740992.0; } };

static hl::Book mkbook(int64_t t, double bid, double ask, double bsz = 5, double asz = 5, double tick = 1) {
    hl::Book b; b.t_ms = t;
    for (int i = 0; i < 5; ++i) { b.bids.push_back({bid - i * tick, bsz + i}); b.asks.push_back({ask + i * tick, asz + i}); }
    return b;
}
static hl::Event ebook(const hl::Book& b, int64_t r = 0) { hl::Event e; e.t_ms = b.t_ms; e.r_ms = r ? r : b.t_ms; e.is_book = true; e.book = b; return e; }
static hl::Event etrade(int64_t t, double px, double sz, bool buy, int64_t r = 0) { hl::Event e; e.t_ms = t; e.r_ms = r ? r : t; e.trade = {"", t, px, sz, buy}; return e; }

// A random-walk market on a 0.01 grid around 100: a book every ~2 s, a trade every ~6 s (some through the touch).
static std::vector<hl::Event> synth_events(int64_t t0, int64_t t1, uint64_t seed, double crash_from_frac = 2) {
    std::vector<hl::Event> ev;
    Lcg g{seed};
    double mid = 100;
    int64_t seq = 0;
    for (int64_t t = t0; t < t1; t += 1500 + static_cast<int64_t>(g.u() * 1000)) {
        mid += (g.u() - 0.5) * 0.06;
        if (static_cast<double>(t - t0) / (t1 - t0) >= crash_from_frac) mid *= 0.999;    // an injected crash
        const double c = std::round(mid * 100) / 100;
        const double hs = (2 + std::floor(g.u() * 3)) * 0.01;
        hl::Book b = mkbook(t, c - hs, c + hs, 2 + g.u() * 20, 2 + g.u() * 20, 0.01);
        auto e = ebook(b, t + 40); e.seq = seq++; ev.push_back(e);
        if (g.u() < 0.35) {
            const bool buy = g.u() < 0.5;
            const double through = g.u() < 0.4 ? std::floor(g.u() * 8) * 0.01 : 0;
            auto tr = etrade(t + 1, buy ? b.asks[0].px + through : b.bids[0].px - through, 0.5 + g.u() * 3, buy, t + 41);
            tr.seq = seq++; ev.push_back(tr);
        }
    }
    hl::sort_events(ev);
    return ev;
}
static std::string level_json(const std::vector<hl::Level>& v) {
    std::string s = "[";
    for (size_t i = 0; i < v.size(); ++i) { char b[96]; std::snprintf(b, sizeof b, "%s{\"px\":\"%.2f\",\"sz\":\"%.4f\",\"n\":1}", i ? "," : "", v[i].px, v[i].sz); s += b; }
    return s + "]";
}
// Write events as a Hyperliquid-format live archive (hl_events.hpp reads it for any venue directory).
static void write_archive(const fs::path& dir, const std::vector<hl::Event>& ev) {
    ZstdDaily z(dir);
    for (const auto& e : ev) {
        std::string m;
        char b[256];
        if (e.is_book) m = "{\"channel\":\"l2Book\",\"data\":{\"coin\":\"XC\",\"time\":" + std::to_string(e.t_ms) + ",\"levels\":[" + level_json(e.book.bids) + "," + level_json(e.book.asks) + "]}}";
        else { std::snprintf(b, sizeof b, "{\"channel\":\"trades\",\"data\":[{\"coin\":\"XC\",\"side\":\"%s\",\"px\":\"%.2f\",\"sz\":\"%.4f\",\"time\":%lld}]}",
                             e.trade.buy_aggr ? "B" : "A", e.trade.px, e.trade.sz, static_cast<long long>(e.t_ms)); m = b; }
        z.write_line("{\"r\":" + std::to_string(e.r_ms * 1000) + ",\"m\":" + m + "}", e.t_ms * 1000);
    }
}
static const int64_t DAY0 = 1767225600000LL;    // 2026-01-01 00:00 UTC
// Five days of venue "extended", coin XC, plus a Hyperliquid archive of the same coin; optional crash in days 4-5.
static fs::path synth_root(const std::string& tag, bool crash_late) {
    fs::path root = tmpdir(tag);
    for (int d = 0; d < 5; ++d) {
        const int64_t a = DAY0 + d * 86'400'000LL + 3'600'000, b = a + 10 * 3'600'000LL;
        write_archive(root / "extended" / "XC", synth_events(a, b, 1000 + d, crash_late && d >= 3 ? 0.2 : 2));
        write_archive(root / "hyperliquid" / "XC", synth_events(a, b, 5000 + d));
        write_archive(root / "lighter" / "XC", synth_events(a, b, 1000 + d));    // a rate-limited venue: two coins
        write_archive(root / "lighter" / "YC", synth_events(a, b, 3000 + d));
    }
    fs::create_directories(root / "params");
    std::ofstream(root / "params" / "params_lighter.json") << R"({"venue":"lighter","maker_bps":0,"taker_bps":0,"latency_ms":300,"order_usd":100,"max_inv":3,
      "coins":{"XC":{"strategy":"frozen","half_width_ticks":5,"min_spread_ticks":4,"imb_pull":0.6,"pause_move_ticks":2,"pause_ms":2000,"requote_ticks":1,
      "venue_tick":0.01,"tick_rule":"book","hl_sz_decimals":0},"YC":{"strategy":"frozen","half_width_ticks":5,"min_spread_ticks":4,"imb_pull":0.6,
      "pause_move_ticks":2,"pause_ms":2000,"requote_ticks":1,"venue_tick":0.01,"tick_rule":"book","hl_sz_decimals":0}}})";
    std::ofstream(root / "params" / "params_extended.json") << R"({"venue":"extended","maker_bps":0,"taker_bps":2.5,"latency_ms":100,"order_usd":100,"max_inv":3,
      "coins":{"XC":{"strategy":"frozen","half_width_ticks":5,"min_spread_ticks":4,"imb_pull":0.6,"pause_move_ticks":2,"pause_ms":2000,"requote_ticks":1,
      "venue_tick":0.01,"tick_rule":"book","hl_sz_decimals":0}}})";
    return root;
}
static Options synth_opts(const fs::path& root, const std::string& reg = "reg.jsonl") {
    Options o; o.env = test_env(root); o.registry = root / reg; o.pinned_registry = o.registry; o.out_dir = root / "out";
    o.boundaries = root / ("bounds_" + reg + ".json");
    o.asof_ms = DAY0 + 5 * 86'400'000LL - 1; o.threads = 2; o.now_iso = "2026-10-26T00:00:00Z";
    return o;
}
static const char* SYN_IDEAS = R"({"ideas":[
  {"id":"syn_width_4","venue":"extended","params":{"half_width_ticks":4}},
  {"id":"syn_flow","venue":"extended","rules":[{"when":[["trade_flow_imbalance@20000",">",0.3]],"action":"pull_ask"}]},
  {"id":"syn_anchor","venue":"extended","fair":{"type":"hl_anchor","k":0.5}}]})";

// ── parsing and refusals ────────────────────────────────────────────────────────────────────────────────────────
TEST(minimal_idea_parses_with_frozen_defaults) {
    auto I = parse(R"({"id":"abc_1","venue":"lighter"})");
    ASSERT(I.has_value());
    ASSERT(I->plain());
    ASSERT(I->markets.empty());
    ASSERT_EQ(I->size_usd, 0.0);
    ASSERT(!I->needs_hl());
    ASSERT_EQ(I->rate_per_min, 60);                                    // Lighter: the Standard limit is always on
    ASSERT_EQ(I->rate_accounts, 1);
    ASSERT_EQ(parse(R"({"id":"abc_1","venue":"extended"})")->rate_per_min, 0);   // not modelled there
    ASSERT_EQ(parse(R"({"id":"abc_1","venue":"lighter","rate_limit":{"per_min":20,"accounts":3}})")->rate_accounts, 3);
}
TEST(unknown_field_feature_param_op_action_are_refused) {
    std::string e;
    ASSERT(!parse(R"({"id":"abc","venue":"lighter","colour":"red"})", &e)); ASSERT(e.find("unknown field") != std::string::npos);
    ASSERT(!parse(R"({"id":"abc","venue":"lighter","rules":[{"when":[["moon_phase",">",1]],"action":"pull_bid"}]})", &e)); ASSERT(e.find("unknown feature") != std::string::npos);
    ASSERT(!parse(R"({"id":"abc","venue":"lighter","params":{"magic":1}})", &e)); ASSERT(e.find("unknown params field") != std::string::npos);
    ASSERT(!parse(R"({"id":"abc","venue":"lighter","rules":[{"when":[["spread_bps","~",1]],"action":"pull_bid"}]})", &e)); ASSERT(e.find("unknown op") != std::string::npos);
    ASSERT(!parse(R"({"id":"abc","venue":"lighter","rules":[{"when":[["spread_bps",">",1]],"action":"buy_everything"}]})", &e)); ASSERT(e.find("unknown action") != std::string::npos);
    ASSERT(!parse(R"({"id":"abc","venue":"lighter","rules":[{"when":[["spread_bps",">",1]],"action":"skew_ticks"}]})", &e)); ASSERT(e.find("needs a numeric") != std::string::npos);
    ASSERT(!parse(R"({"id":"abc","venue":"mars"})", &e)); ASSERT(e.find("unknown venue") != std::string::npos);
    ASSERT(!parse(R"({"id":"Bad-Id","venue":"lighter"})", &e));
    ASSERT(!parse(R"({"id":"abc","venue":"lighter","fair":{"type":"psychic"}})", &e)); ASSERT(e.find("unknown fair.type") != std::string::npos);
}
TEST(evaluation_owned_fields_and_optimistic_settings_are_refused) {
    std::string e;
    ASSERT(!parse(R"({"id":"abc","venue":"lighter","params":{"maker_fee_bps":-1}})", &e)); ASSERT(e.find("fixed by the evaluation") != std::string::npos);
    ASSERT(!parse(R"({"id":"abc","venue":"lighter","params":{"fills":"legacy"}})", &e)); ASSERT(e.find("fixed by the evaluation") != std::string::npos);
    ASSERT(!parse(R"({"id":"abc","venue":"lighter","latency":{"place_ms":5,"cancel_ms":445}})", &e)); ASSERT(e.find("faster") != std::string::npos);
    ASSERT(!parse(R"({"id":"abc","venue":"lighter","rate_limit":{"per_min":600}})", &e));
    ASSERT(!parse(R"({"id":"abc","venue":"extended","rate_limit":{"per_min":10}})", &e)); ASSERT(e.find("not modelled") != std::string::npos);
    ASSERT(parse(R"({"id":"abc","venue":"lighter","latency":{"place_ms":200,"cancel_ms":500},"rate_limit":{"per_min":30,"accounts":2}})"));
}
TEST(funding_and_cross_venue_on_history_are_refused) {
    std::string e;
    ASSERT(!parse(R"({"id":"abc","venue":"lighter","rules":[{"when":[["funding",">",0]],"action":"pull_bid"}]})", &e)); ASSERT(e.find("unavailable") != std::string::npos);
    ASSERT(!parse(R"({"id":"abc","venue":"hyperliquid_hist","rules":[{"when":[["hl_basis_bps",">",2]],"action":"pull_bid"}]})", &e)); ASSERT(e.find("Hyperliquid") != std::string::npos);
    ASSERT(!parse(R"({"id":"abc","venue":"hyperliquid_hist","fair":{"type":"hl_anchor","k":1}})", &e)); ASSERT(e.find("Hyperliquid") != std::string::npos);
}
TEST(windows_and_parameter_references_parse) {
    auto I = parse(R"({"id":"abc","venue":"lighter","rules":[{"when":[["trade_flow_imbalance@2500",">",0.5],["hl_basis_ticks","<=","-0.5*half_width_ticks"]],"action":"pull_ask","hold_ms":300}]})");
    ASSERT(I.has_value());
    ASSERT_EQ(I->rules[0].when[0].window, int64_t{2500});
    ASSERT_EQ(I->rules[0].when[0].key, std::string("trade_flow_imbalance@2500"));
    ASSERT_EQ(I->rules[0].when[1].ref, std::string("half_width_ticks"));
    ASSERT_NEAR(I->rules[0].when[1].ref_mult, -0.5, 1e-12);
    ASSERT_EQ(I->rules[0].hold_ms, int64_t{300});
    ASSERT(I->needs_hl());
    auto D = parse(R"({"id":"abc","venue":"lighter","rules":[{"when":[["trades_per_min",">",3]],"action":"pull_both"}]})");
    ASSERT_EQ(D->rules[0].when[0].window, int64_t{60000});          // the default window
    std::string e;
    ASSERT(!parse(R"({"id":"abc","venue":"lighter","rules":[{"when":[["spread_bps@1000",">",1]],"action":"pull_both"}]})", &e)); ASSERT(e.find("no @window") != std::string::npos);
    ASSERT(!parse(R"({"id":"abc","venue":"lighter","rules":[{"when":[["mid_move_bps@5",">",1]],"action":"pull_both"}]})", &e));
    ASSERT(!parse(R"({"id":"abc","venue":"lighter","rules":[{"when":[["spread_bps",">","order_book"]],"action":"pull_both"}]})", &e));
}
TEST(spec_hash_ignores_id_and_prose_but_not_the_strategy) {
    auto a = parse(R"({"id":"one","venue":"lighter","params":{"half_width_ticks":4},"why":"x"})");
    auto b = parse(R"({"id":"two","venue":"lighter","params":{"half_width_ticks":4},"why":"y","notes":"z"})");
    auto c = parse(R"({"id":"one","venue":"lighter","params":{"half_width_ticks":3}})");
    ASSERT_EQ(a->hash, b->hash);
    ASSERT(a->hash != c->hash);
}
TEST(neighbours_move_every_knob_one_step_both_ways) {
    auto I = parse(R"({"id":"abc","venue":"lighter","params":{"half_width_ticks":4,"imb_pull":0.6},"size_usd":20,
                      "rules":[{"when":[["trade_flow_imbalance@4000",">",0.5],["hl_basis_ticks","<=","-half_width_ticks"]],"action":"widen_ticks","value":1,"hold_ms":1000}]})");
    Env e;
    auto n = neighbours(*I, *venue_of("lighter", e));
    // 2 params ×2 + size ×2 + threshold ×2 + window ×2 + reference multiplier ×2 + action value ×2 + hold ×2 = 16
    ASSERT_EQ(n.size(), size_t{16});
    std::set<std::string> labels;
    for (auto& [l, s] : n) { labels.insert(l); std::string err; ASSERT(parse_idea(s, e, err).has_value()); }
    ASSERT(labels.count("params.half_width_ticks=3") && labels.count("params.half_width_ticks=5"));
    ASSERT(labels.count("rules[0].when[0].trade_flow_imbalance@2000") && labels.count("rules[0].when[0].trade_flow_imbalance@8000"));
    ASSERT(labels.count("rules[0].when[1].hl_basis_ticks=-0.75*half_width_ticks"));
    ASSERT(labels.count("size_usd=16") && labels.count("size_usd=25"));
}

// ── features ────────────────────────────────────────────────────────────────────────────────────────────────────
static std::set<std::string> all_own_keys() {
    return {"spread_bps", "spread_ticks", "book_imbalance", "book_imbalance5", "trade_flow_imbalance@5000", "trades_per_min@60000",
            "realized_vol_bps@60000", "mid_move_bps@1000", "time_of_day_utc", "weekday"};
}
static hl::Params tick1() { hl::Params p; p.venue_tick = 0.01; p.strat_tick_rule = hl::TickRule::Book; return p; }

TEST(own_features_never_see_the_future) {
    auto ev = synth_events(DAY0, DAY0 + 3'600'000, 7);
    const int64_t cut = DAY0 + 1'800'000;
    DayData d0 = compact(ev, 20, 0, cut + 1);
    // the same history plus a future spike: a huge trade and a book at twice the price, after `cut`
    auto ev2 = ev;
    ev2.push_back(etrade(cut + 500, 250, 1e6, true));
    ev2.push_back(ebook(mkbook(cut + 600, 199, 201, 1e5, 1)));
    hl::sort_events(ev2);
    DayData d1 = compact(ev2, 20, 0, INT64_MAX);
    const Frame f0 = compute_frame(d0, nullptr, all_own_keys(), tick1()), f1 = compute_frame(d1, nullptr, all_own_keys(), tick1());
    ASSERT_EQ(f0.keys, f1.keys);
    size_t checked = 0;
    for (size_t i = 0; i < d0.ev.size(); ++i) {
        ASSERT_EQ(d0.ev[i].t, d1.ev[i].t);
        for (size_t k = 0; k < f0.keys.size(); ++k) { if (!same(f0.cols[k][i], f1.cols[k][i])) throw std::runtime_error("feature " + f0.keys[k] + " changed at a past event"); ++checked; }
    }
    ASSERT(checked > 1000);
    // and the spike IS visible once it has happened
    const int c = f1.col("trade_flow_imbalance@5000");
    bool seen = false;
    for (size_t i = 0; i < d1.ev.size(); ++i) if (d1.ev[i].t >= cut + 500 && d1.ev[i].t < cut + 4000 && f1.cols[c][i] > 0.99) seen = true;
    ASSERT(seen);
}
TEST(cross_venue_features_cut_at_the_decision_time_and_never_see_the_future) {
    // venue books every second for 3 minutes (we receive them 100 ms late); Hyperliquid books every second, received
    // 130 ms after each venue book's exchange time
    std::vector<hl::Event> ev;
    std::vector<HlMid> hlm;
    for (int s = 0; s < 180; ++s) {
        const int64_t t = DAY0 + s * 1000;
        ev.push_back(ebook(mkbook(t, 99.99, 100.01, 5, 5, 0.01), t + 100));
        hlm.push_back({t + 130, 100.05});
    }
    DayData d = compact(ev, 20, 0, INT64_MAX);
    const std::set<std::string> keys = {"hl_basis_bps", "hl_basis_ticks", "cross_venue_lead_bps@1000"};
    const Frame f0 = compute_frame(d, &hlm, keys, tick1());
    // a Hyperliquid jump received at t(120) + 50 ms — BEFORE we received the venue event (t + 100) but AFTER its
    // decision time t — must not reach that event (the review's look-ahead), nor anything before it
    auto hl2 = hlm;
    for (auto& h : hl2) if (h.r >= DAY0 + 120'000 + 50) h.mid = 110.0;   // the jump persists in every later book
    hl2.push_back({DAY0 + 120'000 + 50, 110.0});
    std::stable_sort(hl2.begin(), hl2.end(), [](const HlMid& a, const HlMid& b) { return a.r < b.r; });
    const Frame f1 = compute_frame(d, &hl2, keys, tick1());
    for (size_t i = 0; i <= 120; ++i) for (size_t k = 0; k < keys.size(); ++k) ASSERT(same(f0.cols[k][i], f1.cols[k][i]));
    // the basis needs 60 one-second samples: unavailable before, available after
    const int b = f0.col("hl_basis_bps");
    ASSERT(std::isnan(f0.cols[b][30]));
    ASSERT(std::isfinite(f0.cols[b][100]));
    ASSERT_NEAR(f0.cols[b][100], 0.0, 1e-9);                            // a constant basis is removed by its own median
    // from s = 121 (decision time ≥ the jump's receive time) it is known: our mid far BELOW → negative basis, positive lead
    ASSERT(f1.cols[b][121] < -500);
    ASSERT(f1.cols[f1.col("cross_venue_lead_bps@1000")][121] > 900);
    // stale Hyperliquid (> 5 s between its last book and the decision time) → unavailable
    std::vector<HlMid> early(hlm.begin(), hlm.begin() + 100);
    const Frame f2 = compute_frame(d, &early, keys, tick1());
    ASSERT(std::isfinite(f2.cols[b][100]));
    ASSERT(std::isnan(f2.cols[b][110]));
}
TEST(cross_venue_features_ignore_a_non_monotone_venue_receive_clock) {
    // the venue's receive times jump around (reconnects, bursts): the features must depend only on the decision time
    std::vector<hl::Event> ev_r, ev_t;
    std::vector<HlMid> hlm;
    Lcg g{42};
    for (int s = 0; s < 200; ++s) {
        const int64_t t = DAY0 + s * 1000;
        const int64_t r = t + static_cast<int64_t>(g.u() * 3000) - 500;          // non-monotone, sometimes before t
        ev_r.push_back(ebook(mkbook(t, 99.99 + 0.01 * (s % 3), 100.03, 5, 5, 0.01), r));
        ev_t.push_back(ebook(mkbook(t, 99.99 + 0.01 * (s % 3), 100.03, 5, 5, 0.01), t));
        hlm.push_back({t + 400, 100.0 + 0.02 * (s % 5)});
    }
    DayData dr = compact(ev_r, 20, 0, INT64_MAX), dt = compact(ev_t, 20, 0, INT64_MAX);
    bool non_monotone = false;
    for (size_t i = 1; i < dr.ev.size(); ++i) non_monotone |= dr.ev[i].r < dr.ev[i - 1].r;
    ASSERT(non_monotone);
    const std::set<std::string> keys = {"hl_basis_bps", "hl_basis_ticks", "cross_venue_lead_bps@2000"};
    const Frame a = compute_frame(dr, &hlm, keys, tick1()), b = compute_frame(dt, &hlm, keys, tick1());
    size_t finite = 0;
    for (size_t k = 0; k < keys.size(); ++k)
        for (size_t i = 0; i < dr.ev.size(); ++i) { ASSERT(same(a.cols[k][i], b.cols[k][i])); finite += std::isfinite(a.cols[k][i]); }
    ASSERT(finite > 300);
    // and no event ever used a Hyperliquid book received after its own exchange time: shift every HL book 1 ms later
    // than an event's t and it is not seen by that event
    std::vector<HlMid> late;
    for (int s = 0; s < 200; ++s) late.push_back({DAY0 + s * 1000 + 1, 100.0});
    const Frame c = compute_frame(dt, &late, {"cross_venue_lead_bps@2000"}, tick1());
    ASSERT(std::isnan(c.cols[0][2]));                                     // at s = 2 only books received ≤ t(2) exist: 2 of them, window not covered
}
TEST(flow_rate_and_move_features_compute_the_documented_values) {
    std::vector<hl::Event> ev;
    ev.push_back(ebook(mkbook(DAY0, 99, 101)));
    ev.push_back(etrade(DAY0 + 6000, 101, 3, true));       // $303 bought
    ev.push_back(etrade(DAY0 + 7000, 99, 1, false));       // $99 sold
    ev.push_back(ebook(mkbook(DAY0 + 8000, 100, 102)));    // mid 100 → 101
    ev.push_back(etrade(DAY0 + 20000, 101, 1, false));
    DayData d = compact(ev, 20, 0, INT64_MAX);
    hl::Params p; p.venue_tick = 1;
    const Frame f = compute_frame(d, nullptr, {"trade_flow_imbalance@5000", "trades_per_min@5000", "mid_move_bps@1000", "spread_ticks", "book_imbalance"}, p);
    const int fl = f.col("trade_flow_imbalance@5000"), tp = f.col("trades_per_min@5000"), mm = f.col("mid_move_bps@1000");
    ASSERT(std::isnan(f.cols[fl][0]));                                  // warm-up: the window has not elapsed
    ASSERT_NEAR(f.cols[fl][2], (303.0 - 99.0) / 402.0, 1e-12);
    ASSERT_NEAR(f.cols[tp][2], 2 * 60.0 / 5.0, 1e-12);
    ASSERT_NEAR(f.cols[mm][3], 100.0, 1e-9);                           // +1 on 100 = 100 bps
    ASSERT_NEAR(f.cols[fl][4], -1.0, 1e-12);                            // only the last sale is in the window
    ASSERT_NEAR(f.cols[f.col("spread_ticks")][0], 2.0, 1e-12);
    ASSERT_NEAR(f.cols[f.col("book_imbalance")][0], 0.0, 1e-12);
}

// ── rules act on the quotes ─────────────────────────────────────────────────────────────────────────────────────
static hl::Params rule_params() {
    hl::Params p; p.order_usd = 100; p.max_inv_units = 3; p.maker_fee_bps = 0; p.taker_fee_bps = 0; p.latency_ms = 0;
    p.venue_tick = 1; p.strat_tick_rule = hl::TickRule::Book; p.half_width_ticks = 0.5; p.fills = hl::FillModel::NoCross;
    return p;
}
struct Quotes { double bid, ask; };
static Quotes quotes_after(const std::string& idea_json, const std::vector<hl::Event>& ev, size_t steps) {
    static Idea keep;
    keep = *parse(idea_json);
    static DayData d; d = compact(ev, 20, 0, INT64_MAX);
    static Frame F; F = compute_frame(d, nullptr, keep.frame_keys(), rule_params());
    CoinRun cr(keep, rule_params(), d, F);
    for (size_t i = 0; i < steps; ++i) cr.step(i);
    return {cr.sim().quote_px(true), cr.sim().quote_px(false)};
}
TEST(pull_rules_remove_exactly_the_named_side) {
    std::vector<hl::Event> ev = {ebook(mkbook(DAY0, 95, 105))};
    auto q0 = quotes_after(R"({"id":"abc","venue":"lighter"})", ev, 1);
    ASSERT_EQ(q0.bid, 99.0); ASSERT_EQ(q0.ask, 101.0);
    auto q1 = quotes_after(R"({"id":"abc","venue":"lighter","rules":[{"when":[["spread_bps",">",0]],"action":"pull_bid"}]})", ev, 1);
    ASSERT_EQ(q1.bid, 0.0); ASSERT_EQ(q1.ask, 101.0);
    auto q2 = quotes_after(R"({"id":"abc","venue":"lighter","rules":[{"when":[["spread_bps",">",0]],"action":"pull_ask"}]})", ev, 1);
    ASSERT_EQ(q2.bid, 99.0); ASSERT_EQ(q2.ask, 0.0);
    auto q3 = quotes_after(R"({"id":"abc","venue":"lighter","rules":[{"when":[["spread_bps",">",0]],"action":"pull_both"}]})", ev, 1);
    ASSERT_EQ(q3.bid, 0.0); ASSERT_EQ(q3.ask, 0.0);
    // a condition that does not hold changes nothing
    auto q4 = quotes_after(R"({"id":"abc","venue":"lighter","rules":[{"when":[["spread_bps","<",0]],"action":"pull_both"}]})", ev, 1);
    ASSERT_EQ(q4.bid, 99.0); ASSERT_EQ(q4.ask, 101.0);
    // all conditions must hold (AND)
    auto q5 = quotes_after(R"({"id":"abc","venue":"lighter","rules":[{"when":[["spread_bps",">",0],["book_imbalance",">",0.5]],"action":"pull_both"}]})", ev, 1);
    ASSERT_EQ(q5.bid, 99.0);
}
TEST(skew_and_widen_move_the_quotes_by_the_stated_ticks) {
    std::vector<hl::Event> ev = {ebook(mkbook(DAY0, 95, 105))};
    auto s = quotes_after(R"({"id":"abc","venue":"lighter","rules":[{"when":[["spread_bps",">",0]],"action":"skew_ticks","value":2}]})", ev, 1);
    ASSERT_EQ(s.bid, 101.0); ASSERT_EQ(s.ask, 103.0);
    auto n = quotes_after(R"({"id":"abc","venue":"lighter","rules":[{"when":[["spread_bps",">",0]],"action":"skew_ticks","value":-2}]})", ev, 1);
    ASSERT_EQ(n.bid, 97.0); ASSERT_EQ(n.ask, 99.0);
    auto w = quotes_after(R"({"id":"abc","venue":"lighter","rules":[{"when":[["spread_bps",">",0]],"action":"widen_ticks","value":2}]})", ev, 1);
    ASSERT_EQ(w.bid, 97.0); ASSERT_EQ(w.ask, 103.0);
    // two rules add
    auto t = quotes_after(R"({"id":"abc","venue":"lighter","rules":[{"when":[["spread_bps",">",0]],"action":"widen_ticks","value":1},
                                                                       {"when":[["spread_bps",">",0]],"action":"widen_ticks","value":1}]})", ev, 1);
    ASSERT_EQ(t.bid, 97.0); ASSERT_EQ(t.ask, 103.0);
}
TEST(hold_ms_keeps_an_action_after_its_condition_clears) {
    std::vector<hl::Event> ev = {ebook(mkbook(DAY0, 95, 105, 50, 1)),            // heavy bids: imbalance ≈ 0.94
                                 ebook(mkbook(DAY0 + 200, 95, 105, 5, 5)),      // balanced, 200 ms later
                                 ebook(mkbook(DAY0 + 2000, 95, 105, 5, 5))};    // balanced, 2 s later
    const std::string idea = R"({"id":"abc","venue":"lighter","rules":[{"when":[["book_imbalance",">",0.5]],"action":"pull_ask","hold_ms":1000}]})";
    ASSERT_EQ(quotes_after(idea, ev, 1).ask, 0.0);
    ASSERT_EQ(quotes_after(idea, ev, 2).ask, 0.0);       // still held
    ASSERT_EQ(quotes_after(idea, ev, 3).ask, 101.0);     // released
}
TEST(state_features_inventory_and_time_since_fill_drive_rules) {
    // a sale through our bid fills us long; a rule "inventory_usd > 50 → pull_bid" must then stop the bid
    std::vector<hl::Event> ev = {ebook(mkbook(DAY0, 95, 105)), etrade(DAY0 + 10, 98, 5, false), ebook(mkbook(DAY0 + 20, 95, 105))};
    const std::string idea = R"({"id":"abc","venue":"lighter","rules":[{"when":[["inventory_usd",">",50]],"action":"pull_bid"}]})";
    auto q = quotes_after(idea, ev, 3);
    ASSERT_EQ(q.bid, 0.0);
    ASSERT(q.ask > 0);
    auto none = quotes_after(R"({"id":"abc","venue":"lighter","rules":[{"when":[["minutes_since_last_fill","<",1]],"action":"pull_both"}]})", ev, 1);
    ASSERT_EQ(none.bid, 0.0);                              // segment start counts as the last fill: 0 minutes
}
TEST(hl_anchor_fair_moves_quotes_toward_hyperliquid) {
    std::vector<hl::Event> ev;
    std::vector<HlMid> hlm;
    for (int s = 0; s < 90; ++s) { ev.push_back(ebook(mkbook(DAY0 + s * 1000, 95, 105), DAY0 + s * 1000 + 50)); hlm.push_back({DAY0 + s * 1000 + 10, s < 80 ? 100.0 : 102.0}); }
    Idea I = *parse(R"({"id":"abc","venue":"lighter","fair":{"type":"hl_anchor","k":1}})");
    Idea P = *parse(R"({"id":"abc","venue":"lighter"})");
    DayData d = compact(ev, 20, 0, INT64_MAX);
    Frame F = compute_frame(d, &hlm, I.frame_keys(), rule_params());
    CoinRun a(I, rule_params(), d, F), b(P, rule_params(), d, F);
    for (size_t i = 0; i < d.ev.size(); ++i) { a.step(i); b.step(i); }
    ASSERT_EQ(b.sim().quote_px(true), 99.0);
    ASSERT(a.sim().quote_px(true) >= 100.0);               // HL-implied ≈ 102: the bid moved up toward it
    ASSERT(a.sim().quote_px(false) >= 102.0);
}

// ── baseline = mm_sim ───────────────────────────────────────────────────────────────────────────────────────────
static hl::Result mm_sim_way(const std::vector<hl::Event>& ev, const hl::Params& p) {   // mm_sim --daily, verbatim
    hl::Simulator s(p);
    hl::run_events(ev, s);
    int64_t last = 0;
    for (const auto& e : ev) if (e.is_book) last = e.book.t_ms;
    s.flatten_now(last);
    return s.finish();
}
TEST(plain_idea_and_a_never_firing_rule_reproduce_mm_sim_bit_for_bit) {
    auto ev = synth_events(DAY0, DAY0 + 6 * 3'600'000, 11);
    hl::Params p = tick1(); p.half_width_ticks = 2; p.min_spread_ticks = 4; p.imb_pull = 0.6; p.pause_move_ticks = 2; p.pause_ms = 2000;
    p.requote_ticks = 1; p.maker_fee_bps = 0; p.taker_fee_bps = 2.5; p.latency_ms = 100; p.fills = hl::FillModel::NoCross;
    const hl::Result ref = mm_sim_way(ev, p);
    ASSERT(ref.fills > 20);
    DayData d = compact(ev, 20, 0, INT64_MAX);
    for (const char* js : {R"({"id":"abc","venue":"extended"})",
                           R"({"id":"abc","venue":"extended","rules":[{"when":[["spread_bps","<",-1]],"action":"pull_both"}]})"}) {
        Idea I = *parse(js);
        Frame F = compute_frame(d, nullptr, I.frame_keys(), p);
        CoinRun cr(I, p, d, F);
        for (size_t i = 0; i < d.ev.size(); ++i) cr.step(i);
        const SegResult r = cr.finish();
        ASSERT_EQ(r.fills, ref.fills);
        ASSERT_EQ(r.pnl, ref.flat_pnl);
        ASSERT_EQ(r.vol, ref.volume_usd);
        double hsum = 0; for (auto& [h, x] : r.hours) hsum += x;
        ASSERT_NEAR(hsum, ref.flat_pnl, 1e-9);           // hourly blocks add up to the day
    }
}
TEST(baseline_equals_mm_sim_frozen_on_a_real_lighter_day) {
    Env env;
    const auto V = venue_of("lighter", env);
    const fs::path f = V->dirs[0] / "GMX" / "2026-10-25.jsonl.zst";
    std::error_code ec;
    if (!fs::exists(f, ec)) { std::cout << "  (archive drive not mounted — skipped)\n"; return; }
    std::ifstream in(REPO / "data/mm_gates/params_lighter.json");
    const json pf = json::parse(in);
    Venue v = *V; v.params_file = REPO / "data/mm_gates/params_lighter.json";
    const auto p = frozen_params(v, pf, "GMX", false);                       // mm_sim --params: 300 ms both, NoCross
    ASSERT(p.has_value());
    hl::DayFile df{"GMX", "2026-10-25", f, {}, false};
    const auto ev = hl::load_day(df);
    const hl::Result ref = mm_sim_way(ev, *p);
    DayData d = compact(ev, 20, 0, INT64_MAX);
    Idea I = *parse(R"({"id":"baseline","venue":"lighter"})");
    Frame F = compute_frame(d, nullptr, I.frame_keys(), *p);
    CoinRun cr(I, *p, d, F);
    for (size_t i = 0; i < d.ev.size(); ++i) cr.step(i);
    const SegResult r = cr.finish();
    std::cout << "  GMX 2026-10-25: mm_sim " << ref.fills << " fills $" << ref.flat_pnl << " | mm_lab " << r.fills << " fills $" << r.pnl << "\n";
    ASSERT_EQ(r.fills, ref.fills);
    ASSERT_EQ(r.pnl, ref.flat_pnl);
    ASSERT_EQ(r.vol, ref.volume_usd);
    // and the evaluation's frozen setting differs from it only by latency: 145 place / 445 cancel
    const auto pe = frozen_params(v, pf, "GMX", true);
    ASSERT_EQ(pe->place_latency_ms, int64_t{145});
    ASSERT_EQ(pe->cancel_latency_ms, int64_t{445});
    ASSERT_EQ(pe->half_width_ticks, p->half_width_ticks);
}

// ── registry, threshold, holdout sealing, determinism ───────────────────────────────────────────────────────────
TEST(registry_appends_counts_and_refuses_a_retest) {
    const fs::path root = synth_root("reg", false);
    Options o = synth_opts(root);
    BatchResult b1 = judge_batch(json::parse(SYN_IDEAS), o);
    ASSERT_EQ(b1.cards.size(), size_t{3});
    auto reg = read_registry(o.registry);
    ASSERT_EQ(reg.size(), size_t{3});
    ASSERT_EQ(registry_tests(reg), int64_t{3});
    for (size_t i = 0; i < reg.size(); ++i) { ASSERT_EQ(reg[i]["k"].get<int64_t>(), static_cast<int64_t>(i + 1)); ASSERT_EQ(reg[i]["kind"].get<std::string>(), std::string("in_sample")); ASSERT(reg[i].contains("hash")); }
    // a second batch: the same id is refused, a new id raises the bar
    BatchResult b2 = judge_batch(json::parse(R"({"ideas":[{"id":"syn_width_4","venue":"extended","params":{"half_width_ticks":4}},
                                                          {"id":"syn_width_6","venue":"extended","params":{"half_width_ticks":6}}]})"), o);
    ASSERT_EQ(b2.cards.size(), size_t{1});
    ASSERT_EQ(b2.refused.size(), size_t{1});
    ASSERT(b2.refused[0].find("already tested") != std::string::npos);
    reg = read_registry(o.registry);
    ASSERT_EQ(registry_tests(reg), int64_t{4});
    ASSERT_EQ(reg.back()["k"].get<int64_t>(), int64_t{4});
    ASSERT(reg.back()["t_needed"].get<double>() > reg.front()["t_needed"].get<double>());
    fs::remove_all(root);
}
TEST(holdout_is_sealed_without_a_registered_in_sample_pass) {
    const fs::path root = synth_root("seal", false);
    Options o = synth_opts(root);
    o.holdout = true;
    // never registered
    BatchResult h0 = judge_batch(json::parse(SYN_IDEAS), o);
    ASSERT(h0.cards.empty());
    ASSERT_EQ(h0.refused.size(), size_t{3});
    // registered, but FAIL
    Options is = synth_opts(root);
    BatchResult b = judge_batch(json::parse(SYN_IDEAS), is);
    for (const auto& c : b.cards) ASSERT(c["verdict"] == "FAIL" || c["verdict"] == "PASS");
    BatchResult h1 = judge_batch(json::parse(SYN_IDEAS), o);
    size_t fails = 0; for (const auto& c : b.cards) fails += c["verdict"] == "FAIL";
    ASSERT_EQ(h1.cards.size(), b.cards.size() - fails);
    // a registered PASS with a DIFFERENT spec: refused; with the same spec: the holdout card, on the sealed window only
    json pass = read_registry(o.registry)[0];
    pass["verdict"] = "PASS"; pass["id"] = "syn_sealed"; pass["spec"]["id"] = "syn_sealed";   // a hand-made PASS (tests only)
    registry_append(o.registry, pass);
    json doc = {{"ideas", json::array({pass["spec"]})}};
    json bad = doc; bad["ideas"][0]["params"] = {{"half_width_ticks", 3}};
    ASSERT(judge_batch(bad, o).cards.empty());
    BatchResult h2 = judge_batch(doc, o);
    ASSERT_EQ(h2.cards.size(), size_t{1});
    ASSERT_EQ(h2.cards[0]["kind"].get<std::string>(), std::string("holdout"));
    ASSERT_EQ(h2.cards[0]["window"].get<std::string>().substr(0, 20), iso_ms(pass["split"]["boundary_ms"].get<int64_t>()));
    // the look is logged (no result in it) and counted; a second look at the same id is refused
    auto reg = read_registry(o.registry);
    ASSERT_EQ(reg.size(), size_t{5});
    ASSERT_EQ(reg.back()["kind"].get<std::string>(), std::string("holdout_look"));
    ASSERT(!reg.back().contains("verdict") && !reg.back().contains("summary"));
    ASSERT_EQ(registry_tests(reg), int64_t{5});
    BatchResult h3 = judge_batch(doc, o);
    ASSERT(h3.cards.empty());
    ASSERT(h3.refused[0].find("already looked") != std::string::npos);
    // a holdout against any registry but the pinned one is refused before anything is read
    Options other = o; other.registry = root / "elsewhere.jsonl";
    registry_append(other.registry, pass);
    BatchResult h4 = judge_batch(doc, other);
    ASSERT(h4.cards.empty());
    ASSERT(h4.refused[0].find("pinned") != std::string::npos);
    fs::remove_all(root);
}
TEST(in_sample_results_cannot_see_the_holdout_days) {
    const fs::path a = synth_root("calm", false), b = synth_root("crash", true);
    const Split s = compute_split(*venue_of("extended", test_env(a)), venue_days(*venue_of("extended", test_env(a))), synth_opts(a).asof_ms);
    ASSERT_EQ(s.mode, std::string("days"));
    ASSERT_EQ(s.recorded_days, 5);
    ASSERT_EQ(s.boundary_ms, DAY0 + 3 * 86'400'000LL);          // the last ceil(30 %) = 2 of 5 days are sealed
    BatchResult ra = judge_batch(json::parse(SYN_IDEAS), synth_opts(a)), rb = judge_batch(json::parse(SYN_IDEAS), synth_opts(b));
    ASSERT_EQ(ra.cards.size(), rb.cards.size());
    for (size_t i = 0; i < ra.cards.size(); ++i) ASSERT_EQ(ra.cards[i].dump(), rb.cards[i].dump());   // a crash after the boundary changes nothing
    // whereas a holdout look would see it
    json pass = read_registry(synth_opts(a).registry)[0]; pass["verdict"] = "PASS";
    for (const auto& r : {a, b}) registry_append(synth_opts(r).registry, pass);
    Options ha = synth_opts(a), hb = synth_opts(b); ha.holdout = hb.holdout = true;
    json doc = {{"ideas", json::array({pass["spec"]})}};
    // the registry now holds the id twice (FAIL then PASS): the latest entry decides
    const auto ca = judge_batch(doc, ha).cards, cb = judge_batch(doc, hb).cards;
    ASSERT_EQ(ca.size(), size_t{1}); ASSERT_EQ(cb.size(), size_t{1});
    ASSERT(ca[0]["idea"].dump() != cb[0]["idea"].dump());
    fs::remove_all(a); fs::remove_all(b);
}
TEST(same_input_gives_identical_output_at_any_thread_count) {
    const fs::path root = synth_root("det", false);
    Options o1 = synth_opts(root, "r1.jsonl"), o2 = synth_opts(root, "r2.jsonl");
    o1.threads = 1; o2.threads = 3;
    const BatchResult a = judge_batch(json::parse(SYN_IDEAS), o1), b = judge_batch(json::parse(SYN_IDEAS), o2);
    ASSERT_EQ(a.cards.size(), size_t{3});
    for (size_t i = 0; i < a.cards.size(); ++i) { ASSERT_EQ(a.cards[i].dump(), b.cards[i].dump()); ASSERT_EQ(card_md(a.cards[i]), card_md(b.cards[i])); }
    std::ifstream x(o1.registry), y(o2.registry);
    std::string sx((std::istreambuf_iterator<char>(x)), {}), sy((std::istreambuf_iterator<char>(y)), {});
    ASSERT_EQ(sx, sy);
    fs::remove_all(root);
}
TEST(verdict_rule_checks_every_criterion) {
    // a hand-built variant that wins everywhere passes; each broken criterion fails with its own reason
    auto seg = [](const std::string& coin, int64_t h0, std::vector<double> hp, int64_t fills, double vol) {
        SegResult s; s.coin = coin; s.day = "2026-01-01"; s.fills = fills; s.vol = vol;
        for (size_t i = 0; i < hp.size(); ++i) { s.hours[h0 + i] = hp[i]; s.pnl += hp[i]; }
        return s;
    };
    Variant idea, base;
    idea.idea = *parse(R"({"id":"abc","venue":"extended","params":{"half_width_ticks":4}})");
    idea.coins = {"A", "B"};
    const double U[6] = {10, 6, 8, 4, 7, 5};                          // 4-hour block P&L: t ≈ 7.6 over 6 blocks
    std::vector<double> good(24); for (size_t i = 0; i < 24; ++i) good[i] = U[i / 4] / 8;
    idea.segs = {seg("A", 490'000, good, 40, 20'000), seg("B", 490'000, good, 40, 20'000)};
    base.segs = {seg("A", 490'000, std::vector<double>(24, -0.5), 40, 20'000), seg("B", 490'000, std::vector<double>(24, -0.5), 40, 20'000)};
    base.coins = idea.coins;
    Variant nb = idea; nb.label = "n";
    Verdict v = judge(idea, base, nullptr, {{&nb, &base}}, 1, 4, false);
    if (!v.pass) throw std::runtime_error(v.fails[0]);
    ASSERT_EQ(v.card["criteria"]["t_units"].get<size_t>(), size_t{6});
    // the bar after a billion registered tests (α_k = 2.5e-20, t_needed ≈ 9.2) is beyond this t
    Verdict t = judge(idea, base, nullptr, {{&nb, &base}}, 1'000'000'000, 4, false);
    ASSERT(!t.pass); ASSERT(t.fails[0].find("t_needed") != std::string::npos);
    // robustness: a neighbour that is positive but loses to ITS OWN same-size baseline fails the idea
    Variant nbase = base; nbase.segs = {seg("A", 490'000, std::vector<double>(24, 2.0), 40, 20'000), seg("B", 490'000, std::vector<double>(24, 2.0), 40, 20'000)};
    Verdict r = judge(idea, base, nullptr, {{&nb, &nbase}}, 1, 4, false);
    ASSERT(!r.pass); ASSERT(r.fails[0].find("robustness") != std::string::npos);
    // and a neighbour's own numbers never reach the card: only the count
    ASSERT(!r.card.contains("robustness"));
    ASSERT_EQ(r.card["criteria"]["neighbours_bad"].get<int>(), 1);
    ASSERT(r.card.dump().find("\"n\"") == std::string::npos);
    ASSERT(card_md(r.card).find("| n |") == std::string::npos);
    Variant few = idea; few.segs[0].fills = 5; few.segs[1].fills = 5;
    ASSERT(!judge(few, base, nullptr, {}, 1, 4, false).pass);
    Variant b2 = base; b2.segs = idea.segs;
    Verdict tv = judge(idea, b2, nullptr, {}, 1, 4, false);
    ASSERT(!tv.pass); ASSERT(tv.fails[0].find("same-size baseline") != std::string::npos);
}
TEST(criterion_two_compares_against_the_same_size_baseline_not_100) {
    auto seg = [](const std::string& coin, double pnl, double vol) {
        SegResult s; s.coin = coin; s.day = "2026-01-01"; s.fills = 50; s.vol = vol; s.pnl = pnl;
        for (int h = 0; h < 24; ++h) s.hours[490'000 + h] = pnl / 24 + (h % 2 ? 0.01 : -0.01) * pnl / 24;
        return s;
    };
    Variant idea, same_size, at100;
    idea.idea = *parse(R"({"id":"abc","venue":"extended","size_usd":11})");
    idea.coins = {"A"};
    idea.segs = {seg("A", 2.0, 2'000)};          // +10 bps at $11
    same_size.segs = {seg("A", 1.95, 2'000)};    // +9.75 bps: the frozen strategy at $11 is nearly as good
    at100.segs = {seg("A", -20, 20'000)};        // −10 bps at $100
    Verdict v = judge(idea, same_size, &at100, {}, 1, 4, false);
    ASSERT(!v.pass);
    bool beat_fail = false; for (const auto& f : v.fails) beat_fail |= f.find("same-size baseline") != std::string::npos;
    ASSERT(beat_fail);                                                    // 0.25 bps better than same size: not enough
    ASSERT_NEAR(v.card["delta_bps"].get<double>(), 0.25, 1e-9);
    ASSERT(v.card.contains("baseline_100"));                              // the $100 baseline is shown for information
    ASSERT_NEAR(v.card["baseline_100"]["net_bps"].get<double>(), -10.0, 1e-9);
}
TEST(alpha_spending_sums_below_alpha_and_asof_is_sealed) {
    double sum = 0;
    for (int64_t k = 1; k <= 200'000; ++k) sum += alpha_k(k);
    ASSERT(sum <= kAlpha + 1e-12);
    ASSERT_NEAR(alpha_k(1), kAlpha / 2, 1e-15);
    ASSERT_NEAR(t_needed(1), z_upper(0.0125), 1e-12);
    double prev = 0;
    for (int64_t k : {1, 2, 5, 10, 40, 100, 1000}) { ASSERT(t_needed(k) > prev); prev = t_needed(k); }
    const int64_t now = DAY0 + 10 * 3'600'000;
    ASSERT(check_asof(now - 6 * 60'000, now).empty());
    ASSERT(!check_asof(now - 4 * 60'000, now).empty());
    ASSERT(!check_asof(now + 3'600'000, now).empty());
}
TEST(holdout_boundary_is_frozen_once_per_venue) {
    const fs::path root = synth_root("freeze", false);
    Options o = synth_opts(root);
    o.asof_ms = DAY0 + 3 * 86'400'000LL - 1;                             // only 3 days recorded so far → a time split
    BatchResult b1 = judge_batch(json::parse(R"({"ideas":[{"id":"fz_one","venue":"extended","params":{"half_width_ticks":4}}]})"), o);
    ASSERT_EQ(b1.cards.size(), size_t{1});
    const json bnd = read_boundaries(o.boundaries);
    ASSERT(bnd.contains("extended"));
    const int64_t frozen = bnd["extended"]["boundary_ms"].get<int64_t>();
    ASSERT_EQ(bnd["extended"]["mode"].get<std::string>(), std::string("time"));
    // two days later the 30 % rule would put the boundary elsewhere; the frozen one is used
    Options o2 = synth_opts(root);
    ASSERT(compute_split(*venue_of("extended", o2.env), venue_days(*venue_of("extended", o2.env)), o2.asof_ms).boundary_ms != frozen);
    BatchResult b2 = judge_batch(json::parse(R"({"ideas":[{"id":"fz_two","venue":"extended","params":{"half_width_ticks":6}}]})"), o2);
    ASSERT_EQ(b2.cards[0]["split"]["boundary_ms"].get<int64_t>(), frozen);
    ASSERT_EQ(read_boundaries(o2.boundaries).dump(), bnd.dump());         // never rewritten
    // a holdout look on a venue that never had an in-sample run (no frozen boundary) is refused
    Options h = synth_opts(root); h.holdout = true; h.boundaries = root / "none.json";
    json pass = read_registry(h.registry)[0]; pass["verdict"] = "PASS";
    registry_append(h.registry, pass);
    BatchResult hb = judge_batch(json{{"ideas", json::array({pass["spec"]})}}, h);
    ASSERT(hb.cards.empty());
    fs::remove_all(root);
}
TEST(rate_limit_is_on_by_default_and_the_baseline_shares_it) {
    const fs::path root = synth_root("rl", false);
    Options o = synth_opts(root);
    BatchResult b = judge_batch(json::parse(R"({"ideas":[{"id":"rl_frozen","venue":"lighter"},
                                                         {"id":"rl_free","venue":"lighter","rate_limit":{"per_min":60,"accounts":10}}]})"), o);
    ASSERT_EQ(b.cards.size(), size_t{2});
    const json& c = b.cards[0];
    bool noted = false; for (const auto& n : c["notes"]) noted |= n.get<std::string>().find("rate limit 60 requests/min × 1 account") != std::string::npos;
    ASSERT(noted);
    ASSERT_EQ(c["coins_run"].size(), size_t{2});
    ASSERT_EQ(c["idea"].dump(), c["baseline"].dump());                    // the frozen idea IS its baseline: same limiter
    ASSERT_EQ(c["delta_bps"].get<double>(), 0.0);
    // ten accounts relax the limiter: a different result, judged against a baseline under the same ten accounts
    const json& f = b.cards[1];
    ASSERT_EQ(f["idea"].dump(), f["baseline"].dump());
    fs::remove_all(root);
}

int main() {
    RUN(minimal_idea_parses_with_frozen_defaults);
    RUN(unknown_field_feature_param_op_action_are_refused);
    RUN(evaluation_owned_fields_and_optimistic_settings_are_refused);
    RUN(funding_and_cross_venue_on_history_are_refused);
    RUN(windows_and_parameter_references_parse);
    RUN(spec_hash_ignores_id_and_prose_but_not_the_strategy);
    RUN(neighbours_move_every_knob_one_step_both_ways);
    RUN(own_features_never_see_the_future);
    RUN(cross_venue_features_cut_at_the_decision_time_and_never_see_the_future);
    RUN(cross_venue_features_ignore_a_non_monotone_venue_receive_clock);
    RUN(flow_rate_and_move_features_compute_the_documented_values);
    RUN(pull_rules_remove_exactly_the_named_side);
    RUN(skew_and_widen_move_the_quotes_by_the_stated_ticks);
    RUN(hold_ms_keeps_an_action_after_its_condition_clears);
    RUN(state_features_inventory_and_time_since_fill_drive_rules);
    RUN(hl_anchor_fair_moves_quotes_toward_hyperliquid);
    RUN(plain_idea_and_a_never_firing_rule_reproduce_mm_sim_bit_for_bit);
    RUN(baseline_equals_mm_sim_frozen_on_a_real_lighter_day);
    RUN(registry_appends_counts_and_refuses_a_retest);
    RUN(holdout_is_sealed_without_a_registered_in_sample_pass);
    RUN(in_sample_results_cannot_see_the_holdout_days);
    RUN(same_input_gives_identical_output_at_any_thread_count);
    RUN(verdict_rule_checks_every_criterion);
    RUN(criterion_two_compares_against_the_same_size_baseline_not_100);
    RUN(alpha_spending_sums_below_alpha_and_asof_is_sealed);
    RUN(holdout_boundary_is_frozen_once_per_venue);
    RUN(rate_limit_is_on_by_default_and_the_baseline_shares_it);
    std::cout << "\n" << (tests_run - tests_failed) << "/" << tests_run << " passed\n";
    return tests_failed ? 1 : 0;
}
