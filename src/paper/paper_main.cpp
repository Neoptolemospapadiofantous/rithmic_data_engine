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

    bool halted() const { return strategy ? broker->halted() : bbroker->halted(); }
    RiskManager& risk() { return strategy ? broker->risk() : bbroker->risk(); }
    double last_price() const {
        return strategy ? strategy->last_price() : bbroker->last_price();
    }
    paper::PaperPositionRow position_row(double lp) const {
        return strategy ? broker->position_row(lp) : bbroker->position_row(lp);
    }
    std::function<void(const paper::PaperTradeRow&)> on_trade_closed;
};

} // namespace

int main(int argc, char** argv) {
    std::string config_path = "config/paper_fleet.json";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc)
            config_path = argv[++i];
        else if (std::strcmp(argv[i], "--help") == 0) {
            std::printf("usage: paper_engine [--config config/paper_fleet.json]\n");
            return 0;
        }
    }

    std::signal(SIGTERM, on_signal);
    std::signal(SIGINT,  on_signal);

    LOG("[PAPER] paper_engine starting — config=%s", config_path.c_str());

    paper::FleetConfig fleet;
    try {
        fleet = paper::FleetConfig::from_file(config_path);
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

    // ── Account envelope ─────────────────────────────────────────────────────
    paper::AccountEnvelope account(fleet.starting_balance, fleet.daily_loss_limit,
                                   fleet.trailing_drawdown_cap);

    const std::string session_date = paper::et_trade_date(now_us());
    const int64_t day_start_us     = paper::et_day_start_us(now_us());
    std::string cur_date = session_date;  // mutated on day rollover; shared with callbacks
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
        if (fs.engine != "orb" && fs.engine != "mtf_scalper") {
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
            if (r->strategy) r->strategy->halt_trading("manual_disabled");
            else             r->mtf->halt_trading("manual_disabled");
            LOG("[PAPER] %s disabled in paper_strategies — built but halted",
                fs.id.c_str());
        }

        // Seed per-strategy risk + trade counters from history.
        double total = db->sum_pnl(fs.id);
        double today = db->sum_pnl_since(fs.id, day_start_us);
        int    ntoday = db->count_trades_since(fs.id, day_start_us);
        r->risk().seed_total_profit(total);
        r->risk().seed_daily_pnl(today);
        if (r->strategy) {
            r->broker->seed_entries_today(ntoday);
            r->strategy->seed_trades_today(ntoday);
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
                if (r->strategy) r->strategy->halt_trading("resumed_daily_halt: " + d->halt_reason);
                else             r->mtf->halt_trading("resumed_daily_halt: " + d->halt_reason);
                LOG("[PAPER] %s resumed with daily halt (%s)",
                    fs.id.c_str(), d->halt_reason.c_str());
            }
        }

        // Resume an open paper position (restart recovery). ORB: broker resumes
        // with the persisted stop. MTF: the strategy restarts flat-minded, so a
        // resumed leg would diverge (strategy bracket ≠ held leg) — resume for
        // the row, then flatten at entry once accounting is wired (below).
        if (auto p = db->load_position(fs.id)) {
            if (r->strategy) {
                r->broker->resume_position(p->direction, p->qty, p->entry_price,
                                           p->stop_price, p->entry_time_us);
            } else {
                r->bbroker->resume_position(p->direction, p->qty, p->entry_price,
                                            p->stop_price, p->entry_time_us);
                r->mtf_restart_flatten = true;
            }
        }

        if (r->risk().halted()) {
            if (r->strategy) r->strategy->halt_trading("risk_halted_at_seed: " +
                                                       r->risk().halt_reason());
            else             r->mtf->halt_trading("risk_halted_at_seed: " +
                                                  r->risk().halt_reason());
        }

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
        if (r->strategy) r->broker->on_trade_closed  = r->on_trade_closed;
        else             r->bbroker->on_trade_closed = r->on_trade_closed;

        // Lockstep start: close a resumed MTF leg at its entry price so broker
        // and strategy agree on flat before the first tick arrives.
        if (r->mtf_restart_flatten)
            r->bbroker->flatten("restart_flatten", now_us(), 0.0);

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
            if (r->strategy) {
                r->strategy->halt_trading(reason);
                r->broker->flatten("account_halt", now_us(),
                                   r->strategy->last_price());
            } else {
                r->mtf->halt_trading(reason);
                r->bbroker->flatten("account_halt", now_us(),
                                    r->bbroker->last_price());
            }
        }
        persist_account();
    };

    if (auto reason = account.check_limits(); !reason.empty())
        halt_all(reason);

    // ── Main loop ────────────────────────────────────────────────────────────
    int64_t watermark = now_us() - 5LL * 60 * 1'000'000;  // seed 5 min of context
    LOG("[PAPER] Tick watermark seeded at %s (now − 5 min)",
        paper::PaperDb::format_ts(watermark).c_str());

    // Reference-feed 1m bar aggregator state (SMT/intermarket module)
    int64_t ref_watermark = watermark;
    long    ref_bar_min   = -1;
    double  ref_hi = 0.0, ref_lo = 0.0, ref_close = 0.0;

    int flush_every_ms = 2000;
    int64_t last_flush_ms = 0;
    int ctl_every_ms = 1000;
    int64_t last_ctl_ms = 0;
    double last_price = 0.0;

    while (!g_stop) {
        auto ticks = db->poll_ticks(feed_symbol, watermark, 5000);
        for (const auto& t : ticks) {
            watermark = t.ts_us;
            last_price = t.price;
            OrbTick ot{t.ts_us, t.price, t.size, t.is_buy};
            for (auto& r : runners) {
                if (r->strategy) {
                    r->broker->on_tick(ot);     // broker first: signals fill on NEXT tick
                    r->strategy->on_tick(ot);
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
                    for (auto& r : runners)
                        if (r->mtf && r->mtf->wants_reference_feed())
                            r->mtf->on_reference_bar(ref_hi, ref_lo, ref_close);
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
        utc_now_et(eh, em);
        for (auto& r : runners) {
            if (r->strategy) r->strategy->check_eod(eh, em);
            else             r->mtf->check_time_flatten(eh, em);
        }

        // Day rollover at 18:00 ET.
        std::string d = paper::et_trade_date(now_us());
        if (d != cur_date) {
            LOG("[PAPER] Day rollover %s → %s — resetting sessions",
                cur_date.c_str(), d.c_str());
            cur_date = d;
            for (auto& r : runners) {
                if (r->strategy) {
                    r->strategy->reset_session();
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

        // Manual control channel: dashboard-written paper_control rows.
        // Runs even with zero ticks (loop spins on the poll sleep).
        int64_t now_ms = now_us() / 1000;
        if (now_ms - last_ctl_ms >= ctl_every_ms) {
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
                    if (hit->strategy) hit->strategy->halt_trading("manual_disable");
                    else               hit->mtf->halt_trading("manual_disable");
                } else if (ctl.action == "enable") {
                    if (hit->risk().halted()) {
                        LOG("[PAPER] WARN control #%lld: enable %s refused — "
                            "risk-halted (%s)", (long long)ctl.id,
                            hit->fcfg.id.c_str(), hit->risk().halt_reason().c_str());
                        db->consume_control(ctl.id);
                        continue;
                    }
                    if (hit->strategy) hit->strategy->unhalt_trading("manual_enable");
                    else               hit->mtf->unhalt_trading("manual_enable");
                } else if (ctl.action == "flatten") {
                    if (hit->strategy)
                        hit->broker->flatten("manual", now_us(),
                                             hit->strategy->last_price());
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

        std::this_thread::sleep_for(std::chrono::milliseconds(fleet.poll_ms));
    }

    // ── Graceful shutdown ────────────────────────────────────────────────────
    LOG("[PAPER] Shutdown signal — flushing paper state");
    for (auto& r : runners)
        db->save_position(r->position_row(last_price));
    persist_account();
    LOG("[PAPER] paper_engine stopped cleanly");
    return 0;
}
