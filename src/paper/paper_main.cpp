/*  ═══════════════════════════════════════════════════════════════════════════
    paper_main.cpp — paper-trading fleet engine

    Runs ~20 strategy instances in paper mode on one prop account, consuming
    the live tick stream from PostgreSQL (collector-owned `ticks` table).
    This process NEVER connects to Rithmic and never sends orders.

    Usage: paper_engine [--config config/paper_fleet.json]
    ═══════════════════════════════════════════════════════════════════════════ */
#include "config.hpp"
#include "log.hpp"
#include "orb_strategy.hpp"
#include "mtf_scalper_strategy.hpp"
#include "trend_strategy.hpp"
#include "paper_quote.hpp"
#include "paper_broker.hpp"
#include "paper_bracket_broker.hpp"
#include "paper_config.hpp"
#include "paper_db.hpp"
#include "paper_engine.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

volatile std::sig_atomic_t g_stop = 0;
void on_signal(int) { g_stop = 1; }

int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// ── Replay (audit) mode ──────────────────────────────────────────────────────
// `--replay-from "YYYY-MM-DD HH:MM" --replay-to "YYYY-MM-DD HH:MM"` (ET) runs the
// whole fleet over RECORDED ticks as fast as they load, with the engine clock
// driven by tick timestamps (EOD checks, day rollover) instead of wall time,
// then exits. Combined with `--account-label audit` it exercises every
// strategy against a real session without touching the live paper account:
// the dashboard's learning queries exclude the 'audit' label.
// Known distortion: OrbStrategy's post-stop cooldown is wall-clock (5 s of
// replay ≈ many replayed minutes), so re-entries right after a stop are
// under-counted in replay.
static bool    g_replay       = false;
static int64_t g_replay_clock = 0;   // last replayed tick (us since epoch)
static int64_t g_replay_to    = 0;

static int64_t clock_us() { return g_replay ? g_replay_clock : now_us(); }
// "YYYY-MM-DD" + 1 day (calendar), for the hold_pre_event filter
static std::string next_ymd(const std::string& ymd) {
    struct tm t{}; if (sscanf(ymd.c_str(), "%d-%d-%d", &t.tm_year, &t.tm_mon, &t.tm_mday) != 3) return ymd;
    t.tm_year -= 1900; t.tm_mon -= 1; time_t tt = timegm(&t) + 86400; struct tm n; gmtime_r(&tt, &n);
    char buf[16]; std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", n.tm_year + 1900, n.tm_mon + 1, n.tm_mday); return buf;
}

static int64_t parse_et_us(const std::string& ymd_hm) {
    // "YYYY-MM-DD HH:MM" in America/New_York → us since epoch (uses the TZ env)
    std::tm tm{}; int Y, M, D, h, m;
    if (std::sscanf(ymd_hm.c_str(), "%d-%d-%d %d:%d", &Y, &M, &D, &h, &m) != 5)
        throw std::runtime_error("bad --replay time (want \"YYYY-MM-DD HH:MM\"): " + ymd_hm);
    tm.tm_year = Y - 1900; tm.tm_mon = M - 1; tm.tm_mday = D; tm.tm_hour = h; tm.tm_min = m; tm.tm_isdst = -1;
    setenv("TZ", "America/New_York", 1); tzset();
    time_t t = mktime(&tm);
    return static_cast<int64_t>(t) * 1'000'000LL;
}

static void utc_us_to_et(int64_t us, int& h, int& m) {
    time_t tt = static_cast<time_t>(us / 1'000'000LL);
    struct tm tm_utc;
    gmtime_r(&tt, &tm_utc);
    int64_t et = (int64_t)tt - us_et_offset(tm_utc) * 3600LL;
    h = (int)((et / 3600) % 24);
    if (h < 0) h += 24;
    m = (int)((et % 3600) / 60);
}

void utc_now_et(int& h, int& m) {
    time_t tt = time(nullptr);
    struct tm tm_utc;
    gmtime_r(&tt, &tm_utc);
    int64_t et = (int64_t)tt - us_et_offset(tm_utc) * 3600LL;
    h = (int)((et / 3600) % 24);
    if (h < 0) h += 24;
    m = (int)((et % 3600) / 60);
}

