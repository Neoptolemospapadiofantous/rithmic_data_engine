// mm_rl_label — the counterfactual dataset for a state-dependent quoting policy (research/preregistered + docs/market_making_plan.md,
// 2026-10-28 "RL phase 1"). For every coin-day archive it replays the recorded books and trades through the queue-aware simulator
// ONCE PER ACTION — "always quote at k ticks, no defences" for each k in --widths, plus the frozen setting (--params) as the
// reference policy — and writes one row per --step-ms window: the market state at the window start (known then) and, for each
// action and side, the markout-adjusted $ earned by that action's fills inside the window (side · (mid at fill + markout − fill
// price) · qty, fees per --params). "Pull" earns 0 by definition. A learner then asks: given this state, which action's fills
// were worth the most — a contextual bandit on honest fills (hl::FillModel, default NoCross), never on the legacy model.
// Attribution is by fill time: an order resting across a window boundary is credited to the window it fills in (v1 approximation).
//
//   build-exp/mm_rl_label --dir /media/.../rithmic_dex/lighter --params data/mm_gates/params_lighter.json --coins AVAX,AAVE
//                         --from 2026-10-26 --to 2026-10-28 --out data/mm_rl/labels.csv [--widths 4,5,6,8] [--step-ms 10000]
//                         [--markout-s 10] [--order-usd 11] [--latency-ms 300]
#include "dex/hl_mm.hpp"
#include "dex/hl_events.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

static hl::Params coin_params(const json& j, const std::string& coin, double order_usd, int64_t latency_ms) {
    hl::Params q;
    q.maker_fee_bps = j.value("maker_bps", 0.0); q.taker_fee_bps = j.value("taker_bps", 0.0);
    q.latency_ms = latency_ms > 0 ? latency_ms : j.value("latency_ms", int64_t{300});
    q.order_usd = order_usd > 0 ? order_usd : j.value("order_usd", 11.0);
    q.cancels = hl::CancelModel::Pessimistic; q.track_markouts = false;
    if (j.contains("coins") && j["coins"].contains(coin)) {
        const auto& cj = j["coins"][coin];
        q.half_width_ticks = cj.value("half_width_ticks", 5.0); q.skew_ticks = cj.value("skew_ticks", 0.0);
        q.fair = cj.value("fair", "mid") == "micro" ? hl::Fair::Micro : hl::Fair::Mid;
        q.min_spread_ticks = cj.value("min_spread_ticks", 0.0); q.imb_pull = cj.value("imb_pull", 0.0);
        q.pause_move_ticks = cj.value("pause_move_ticks", 0.0); q.pause_ms = cj.value("pause_ms", int64_t{0});
        q.requote_ticks = cj.value("requote_ticks", 0.0); q.venue_tick = cj.value("venue_tick", 0.0);
        q.strat_tick_rule = cj.value("tick_rule", "book") == "hyperliquid" ? hl::TickRule::Hyperliquid : hl::TickRule::Book;
        q.hl_sz_decimals = cj.value("hl_sz_decimals", 0); q.size_step = cj.value("size_step", 0.0);
        q.min_base = cj.value("min_base", 0.0); q.min_quote_usd = cj.value("min_quote_usd", 0.0);
    }
    return q;
}

struct Feat {   // the state at a window start, from information available then
    double spread_bps = 0, imb = 0, depth_bid_usd = 0, depth_ask_usd = 0, trades_60 = 0, buy_share_60 = 0.5, trade_usd_60 = 0;
    double range_60_bps = 0, dmid_1s_bps = 0, dmid_5s_bps = 0, ms_since_jump = 0; int hour = 0;
};