// One enabled strategy instance: signal generator + paper broker + daily stats.
// engine "orb" uses strategy/broker; "mtf_scalper" uses mtf/bbroker.
struct Runner {
    paper::FleetStrategy           fcfg;
    OrbConfig                      cfg;
    std::unique_ptr<OrbStrategy>   strategy;
    std::unique_ptr<paper::PaperBroker> broker;
    std::unique_ptr<MtfScalperStrategy> mtf;
    std::unique_ptr<paper::PaperBracketBroker> bbroker;
    // today's paper_daily accumulators
    int    trades = 0;
    int    wins   = 0;
    double pnl    = 0.0;
    bool   mtf_restart_flatten = false;  // resumed MTF leg → close after wiring

    std::unique_ptr<TrendStrategy>  trend;   // engine "trend" — shares the plain PaperBroker with orb

    bool halted() const { return broker ? broker->halted() : bbroker->halted(); }
    RiskManager& risk() { return broker ? broker->risk() : bbroker->risk(); }
    double last_price() const {
        return strategy ? strategy->last_price() : trend ? trend->last_price() : bbroker->last_price();
    }
    paper::PaperPositionRow position_row(double lp) const {
        return broker ? broker->position_row(lp) : bbroker->position_row(lp);
    }
    // engine-agnostic strategy calls
    void s_halt(const std::string& why)   { if (strategy) strategy->halt_trading(why);   else if (trend) trend->halt_trading(why);   else mtf->halt_trading(why); }
    void s_unhalt(const std::string& why) { if (strategy) strategy->unhalt_trading(why); else if (trend) trend->unhalt_trading(why); else mtf->unhalt_trading(why); }
    void s_reset()                        { if (strategy) strategy->reset_session();     else if (trend) trend->reset_session();     else mtf->reset_session(); }
    void s_eod(int h, int m)              { if (strategy) strategy->check_eod(h, m);     else if (trend) trend->check_eod(h, m);     else mtf->check_time_flatten(h, m); }
    // Feed-gap guard (orb + trend only): a strategy that is flat and not halted
    // can safely restart its session state after a hole in the tick stream.
    void on_quote(const paper::Quote& q, double tick) {
        if (broker) broker->on_quote(q); else bbroker->on_quote(q);
        if (trend) trend->on_quote(q.ts_us, q.bid, q.bid_sz, q.ask, q.ask_sz, tick);
    }
    bool gap_resettable() const {
        if (mtf) return false;
        if (broker->direction() != 0 || halted()) return false;
        return strategy ? !strategy->session().risk_halted : !trend->session().risk_halted;
    }
    std::function<void(const paper::PaperTradeRow&)> on_trade_closed;
};

} // namespace

int main(int argc, char** argv) {
    std::string config_path = "config/paper_fleet.json";
    std::string label_override, replay_from, replay_to;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc)
            config_path = argv[++i];
        else if (std::strcmp(argv[i], "--account-label") == 0 && i + 1 < argc)
            label_override = argv[++i];
        else if (std::strcmp(argv[i], "--replay-from") == 0 && i + 1 < argc)
            replay_from = argv[++i];
        else if (std::strcmp(argv[i], "--replay-to") == 0 && i + 1 < argc)
            replay_to = argv[++i];
        else if (std::strcmp(argv[i], "--help") == 0) {
            std::printf("usage: paper_engine [--config config/paper_fleet.json]\n"
                        "       paper_engine --replay-from \"YYYY-MM-DD HH:MM\" --replay-to \"YYYY-MM-DD HH:MM\" "
                        "[--account-label audit]   (ET; replays recorded ticks, then exits)\n");
            return 0;
        }
    }
    if (!replay_from.empty() || !replay_to.empty()) {
        if (replay_from.empty() || replay_to.empty()) {
            std::fprintf(stderr, "replay needs both --replay-from and --replay-to\n"); return 2;
        }
        g_replay = true;
        g_replay_clock = parse_et_us(replay_from);
        g_replay_to    = parse_et_us(replay_to);
        if (label_override.empty()) label_override = "audit";
    }

    std::signal(SIGTERM, on_signal);
    std::signal(SIGINT,  on_signal);

    LOG("[PAPER] paper_engine starting — config=%s", config_path.c_str());

    paper::FleetConfig fleet;
    try {
        fleet = paper::FleetConfig::from_file(config_path);
        if (!label_override.empty()) {
            LOG("[PAPER] account_label overridden: %s -> %s%s", fleet.account_label.c_str(),
                label_override.c_str(), g_replay ? " (replay/audit mode)" : "");
            fleet.account_label = label_override;
        }
    } catch (const std::exception& e) {
        LOG("[PAPER] FATAL: %s", e.what());
        return 1;
    }

    // The collector writes ticks under the feed (front-month) symbol — NQ —
    // while the fleet trades the micro label (MNQ). Same price series.
    const std::string feed_symbol = fleet.feed_symbol.empty()
                                        ? fleet.symbol : fleet.feed_symbol;
    LOG("[PAPER] Feed symbol=%s (trading %s)%s", feed_symbol.c_str(),
        fleet.symbol.c_str(),
        fleet.reference_symbol.empty()
            ? "" : (" — reference feed=" + fleet.reference_symbol).c_str());

    Config env = Config::from_env(".env");
    if (env.pg_password.empty()) {
        LOG("[PAPER] FATAL: PG_PASSWORD not set (checked .env)");
        return 1;
    }

    std::unique_ptr<paper::PaperDb> db;
    try {
        db = std::make_unique<paper::PaperDb>(env.pg_connstr());
    } catch (const std::exception& e) {
        LOG("[PAPER] FATAL: %s", e.what());
        return 1;
    }
    LOG("[PAPER] Connected to PostgreSQL — schema ensured");
    db->set_account_label(fleet.account_label);   // seeding queries never see another label's rows
    if (g_replay) {
        db->set_trades_only(true);
        LOG("[PAPER] REPLAY isolation: the store will record paper_trades (label '%s') only — "
            "no strategy/position/daily/account rows, no inherited live state", fleet.account_label.c_str());
    }

    // ── Account envelope ─────────────────────────────────────────────────────
    paper::AccountEnvelope account(fleet.starting_balance, fleet.daily_loss_limit,
                                   fleet.trailing_drawdown_cap);

    const std::string session_date = paper::et_trade_date(clock_us());
    const int64_t day_start_us     = paper::et_day_start_us(clock_us());
    std::string cur_date = session_date;  // mutated on day rollover; shared with callbacks
    // Session-shape regime gate (paper_quote.hpp): one state for the whole fleet — every
    // strategy sees the same tape — with the prior day's ATR14 from session_stats.
    paper::RegimeState regime;
    bool event_day = db->calendar_event_day(cur_date);
    bool event_next = db->calendar_event_day(next_ymd(cur_date));
    regime.set_atr(cur_date, db->session_atr14_before(feed_symbol, cur_date));
    LOG("[PAPER] Regime gate: date=%s prior ATR14=%.1f pts%s", cur_date.c_str(), regime.atr_pts,
        regime.atr_pts > 0.0 ? "" : " (none — regime-gated variants stay flat until session_stats has a row)");
    LOG("[PAPER] Session trade_date=%s (day start %s)", session_date.c_str(),
        paper::PaperDb::format_ts(day_start_us).c_str());

    // ── Build strategy runners ───────────────────────────────────────────────
    std::vector<std::unique_ptr<Runner>> runners;
    double acct_total_pnl = 0.0, acct_day_pnl = 0.0;

    for (const auto& fs : fleet.strategies) {
        // Registry upsert happens for every configured strategy, known engine or not.
        db->upsert_strategy(fs.id, fleet.account_label, fs.engine, fs.params_json,
                            fs.enabled);
        if (!fs.enabled) {
            LOG("[PAPER] %s disabled in config — skipped", fs.id.c_str());
            continue;
        }
        if (fs.engine != "orb" && fs.engine != "mtf_scalper" && fs.engine != "trend") {
            LOG("[PAPER] WARN: unknown engine '%s' for strategy %s — skipped",
                fs.engine.c_str(), fs.id.c_str());
            continue;
        }

        auto r = std::make_unique<Runner>();
        r->fcfg = fs;
        r->cfg  = fleet.orb_config_for(fs);  // risk knobs shared by both engines

        if (fs.engine == "mtf_scalper") {
            MtfScalperConfig mc = MtfScalperConfig::from_json_string(fs.params_json);
            mc.point_value      = fleet.point_value;
            mc.tick_size        = fleet.tick_size;
            mc.starting_balance = fleet.starting_balance;
            r->mtf = std::make_unique<MtfScalperStrategy>(mc);
            r->bbroker = std::make_unique<paper::PaperBracketBroker>(
                fs.id, fleet.account_label, fleet.symbol, r->cfg,
                fleet.tick_size, fleet.slippage_ticks, db.get());
            paper::PaperBracketBroker* brk = r->bbroker.get();
            r->mtf->set_signal_callback(
                [brk](OrbSignal sig, double px, const std::string& reason) {
                    brk->on_signal(sig, px, reason);
                });
        } else if (fs.engine == "trend") {
            TrendConfig tc = TrendConfig::from_json_string(fs.params_json);
            r->trend  = std::make_unique<TrendStrategy>(tc, r->cfg);
            r->broker = std::make_unique<paper::PaperBroker>(
                fs.id, fleet.account_label, fleet.symbol, r->cfg,
                fleet.tick_size, fleet.slippage_ticks, db.get());
            paper::PaperBroker* brk = r->broker.get();
            r->trend->set_signal_callback(
                [brk](OrbSignal sig, double px, const std::string& reason) {
                    brk->on_signal(sig, px, reason);
                });
        } else {
            r->strategy = std::make_unique<OrbStrategy>(r->cfg);
            r->broker   = std::make_unique<paper::PaperBroker>(
                fs.id, fleet.account_label, fleet.symbol, r->cfg,
                fleet.tick_size, fleet.slippage_ticks, db.get());
            paper::PaperBroker* brk = r->broker.get();
            r->strategy->set_signal_callback(
                [brk](OrbSignal sig, double px, const std::string& reason) {
                    brk->on_signal(sig, px, reason);
                });
        }

        // Manual disable (paper_strategies.enabled, set via the dashboard)
        // overrides config: the runner is still built but starts halted.
        if (auto en = db->load_enabled(fs.id); en && !*en) {
            r->s_halt("manual_disabled");
            LOG("[PAPER] %s disabled in paper_strategies — built but halted",
                fs.id.c_str());
        }

        // Seed per-strategy risk + trade counters from history.
        double total = db->sum_pnl(fs.id);
        double today = db->sum_pnl_since(fs.id, day_start_us);
        int    ntoday = db->count_trades_since(fs.id, day_start_us);
        r->risk().seed_total_profit(total);
        r->risk().seed_daily_pnl(today);
        if (r->broker) {
            r->broker->seed_entries_today(ntoday);
            if (r->strategy) r->strategy->seed_trades_today(ntoday);
            else             r->trend->seed_trades_today(ntoday);
        } else {
            r->mtf->seed_state(ntoday, today, total);
        }
        acct_total_pnl += total;
        acct_day_pnl   += today;

        // Resume today's daily accumulators.
        if (auto d = db->load_daily(fs.id, session_date)) {
            r->trades = d->trades;
            r->wins   = d->wins;
            r->pnl    = d->pnl_usd;
            if (d->halted) {
                r->s_halt("resumed_daily_halt: " + d->halt_reason);
                LOG("[PAPER] %s resumed with daily halt (%s)",
                    fs.id.c_str(), d->halt_reason.c_str());
            }
        }

        // Resume an open paper position (restart recovery). ORB: broker resumes
        // with the persisted stop. MTF: the strategy restarts flat-minded, so a
        // resumed leg would diverge (strategy bracket ≠ held leg) — resume for
        // the row, then flatten at entry once accounting is wired (below).
        if (auto p = db->load_position(fs.id)) {
            if (r->broker) {                      // orb + trend share the plain broker
                r->broker->resume_position(p->direction, p->qty, p->entry_price,
                                           p->stop_price, p->entry_time_us);
                if (r->trend)
                    r->trend->seed_open_position(
                        p->direction > 0 ? OrbSignal::BUY : OrbSignal::SELL,
                        p->entry_time_us);
            } else {
                r->bbroker->resume_position(p->direction, p->qty, p->entry_price,
                                            p->stop_price, p->entry_time_us);
                r->mtf_restart_flatten = true;
            }
        }

        if (r->risk().halted())
            r->s_halt("risk_halted_at_seed: " + r->risk().halt_reason());

        // Wire trade-close accounting (daily row + account envelope). For
        // mtf_scalper the strategy also needs the close (loss-streak cooldown,
        // daily-loss guardrails — Pine spec §4).
        Runner* rp = r.get();
        r->on_trade_closed = [rp, &db, &account, &cur_date](
                                 const paper::PaperTradeRow& t) {
            rp->trades++;
            if (t.pnl_usd > 0) rp->wins++;
            rp->pnl += t.pnl_usd;
            // Tell the strategy its trade closed — ORB clears in_position (the
            // entry gate) and arms its cooldown; MTF runs §4 guardrails
            // (loss streak, daily-loss accounting). A restart_flatten of a
            // resume-dropped MTF leg is bookkeeping, not a strategy decision.
            const OrbSignal closed_dir =
                t.direction == "LONG" ? OrbSignal::BUY : OrbSignal::SELL;
            if (rp->mtf) {
                if (t.exit_reason != "restart_flatten")
                    rp->mtf->notify_trade_filled(closed_dir, t.exit_reason,
                                                 t.pnl_usd);
            } else if (rp->strategy) {
                rp->strategy->notify_trade_filled(closed_dir, t.exit_reason);
            } else if (rp->trend) {
                rp->trend->notify_trade_filled(closed_dir, t.exit_reason);
            }
            paper::PaperDailyRow d;
            d.strategy_id = rp->fcfg.id;
            d.trade_date  = cur_date;
            d.trades      = rp->trades;
            d.wins        = rp->wins;
            d.pnl_usd     = rp->pnl;
            d.halted      = rp->risk().halted();
            d.halt_reason = rp->risk().halt_reason();
            db->upsert_daily(d);
            std::string halt = account.on_trade_close(t.pnl_usd);
            if (!halt.empty()) account.set_halt(halt);
        };
        if (r->broker) r->broker->on_trade_closed  = r->on_trade_closed;   // orb + trend
        else           r->bbroker->on_trade_closed = r->on_trade_closed;   // mtf_scalper

        // Lockstep start: close a resumed MTF leg at its entry price so broker
        // and strategy agree on flat before the first tick arrives.
        if (r->mtf_restart_flatten)
            r->bbroker->flatten("restart_flatten", now_us(), 0.0);

        if (r->broker) r->broker->set_regime(&regime); else if (r->bbroker) r->bbroker->set_regime(&regime);
        if (r->trend) { r->trend->set_event_day(event_day); r->trend->set_event_next_day(event_next); r->trend->set_day_atr(regime.atr_pts); }
        runners.push_back(std::move(r));
    }

    // Seed account peak from the persisted row (survives restarts).
    double peak = fleet.starting_balance;
    if (auto a = db->load_account(fleet.account_label)) {
        peak = a->peak_equity;
        LOG("[PAPER] Loaded paper_account: equity=%.2f peak=%.2f",
            a->equity, a->peak_equity);
    }
    account.seed(acct_total_pnl, acct_day_pnl, peak);

    LOG("[PAPER] Fleet ready: %zu/%zu strategies active (account=%s symbol=%s)",
        runners.size(), fleet.strategies.size(),
        fleet.account_label.c_str(), fleet.symbol.c_str());

    auto persist_account = [&]() {
        paper::PaperAccountRow a;
        a.account_label    = fleet.account_label;
        a.starting_balance = account.starting_balance();
        a.equity           = account.equity();
        a.peak_equity      = account.peak_equity();
        a.day_pnl          = account.day_pnl();
        a.day_start_equity = account.day_start_equity() > 0.0
                                 ? account.day_start_equity()
                                 : account.equity() - account.day_pnl();
        a.trade_date       = cur_date;
        a.halted           = account.halted();
        a.halt_reason      = account.halt_reason();
        db->upsert_account(a);
    };
    persist_account();

    // Halt-all: flatten every runner's paper position, block new entries.
    auto halt_all = [&](const std::string& reason) {
        LOG("[PAPER] ACCOUNT HALT — %s — flattening all paper positions",
            reason.c_str());
        account.set_halt(reason);
        for (auto& r : runners) {
            r->s_halt(reason);
            if (r->broker) r->broker->flatten("account_halt", now_us(), r->last_price());
            else           r->bbroker->flatten("account_halt", now_us(), r->bbroker->last_price());
        }
        persist_account();
    };

    if (auto reason = account.check_limits(); !reason.empty())
        halt_all(reason);

    // ── Main loop ────────────────────────────────────────────────────────────
    int64_t watermark = g_replay ? g_replay_clock
                                 : now_us() - 5LL * 60 * 1'000'000;  // seed 5 min of context
    LOG("[PAPER] Tick watermark seeded at %s (%s)",
        paper::PaperDb::format_ts(watermark).c_str(), g_replay ? "replay start" : "now − 5 min");
    if (g_replay)
        LOG("[PAPER] REPLAY %s → %s as account '%s' — engine clock follows tick timestamps",
            paper::PaperDb::format_ts(g_replay_clock).c_str(),
            paper::PaperDb::format_ts(g_replay_to).c_str(), fleet.account_label.c_str());

    // Reference-feed 1m bar aggregator state (SMT/intermarket module)
    int64_t ref_watermark = watermark;
    long    ref_bar_min   = -1;
    double  ref_hi = 0.0, ref_lo = 0.0, ref_close = 0.0;

    int flush_every_ms = 2000;
    int64_t last_flush_ms = 0;
    int ctl_every_ms = 1000;
    int64_t last_ctl_ms = 0;
    double last_price = 0.0;
    int64_t prev_tick_ts = 0;                                   // feed-gap guard
    // Top-of-book stream: quotes are applied to every runner in tick-time order
    // (a quote is visible once its timestamp is ≤ the tick being processed).
    std::deque<paper::Quote> qbuf; int64_t bbo_watermark = watermark; int64_t quotes_seen = 0;
    auto refill_quotes = [&]() {
        if (!qbuf.empty()) return;
        for (const auto& b : db->poll_bbo(feed_symbol, bbo_watermark, 5000)) {
            qbuf.push_back(paper::Quote{b.ts_us, b.bid, b.ask, b.bid_sz, b.ask_sz});
            bbo_watermark = b.ts_us;
        }
    };
    int last_eod_minute = -1;                                   // tick-time minute of the last EOD check
    const int64_t feed_gap_reset_us = (int64_t)fleet.feed_gap_reset_secs * 1'000'000LL;

    while (!g_stop) {
        auto ticks = db->poll_ticks(feed_symbol, watermark, 5000);
        if (g_replay) {
            bool past_end = ticks.empty() || ticks.front().ts_us > g_replay_to;
            if (past_end) {
                LOG("[PAPER] REPLAY complete at %s", paper::PaperDb::format_ts(watermark).c_str());
                break;
            }
            while (!ticks.empty() && ticks.back().ts_us > g_replay_to) ticks.pop_back();
        }
        // Day rollover at 18:00 ET. Called per tick with the TICK's time (before the feed-gap
        // guard, which re-seeds trades_today from the runner's day count — on 2026-09-22 the
        // 18:00 reopen gap reset ran first and carried the previous evening's hold into the
        // new day, blocking the 18:00 entry) and again after each batch with the wall clock.
        auto roll_day_if_needed = [&](int64_t at_us) {
        std::string d = paper::et_trade_date(at_us);
        if (d != cur_date) {
            LOG("[PAPER] Day rollover %s → %s — resetting sessions",
                cur_date.c_str(), d.c_str());
            cur_date = d;
            regime.set_atr(cur_date, db->session_atr14_before(feed_symbol, cur_date));
            LOG("[PAPER] Regime gate: date=%s prior ATR14=%.1f pts", cur_date.c_str(), regime.atr_pts);
            event_day = db->calendar_event_day(cur_date);
            event_next = db->calendar_event_day(next_ymd(cur_date));
            for (auto& r : runners) if (r->trend) { r->trend->set_event_day(event_day); r->trend->set_event_next_day(event_next); r->trend->set_day_atr(regime.atr_pts); }
            if (event_day) LOG("[PAPER] %s is a scheduled-release day (calendar) — news_break event-only variants armed", cur_date.c_str());
            for (auto& r : runners) {
                if (r->broker) {
                    r->s_reset();
                    r->broker->reset_day();
                } else {
                    r->mtf->reset_session();
                    r->bbroker->reset_day();
                    // MTF halts latch until explicitly cleared; lift at
                    // rollover when the strategy's own risk manager is clear.
                    if (!r->bbroker->risk().halted())
                        r->mtf->unhalt_trading("day_rollover");
                }
                r->trades = 0;
                r->wins   = 0;
                r->pnl    = 0.0;
            }
            account.reset_day();
            // A persistent account halt (e.g. trailing drawdown) survives
            // rollover — re-apply it to every strategy.
            if (account.halted())
                halt_all(account.halt_reason());
            persist_account();
        }
        };
        for (const auto& t : ticks) {
            roll_day_if_needed(t.ts_us);   // the tick's own trading day, before anything else sees it
            // Feed-gap guard: after a hole in the tick stream (collector down,
            // forced logout, box asleep) the first tick back is NOT a signal —
            // an ORB range built on one minute of ticks and a Donchian channel
            // with a 90-minute hole both fire on the jump. Flat, un-halted
            // orb/trend strategies restart their session state; open
            // positions keep their stops; MTF keeps its bar history.
            if (prev_tick_ts && t.ts_us - prev_tick_ts > feed_gap_reset_us) {
                int n = 0;
                for (auto& r : runners)
                    if (r->gap_resettable()) {
                        r->s_reset();
                        if (r->strategy) r->strategy->seed_trades_today(r->trades);
                        else             r->trend->seed_trades_today(r->trades);
                        ++n;
                    }
                LOG("[PAPER] WARN feed gap %.0fs (%s -> %s) — session reset on %d flat strategies; "
                    "%zu keep their positions/halts",
                    (t.ts_us - prev_tick_ts) / 1e6, paper::PaperDb::format_ts(prev_tick_ts).c_str(),
                    paper::PaperDb::format_ts(t.ts_us).c_str(), n, runners.size() - (size_t)n);
            }
            prev_tick_ts = t.ts_us;
            watermark = t.ts_us;
            if (g_replay) g_replay_clock = t.ts_us;
            // quotes up to this tick
            refill_quotes();
            while (!qbuf.empty() && qbuf.front().ts_us <= t.ts_us) {
                for (auto& r : runners) r->on_quote(qbuf.front(), fleet.tick_size);
                qbuf.pop_front(); ++quotes_seen;
                if (qbuf.empty()) refill_quotes();
            }
            // End-of-window check on every minute boundary of TICK time (not wall
            // clock): a strategy whose window ends at 14:00 must flatten on the
            // first 14:00 tick, in replay exactly as live. The wall-clock check
            // below only covers sparse-tick stretches.
            {
                int th, tm; utc_us_to_et(t.ts_us, th, tm);
                if (th * 60 + tm != last_eod_minute) {
                    last_eod_minute = th * 60 + tm;
                    for (auto& r : runners) r->s_eod(th, tm);
                }
            }
            last_price = t.price;
            OrbTick ot{t.ts_us, t.price, t.size, t.is_buy};
            {
                int rh, rm; utc_us_to_et(t.ts_us, rh, rm);
                regime.on_tick(cur_date, rh, rm, t.ts_us, t.price, (double)t.size);
            }
            for (auto& r : runners) {
                if (r->broker) {
                    r->broker->on_tick(ot);     // broker first: signals fill on NEXT tick
                    if (r->strategy) r->strategy->on_tick(ot);
                    else             r->trend->on_tick(ot);
                } else {
                    // bracket broker first; bracket/qty come from the strategy's
                    // state as of the previous tick — signals fill on the next.
                    r->bbroker->on_tick(ot, r->mtf->cur_stop(), r->mtf->cur_tp(),
                                        r->mtf->qty_calc(r->mtf->equity()));
                    r->mtf->on_tick(ot);
                }
            }
        }

        // Intermarket reference feed: aggregate reference ticks into 1m bars
        // and fan each completed bar out to MTF strategies that wired a
        // reference_symbol (SMT/correlation module, Pine spec §1.8).
        if (!fleet.reference_symbol.empty()) {
            auto ref_ticks = db->poll_ticks(fleet.reference_symbol,
                                            ref_watermark, 5000);
            for (const auto& t : ref_ticks) {
                ref_watermark = t.ts_us;
                const long bar_min = (long)(t.ts_us / 60'000'000LL);
                if (ref_bar_min < 0) {          // first tick ever
                    ref_bar_min = bar_min;
                    ref_hi = ref_lo = ref_close = t.price;
                    continue;
                }
                if (bar_min != ref_bar_min) {   // minute rolled → emit bar
                    for (auto& r : runners) {
                        if (r->mtf && r->mtf->wants_reference_feed())
                            r->mtf->on_reference_bar(ref_hi, ref_lo, ref_close);
                        if (r->trend) r->trend->on_reference_bar(t.ts_us, ref_close);
                    }
                    ref_bar_min = bar_min;
                    ref_hi = ref_lo = t.price;
                } else {
                    if (t.price > ref_hi) ref_hi = t.price;
                    if (t.price < ref_lo) ref_lo = t.price;
                }
                ref_close = t.price;
            }
        }

        // Account limit re-check (covers halts latched by trade closes).
        if (!account.halted()) {
            if (auto reason = account.check_limits(); !reason.empty())
                halt_all(reason);
        }

        // Periodic EOD signal check (wall clock — ticks may be sparse).
        int eh, em;
        if (g_replay) utc_us_to_et(g_replay_clock, eh, em);
        else          utc_now_et(eh, em);
        for (auto& r : runners) r->s_eod(eh, em);

        roll_day_if_needed(clock_us());

        // Manual control channel: dashboard-written paper_control rows.
        // Runs even with zero ticks (loop spins on the poll sleep).
        int64_t now_ms = now_us() / 1000;
        if (!g_replay && now_ms - last_ctl_ms >= ctl_every_ms) {
            last_ctl_ms = now_ms;
            for (const auto& ctl : db->poll_control()) {
                Runner* hit = nullptr;
                for (auto& r : runners) {
                    if (r->fcfg.id == ctl.strategy_id) { hit = r.get(); break; }
                }
                if (!hit) {
                    LOG("[PAPER] WARN control #%lld: unknown strategy '%s' — skipped",
                        (long long)ctl.id, ctl.strategy_id.c_str());
                    db->consume_control(ctl.id);
                    continue;
                }
                if (ctl.action == "disable") {
                    hit->s_halt("manual_disable");
                } else if (ctl.action == "enable") {
                    if (hit->risk().halted()) {
                        LOG("[PAPER] WARN control #%lld: enable %s refused — "
                            "risk-halted (%s)", (long long)ctl.id,
                            hit->fcfg.id.c_str(), hit->risk().halt_reason().c_str());
                        db->consume_control(ctl.id);
                        continue;
                    }
                    hit->s_unhalt("manual_enable");
                } else if (ctl.action == "flatten") {
                    if (hit->broker)
                        hit->broker->flatten("manual", now_us(), hit->last_price());
                    else
                        hit->bbroker->flatten("manual", now_us(),
                                              hit->bbroker->last_price());
                } else {
                    LOG("[PAPER] WARN control #%lld: unknown action '%s' for %s — skipped",
                        (long long)ctl.id, ctl.action.c_str(), hit->fcfg.id.c_str());
                    db->consume_control(ctl.id);
                    continue;
                }
                db->consume_control(ctl.id);
                LOG("[PAPER] control #%lld applied: %s %s",
                    (long long)ctl.id, ctl.action.c_str(), hit->fcfg.id.c_str());
            }
        }

        // Periodic flush of position + account rows.
        if (now_ms - last_flush_ms >= flush_every_ms) {
            last_flush_ms = now_ms;
            for (auto& r : runners)
                db->save_position(r->position_row(last_price));
            persist_account();
        }

        if (!g_replay) std::this_thread::sleep_for(std::chrono::milliseconds(fleet.poll_ms));
    }

    // ── Graceful shutdown ────────────────────────────────────────────────────
    LOG("[PAPER] Shutdown signal — flushing paper state");
    for (auto& r : runners)
        db->save_position(r->position_row(last_price));
    persist_account();
    LOG("[PAPER] paper_engine stopped cleanly");
    return 0;
}