int main(int argc, char** argv) {
    fs::path dir, out, params_file; std::set<std::string> only; std::string from = "0000-00-00", to = "9999-99-99";
    std::vector<double> widths{4, 5, 6, 8}; int64_t step_ms = 10'000, latency_ms = 0; int markout_s = 10; double order_usd = 0;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto val = [&] { if (i + 1 >= argc) { std::fprintf(stderr, "%s needs a value\n", a.c_str()); std::exit(2); } return std::string(argv[++i]); };
        if (a == "--dir") dir = val();
        else if (a == "--out") out = val();
        else if (a == "--params") params_file = val();
        else if (a == "--coins") { std::stringstream ss(val()); for (std::string c; std::getline(ss, c, ',');) only.insert(c); }
        else if (a == "--from") from = val();
        else if (a == "--to") to = val();
        else if (a == "--widths") { widths.clear(); std::stringstream ss(val()); for (std::string c; std::getline(ss, c, ',');) widths.push_back(std::atof(c.c_str())); }
        else if (a == "--step-ms") step_ms = std::atoll(val().c_str());
        else if (a == "--markout-s") markout_s = std::atoi(val().c_str());
        else if (a == "--order-usd") order_usd = std::atof(val().c_str());
        else if (a == "--latency-ms") latency_ms = std::atoll(val().c_str());
        else { std::fprintf(stderr, "unknown arg %s\n", a.c_str()); return 2; }
    }
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec) || out.empty() || params_file.empty()) { std::fprintf(stderr, "usage: see header (--dir, --params, --out required)\n"); return 2; }
    json pj; { std::ifstream in(params_file); pj = json::parse(in, nullptr, false); if (pj.is_discarded()) { std::fprintf(stderr, "cannot read --params\n"); return 2; } }

    std::map<std::string, std::vector<hl::DayFile>> by_coin;
    for (auto& d : hl::list_days(dir)) if ((only.empty() || only.count(d.coin)) && d.day >= from && d.day <= to) by_coin[d.coin].push_back(d);
    fs::create_directories(out.parent_path(), ec);
    std::ofstream o(out);
    o << "coin,day,t_ms,hour,spread_bps,imb,depth_bid_usd,depth_ask_usd,trades_60,buy_share_60,trade_usd_60,range_60_bps,dmid_1s_bps,dmid_5s_bps,ms_since_jump";
    for (double w : widths) { char b[64]; std::snprintf(b, sizeof b, ",r_bid_%g,n_bid_%g,r_ask_%g,n_ask_%g", w, w, w, w); o << b; }
    o << ",r_bid_policy,n_bid_policy,r_ask_policy,n_ask_policy\n";

    int64_t rows = 0, bad = 0;
    for (auto& [coin, days] : by_coin) {
        for (const auto& d : days) {
            auto ev = hl::load_day(d, &bad);
            if (ev.empty()) continue;
            // ---- one simulator per action: "always quote at k ticks", defences off, inventory uncapped ----
            std::vector<hl::Params> ps;
            for (double w : widths) {
                hl::Params q = coin_params(pj, coin, order_usd, latency_ms);
                q.half_width_ticks = w; q.skew_ticks = 0; q.fair = hl::Fair::Mid; q.min_spread_ticks = 0; q.imb_pull = 0;
                q.pause_move_ticks = 0; q.pause_ms = 0; q.requote_ticks = 0; q.max_inv_units = 100000;
                ps.push_back(q);
            }
            ps.push_back(coin_params(pj, coin, order_usd, latency_ms));   // the frozen policy as recorded in --params
            std::vector<hl::Simulator> sims; for (auto& q : ps) sims.emplace_back(q);
            for (auto& s : sims) hl::run_events(ev, s);
            // ---- mid timeline for markouts ----
            std::vector<std::pair<int64_t, double>> mids;
            for (const auto& e : ev) if (e.is_book && e.book.valid()) mids.push_back({e.book.t_ms, e.book.mid()});
            if (mids.size() < 10) continue;
            auto mid_at = [&](int64_t t) -> double {   // first mid at or after t, within 2× the markout horizon
                auto it = std::lower_bound(mids.begin(), mids.end(), std::make_pair(t, -1.0));
                if (it == mids.end() || it->first - t > 2000LL * markout_s) return NAN;
                return it->second;
            };
            // ---- per-action rewards bucketed by window ----
            const int64_t t_first = mids.front().first, t_last = mids.back().first;
            const size_t nwin = (size_t)((t_last - t_first) / step_ms) + 1;
            struct Acc { double r_bid = 0, r_ask = 0; int n_bid = 0, n_ask = 0; };
            std::vector<std::vector<Acc>> acc(sims.size(), std::vector<Acc>(nwin));
            for (size_t k = 0; k < sims.size(); ++k)
                for (const auto& f : sims[k].fills()) {
                    if (f.taker) continue;
                    const double m = mid_at(f.t_ms + 1000LL * markout_s);
                    if (std::isnan(m)) continue;
                    const double r = f.side * (m - f.px) * f.qty - ps[k].maker_fee_bps * 1e-4 * f.px * f.qty;
                    const size_t w = (size_t)((f.t_ms - t_first) / step_ms);
                    if (w >= nwin) continue;
                    if (f.side > 0) { acc[k][w].r_bid += r; ++acc[k][w].n_bid; } else { acc[k][w].r_ask += r; ++acc[k][w].n_ask; }
                }
            // ---- features at each window start, then rows ----
            const hl::Book* last_book = nullptr; std::deque<const hl::Trade*> tr60; double jump_t = t_first;
            const double tick = sims[0].has_book() ? sims[0].strategy_tick() : 0;   // the strategy's tick for this coin (Hyperliquid rule on Lighter's grid)
            std::deque<std::pair<int64_t, double>> mid60;
            size_t ei = 0;
            for (size_t w = 0; w < nwin; ++w) {
                const int64_t t0 = t_first + (int64_t)w * step_ms;
                for (; ei < ev.size() && ev[ei].t_ms <= t0; ++ei) {   // everything known at t0
                    const auto& e = ev[ei];
                    if (e.is_book) {
                        if (!e.book.valid()) continue;
                        if (last_book && tick > 0 && std::abs(e.book.mid() - last_book->mid()) >= tick - 1e-12) jump_t = e.book.t_ms;   // a jump of ≥ 1 STRATEGY tick (the pause rule's unit)
                        last_book = &e.book;
                        mid60.push_back({e.book.t_ms, e.book.mid()});
                        while (!mid60.empty() && mid60.front().first < e.book.t_ms - 60'000) mid60.pop_front();
                    } else { tr60.push_back(&e.trade); while (!tr60.empty() && tr60.front()->t_ms < e.t_ms - 60'000) tr60.pop_front(); }
                }
                if (!last_book) continue;
                const hl::Book& b = *last_book; const double mid = b.mid();
                Feat f; f.hour = (int)((t0 / 3'600'000) % 24);
                f.spread_bps = (b.asks[0].px - b.bids[0].px) / mid * 1e4;
                f.imb = b.bids[0].sz + b.asks[0].sz > 0 ? (b.bids[0].sz - b.asks[0].sz) / (b.bids[0].sz + b.asks[0].sz) : 0;
                for (const auto& l : b.bids) if (l.px >= mid * (1 - 1e-3)) f.depth_bid_usd += l.px * l.sz;
                for (const auto& l : b.asks) if (l.px <= mid * (1 + 1e-3)) f.depth_ask_usd += l.px * l.sz;
                double buy = 0;
                for (const auto* t : tr60) { if (t->t_ms < t0 - 60'000) continue; f.trades_60 += 1; f.trade_usd_60 += t->px * t->sz; if (t->buy_aggr) buy += 1; }
                f.buy_share_60 = f.trades_60 > 0 ? buy / f.trades_60 : 0.5;
                double lo = mid, hi = mid, m1 = mid, m5 = mid;
                for (const auto& [t, m] : mid60) { if (t < t0 - 60'000) continue; lo = std::min(lo, m); hi = std::max(hi, m); if (t <= t0 - 1000) m1 = m; if (t <= t0 - 5000) m5 = m; }
                f.range_60_bps = (hi - lo) / mid * 1e4; f.dmid_1s_bps = (mid - m1) / mid * 1e4; f.dmid_5s_bps = (mid - m5) / mid * 1e4;
                f.ms_since_jump = (double)(t0 - (int64_t)jump_t);
                o << coin << ',' << d.day << ',' << t0 << ',' << f.hour << ',' << f.spread_bps << ',' << f.imb << ',' << f.depth_bid_usd << ',' << f.depth_ask_usd << ','
                  << f.trades_60 << ',' << f.buy_share_60 << ',' << f.trade_usd_60 << ',' << f.range_60_bps << ',' << f.dmid_1s_bps << ',' << f.dmid_5s_bps << ',' << f.ms_since_jump;
                for (size_t k = 0; k < sims.size(); ++k) o << ',' << acc[k][w].r_bid << ',' << acc[k][w].n_bid << ',' << acc[k][w].r_ask << ',' << acc[k][w].n_ask;
                o << '\n'; ++rows;
            }
            std::fprintf(stderr, "[mm_rl_label] %s %s: %zu windows, fills per action:", coin.c_str(), d.day.c_str(), nwin);
            for (size_t k = 0; k < sims.size(); ++k) std::fprintf(stderr, " %zu", sims[k].fills().size());
            std::fprintf(stderr, "\n");
        }
    }
    std::printf("mm_rl_label: %lld rows → %s (bad lines %lld)\n", (long long)rows, out.string().c_str(), (long long)bad);
    return 0;
}
