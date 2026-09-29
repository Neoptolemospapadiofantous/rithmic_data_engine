/*  ═══════════════════════════════════════════════════════════════════════════
    test_trade_end_invariant.cpp — "after a trade ends, nothing is left behind"

    Founder requirement (2026-09-23): for EVERY way a trade can end, once it is
    over there must be ZERO working orders at the exchange (no resting stop) and
    the exchange net position must be 0.

    A small mock exchange tracks every order the REAL OrderManager sends until it
    is filled or cancelled, keeps the net position, answers cancels the way
    Rithmic does (a cancel by an unmapped client id FAILS — the 2026-09-23 shape),
    and routes fills/cancel notices through the executor's own handlers
    (notification_router.hpp) exactly as executor_main does:
        tid=351 fill (cumulative qty) → fill_already_processed → on_fill_notification
        unknown order on our account  → notif::handle_unowned_fill
        cancel ack / Cancellation Failed → notif::handle_cancel_notification
        after every closed trade      → recancel_pending_stops (executor post-close)
        tid=451 update                → NetReconciler-style plan_unwind

    Config = production tradeify (2 MNQ). No DB, no network.
    ═══════════════════════════════════════════════════════════════════════════ */
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cmath>
#include <string>
#include <vector>

#include "../../src/execution/order_manager.hpp"
#include "../../src/execution/notification_router.hpp"

static int tests_run = 0, tests_failed = 0;
#define TEST(name) void test_##name()
#define RUN(name) do { \
    ++tests_run; \
    try { test_##name(); std::cout << "PASS " #name "\n"; } \
    catch (std::exception& e) { ++tests_failed; std::cout << "FAIL " #name ": " << e.what() << "\n"; } \
} while(0)
#define ASSERT(cond) do { if (!(cond)) throw std::runtime_error("Assert failed: " #cond); } while(0)
template <class A, class B> static std::string show(const A& a, const B& b) {
    std::ostringstream o; o << " (" << a << " vs " << b << ")"; return o.str(); }
#define ASSERT_EQ(a, b) do { if ((a) != (b)) throw std::runtime_error("ASSERT_EQ failed: " #a " != " #b + show(a, b)); } while(0)

static OrbConfig prod_cfg(double trail_delay = 300) {
    OrbConfig c;
    c.symbol = "MNQ"; c.exchange = "CME";
    c.qty = 2; c.sl_points = 15.0; c.trail_step = 10.0;
    c.trail_be_trigger = 3.0; c.trail_be_offset = 1.0; c.trail_delay_secs = (int)trail_delay;
    c.point_value = 2.0; c.daily_loss_limit = -500.0; c.trailing_drawdown_cap = 1000.0;
    c.consistency_cap_pct = 0.99; c.sl_fire_timeout_ms = 3000; c.dry_run = false;
    return c;
}

// ─── mock exchange ────────────────────────────────────────────────────────────
struct MockExchange {
    struct Ord { std::string client, server; int type; bool buy; int qty; int filled = 0;
                 bool working = true; bool mapped = false; };
    static constexpr const char* ACCT = "RTG25785042011";

    OrbConfig cfg; RiskManager risk; LatencyLogger lat; OrderManager om;
    std::vector<Ord> orders;
    std::vector<std::string> cancel_reqs;     // ids the OM asked us to cancel
    std::vector<std::string> halts;
    int net = 0, seq = 0;
    bool auto_map = true;                     // exchange acks (maps server ids) on send
    std::vector<std::string> to_map;          // acks queued from inside OM callbacks

    explicit MockExchange(OrbConfig c = prod_cfg()) : cfg(c), risk(cfg, 25000.0), lat(), om(cfg, risk, lat) {
        om.set_order_callback([this](const std::string& basket, const std::string&, const std::string&,
                                     int qty, int type, bool is_buy, double, const std::string&) -> bool {
            orders.push_back({basket, "S" + std::to_string(++seq), type, is_buy, qty});
            if (auto_map) to_map.push_back(basket);   // ack later: the OM holds its lock here
            return true;
        });
        om.set_cancel_callback([this](const std::string& id) { cancel_reqs.push_back(id); });
    }
    // deliver queued acks (tid=351 "open pending") — must run outside OM calls
    void flush() { auto q = to_map; to_map.clear(); for (auto& c : q) map(c); }
    Ord* find(const std::string& id) {
        for (auto& o : orders) if (o.client == id || o.server == id) return &o;
        return nullptr;
    }
    // tid=351 ack: executor maps server ids for every order and for the stop
    void map(const std::string& client) {
        Ord* o = find(client); if (!o || o->mapped) return;
        o->mapped = true;
        om.map_server_basket(o->client, o->server);
        om.on_stop_server_mapped(o->client, o->server);
    }
    // Rithmic routes RequestCancelOrder by ITS server basket id: a cancel carrying our
    // client id (user_tag) answers "Cancellation Failed" and the order keeps working.
    void process_cancels(bool fail_all = false) {
        flush();
        auto reqs = cancel_reqs; cancel_reqs.clear();
        for (const auto& id : reqs) {
            Ord* o = find(id);
            if (!o || !o->working) continue;                       // already gone: nothing to do
            bool ok = !fail_all && id == o->server;
            if (ok) {
                o->working = false;
                notif::handle_cancel_notification(om, 3, "Cancel received", o->client, o->server);
            } else {
                notif::handle_cancel_notification(om, 17, "Cancellation Failed", "", id);
            }
        }
    }
    // fill `q` more contracts of an order (default: the rest) and route like executor tid=351
    void fill(const std::string& id, double px, int q = -1) {
        flush();
        Ord* o = find(id); if (!o) throw std::runtime_error("fill: unknown order " + id);
        if (!o->working) throw std::runtime_error("fill: order not working " + id);
        if (q < 0) q = o->qty - o->filled;
        o->filled += q; net += o->buy ? q : -q;
        if (o->filled >= o->qty) o->working = false;
        const bool was_open = !om.is_flat();
        const std::string c = o->client;
        bool is_entry = om.is_entry_basket(c), is_stop = om.is_stop_basket(c), is_exit = om.is_exit_basket(c);
        if (is_entry || is_stop || is_exit) {
            if (!om.fill_already_processed(c, o->filled))
                om.on_fill_notification(c, px, o->filled, is_entry && !is_stop);
        } else {
            notif::handle_unowned_fill(om, c, o->server, ACCT, ACCT, px, q,
                                       [this](const std::string& w) { halts.push_back(w); });
        }
        after_close(was_open);
    }
    void fill_markets(double px) {
        for (size_t i = 0; i < orders.size(); ++i)
            if (orders[i].working && (orders[i].type == 2 || orders[i].type == 1)) fill(orders[i].client, px);
    }
    // executor: after a completed trade → strategy notified, recancel every guarded stop
    void after_close(bool was_open) {
        Position done;
        if (was_open && om.pop_trade_completed(done)) om.recancel_pending_stops();
    }
    // executor tid=451: exchange disagrees with us → trade the gap away / adopt flat
    void reconcile(double px) {
        flush();
        if (om.net_qty_consistent(net)) return;
        auto plan = notif::plan_unwind(net, om.position_snapshot(), cfg.qty);
        if (plan.qty > 0) {
            std::string b = "UNW-" + std::to_string(++seq);
            om.register_unwind_basket(b);
            orders.push_back({b, "S" + std::to_string(++seq), 2, plan.is_buy, plan.qty});
            map(b);
            fill(b, px);
        }
        if (plan.adopt_flat) om.adopt_external_close(px, "external_close");
        if (net == 0) om.confirm_exchange_flat();
    }
    int working() const { int n = 0; for (auto& o : orders) n += o.working; return n; }
    std::string working_list() const {
        std::string s; for (auto& o : orders) if (o.working) s += o.client + "(type=" + std::to_string(o.type) + ") ";
        return s;
    }
    const Ord* current_stop() { return find(om.position_snapshot().basket_id_stop); }
    std::string stop_id() { return om.position_snapshot().basket_id_stop; }

    // the invariant
    void assert_clean(const char* where) {
        flush();
        process_cancels();                                   // let in-flight cancels land
        if (working() != 0) throw std::runtime_error(std::string(where) + ": working orders left: " + working_list());
        if (net != 0) throw std::runtime_error(std::string(where) + ": exchange net=" + std::to_string(net));
        if (!om.is_flat()) throw std::runtime_error(std::string(where) + ": order manager not FLAT");
    }
};

static void enter(MockExchange& x, OrbSignal dir, double px) {
    x.om.on_signal(dir, px, "test_entry");
    x.fill(x.om.position_snapshot().basket_id_entry, px);
    ASSERT(x.om.state() == (dir == OrbSignal::BUY ? PosState::LONG : PosState::SHORT));
    ASSERT_EQ(x.working(), 1);                               // exactly one protective stop
    ASSERT(x.current_stop() && x.current_stop()->type == 4 && x.current_stop()->qty == 2);
}

// 1. hard stop-loss fills (long)
TEST(stop_loss_fill_long) {
    MockExchange x; enter(x, OrbSignal::BUY, 20000.0);
    x.fill(x.stop_id(), 19985.0);
    x.assert_clean("stop_loss_long");
}
// 2. hard stop-loss fills (short)
TEST(stop_loss_fill_short) {
    MockExchange x; enter(x, OrbSignal::SELL, 20000.0);
    x.fill(x.stop_id(), 20015.0);
    x.assert_clean("stop_loss_short");
}
// 3. breakeven: old stop cancelled (by server id), replacement fills
TEST(breakeven_stop_fill) {
    MockExchange x; enter(x, OrbSignal::SELL, 20000.0);
    std::string old_stop = x.stop_id();
    x.om.check_trail_and_stop(19996.0);                      // MFE 4 ≥ 3 → BE
    ASSERT(x.stop_id() != old_stop);
    x.process_cancels();
    ASSERT_EQ(x.working(), 1);                               // old gone, one stop working
    x.fill(x.stop_id(), 19999.0);
    x.assert_clean("breakeven");
}
// 4. trailing stop: several steps, each old stop cancelled, final one fills
TEST(trailing_stop_fill_after_steps) {
    MockExchange x(prod_cfg(0)); enter(x, OrbSignal::BUY, 20000.0);
    for (double p : {20004.0, 20020.0, 20035.0, 20050.0}) {
        x.om.check_trail_and_stop(p);
        x.process_cancels();
        ASSERT_EQ(x.working(), 1);
    }
    x.fill(x.stop_id(), 20040.0);
    x.assert_clean("trailing");
}
// 5. EOD / session_end flatten: stop cancelled + market exit
TEST(eod_session_end_flatten) {
    MockExchange x; enter(x, OrbSignal::BUY, 20000.0);
    x.om.on_signal(OrbSignal::FLATTEN_EOD, 20005.0, "session_end");
    x.process_cancels();
    x.fill_markets(20005.0);
    x.assert_clean("eod");
}
// 6. trend flip: FLATTEN_EOD exit, then the reversal entry, then its own exit
TEST(trend_flip_exit_then_reversal) {
    MockExchange x; enter(x, OrbSignal::BUY, 20000.0);
    x.om.on_signal(OrbSignal::FLATTEN_EOD, 19995.0, "supertrend_flip");
    x.process_cancels(); x.fill_markets(19995.0);
    x.assert_clean("flip_exit");
    enter(x, OrbSignal::SELL, 19994.0);                      // reversal
    x.fill(x.stop_id(), 20009.0);
    x.assert_clean("reversal_exit");
}
// 7. kill signal / SIGTERM
TEST(kill_signal_flatten) {
    MockExchange x; enter(x, OrbSignal::SELL, 20000.0);
    x.om.flatten_now("kill_signal", 20002.0);
    x.process_cancels(); x.fill_markets(20002.0);
    x.assert_clean("kill");
}
// 8. broker day-P&L halt: flatten + no new entry
TEST(broker_day_pnl_halt) {
    MockExchange x; enter(x, OrbSignal::BUY, 20000.0);
    x.risk.halt_external("broker_day_pnl -605 <= limit -500");
    x.om.flatten_now("broker_day_pnl_limit", 19990.0);
    x.process_cancels(); x.fill_markets(19990.0);
    x.assert_clean("broker_halt");
    x.om.on_signal(OrbSignal::BUY, 19990.0, "after_halt");
    ASSERT(x.om.is_flat()); ASSERT_EQ(x.working(), 0);       // refused
}
// 9. exit while the stop's server id is NOT mapped yet: client-id cancel fails,
//    the late mapping re-sends the cancel by server id
TEST(exit_before_stop_server_id_mapped) {
    MockExchange x; x.auto_map = false;
    x.om.on_signal(OrbSignal::BUY, 20000.0, "test_entry");
    std::string entry = x.om.position_snapshot().basket_id_entry;
    x.map(entry); x.fill(entry, 20000.0);
    std::string stop = x.stop_id();                          // unmapped
    x.om.flatten_now("eod_flatten", 20003.0);
    for (auto& o : x.orders) if (o.type == 2 && o.working) x.map(o.client);
    x.process_cancels();                                      // client-id cancel → FAILED
    x.fill_markets(20003.0);
    ASSERT_EQ(x.working(), 1);                                // the stop is still out there
    x.map(stop);                                              // late mapping → cancel by server id
    x.assert_clean("unmapped_stop");
}
// 10. Cancellation Failed while flat and the stop FIRES anyway: the guard unwinds it
TEST(cancel_failed_flat_stop_fires_is_unwound) {
    MockExchange x; x.auto_map = false;
    x.om.on_signal(OrbSignal::SELL, 20000.0, "test_entry");
    std::string entry = x.om.position_snapshot().basket_id_entry;
    x.map(entry); x.fill(entry, 20000.0);
    std::string stop = x.stop_id();
    x.om.flatten_now("eod_flatten", 19998.0);
    for (auto& o : x.orders) if (o.type == 2 && o.working) x.map(o.client);
    x.process_cancels(); x.fill_markets(19998.0);             // flat, stop still working
    x.fill(stop, 20015.0);                                    // orphan fires: BUY 2
    ASSERT_EQ(x.net, 2);
    for (auto& o : x.orders) if (o.working && o.type != 4) x.fill(o.client, 20015.0);   // guard's unwind
    x.reconcile(20015.0);
    x.assert_clean("cancel_failed_flat");
}
// 11. Cancellation Failed while IN the trade (breakeven replace): old stop re-adopted,
//     replacement cancelled, the re-adopted stop fills
TEST(cancel_failed_in_trade_readopts) {
    MockExchange x; x.auto_map = false;
    x.om.on_signal(OrbSignal::SELL, 20000.0, "test_entry");
    std::string entry = x.om.position_snapshot().basket_id_entry;
    x.map(entry); x.fill(entry, 20000.0);
    std::string old_stop = x.stop_id();
    x.om.check_trail_and_stop(19996.0);                       // BE → client-id cancel of old
    std::string repl = x.stop_id();
    x.process_cancels();                                      // FAILED → re-adopt old, cancel repl
    ASSERT_EQ(x.stop_id(), old_stop);
    x.map(repl); x.map(old_stop);
    x.process_cancels();
    ASSERT_EQ(x.working(), 1);
    x.fill(old_stop, 20015.0);
    x.assert_clean("cancel_failed_in_trade");
}
// 12. partial STOP fill (2 contracts in two prints): stay in the trade until complete
TEST(partial_stop_fill_waits_for_complete) {
    MockExchange x; enter(x, OrbSignal::BUY, 20000.0);
    std::string stop = x.stop_id();
    x.fill(stop, 19985.0, 1);                                  // cumulative 1/2
    ASSERT(!x.om.is_flat());                                   // still holding 1
    ASSERT_EQ(x.working(), 1);
    x.fill(stop, 19984.75, 1);                                 // cumulative 2/2 → closed
    x.assert_clean("partial_stop");
}
// 13. partial market EXIT fill
TEST(partial_market_exit_waits_for_complete) {
    MockExchange x; enter(x, OrbSignal::SELL, 20000.0);
    x.om.flatten_now("eod_flatten", 19995.0);
    x.process_cancels();
    std::string exit_b = x.om.position_snapshot().basket_id_exit;
    x.fill(exit_b, 19995.0, 1);
    ASSERT(!x.om.is_flat());
    x.fill(exit_b, 19995.25, 1);
    x.assert_clean("partial_exit");
}
// 14. partial ENTRY fill: no stop for contracts we do not hold, then the full stop
TEST(partial_entry_fill_then_complete) {
    MockExchange x;
    x.om.on_signal(OrbSignal::BUY, 20000.0, "test_entry");
    std::string entry = x.om.position_snapshot().basket_id_entry;
    x.fill(entry, 20000.0, 1);
    ASSERT(x.om.state() == PosState::PENDING_ENTRY);
    ASSERT_EQ(x.working(), 1);                                 // only the entry remainder
    x.fill(entry, 20000.25, 1);
    ASSERT(x.om.state() == PosState::LONG);
    x.fill(x.stop_id(), 19985.0);
    x.assert_clean("partial_entry");
}
// 15. partial fill where the COMPLETE never arrives: NetReconciler adopts the close
TEST(partial_stop_without_complete_reconciles) {
    MockExchange x; enter(x, OrbSignal::BUY, 20000.0);
    MockExchange::Ord* s = x.find(x.stop_id());
    s->filled = 2; s->working = false; x.net -= 2;             // exchange filled both…
    x.om.on_fill_notification(s->client, 19985.0, 1, false);   // …we only heard "1"
    ASSERT(!x.om.is_flat());
    x.reconcile(19985.0);                                      // tid=451: net 0 vs LONG 2
    x.process_cancels();
    x.assert_clean("partial_no_complete");
}
// 16. restart mid-trade: the new process knows nothing; exchange holds 2 + a live stop
TEST(restart_mid_trade_ghost_is_unwound_and_stop_cancelled) {
    MockExchange old; enter(old, OrbSignal::BUY, 20000.0);
    // process dies. New process: fresh order manager, same exchange state.
    MockExchange x; x.orders = old.orders; x.net = old.net; x.seq = 100;
    // executor startup: tid=351 snapshot → cancel every open order (Case A)
    for (auto& o : x.orders) if (o.working) { o.working = false; }
    // tid=451 snapshot: net 2, we are FLAT → unwind
    x.reconcile(20001.0);
    x.assert_clean("restart_mid_trade");
}
// 17. orphan: an order we never sent fills on our account while FLAT (the drill)
TEST(orphan_fill_while_flat_is_unwound) {
    MockExchange x;
    x.orders.push_back({"DRILL-orphan", "S999", 1, true, 1}); x.map("DRILL-orphan");
    x.fill("DRILL-orphan", 20000.0);
    ASSERT_EQ(x.net, 1);
    x.reconcile(20000.0);
    x.assert_clean("orphan");
}

// 18. 2026-09-24 01:15 regression: the exit fill is reported twice — tid=352 closes the
//     trade, then the tid=351 COMPLETE (same order, cumulative qty) lands AFTER the close
//     and arrives with no known role. It must be recognised as a duplicate: no ghost
//     halt, entries still allowed, account still flat.
TEST(duplicate_exit_report_after_close_is_not_a_ghost) {
    MockExchange x; enter(x, OrbSignal::BUY, 20000.0);
    x.om.flatten_now("stop_loss", 20001.0);
    x.process_cancels();
    std::string exit_b = x.om.position_snapshot().basket_id_exit;
    x.fill(exit_b, 20001.0);                                   // tid=352 per-fill: closes
    x.assert_clean("dup_first");
    MockExchange::Ord* o = x.find(exit_b);
    auto r = notif::handle_unowned_fill(x.om, o->client, o->server, MockExchange::ACCT, MockExchange::ACCT,
                                        20001.0, o->filled, [&](const std::string& w) { x.halts.push_back(w); });
    ASSERT(r == notif::UnownedFill::DUPLICATE);
    // executor_main asks this first so the routine second delivery is logged as a
    // duplicate, not as a CRITICAL unowned fill (live 2026-09-28: every exit alarmed).
    ASSERT(notif::unowned_fill_is_duplicate(x.om, o->client, o->server, o->filled));
    ASSERT(!notif::unowned_fill_is_duplicate(x.om, "MNQ-never-seen-1", "0", 1));
    ASSERT(!notif::unowned_fill_is_duplicate(x.om, o->client, o->server, o->filled + 1));
    ASSERT(!x.om.is_entry_halted());
    ASSERT(x.halts.empty());
    x.om.on_signal(OrbSignal::SELL, 20000.0, "next_trade");   // still allowed to trade
    ASSERT(x.om.state() == PosState::PENDING_ENTRY);
}
// 19. a stop the exchange REJECTS never worked: its DB guard is released at once
TEST(rejected_stop_releases_db_guard) {
    MockExchange x;
    std::vector<std::string> persisted, removed;
    x.om.set_cancel_persist_callbacks(
        [&](const std::string& b, bool) { persisted.push_back(b); },
        [&](const std::string& b) { removed.push_back(b); });
    enter(x, OrbSignal::BUY, 20000.0);
    std::string stop = x.stop_id();
    x.find(stop)->working = false;                             // exchange rejected it
    x.om.on_order_rejected(stop, "sell stop price must be below trade price");
    ASSERT(!removed.empty() && removed.back() == stop);
    x.om.flatten_now("stop_loss", 19999.0);                    // software SL covers
    x.process_cancels(); x.fill_markets(19999.0);
    x.assert_clean("rejected_stop");
}

// 20. audit #2: a "Cancellation Failed" for the PREVIOUS trade's stop (its client-id
//     cancel is re-sent at every close) must not touch the CURRENT trade's stop
TEST(cancel_failed_from_previous_trade_is_not_readopted) {
    MockExchange x; x.auto_map = false;
    x.om.on_signal(OrbSignal::BUY, 20000.0, "t1");
    std::string e1 = x.om.position_snapshot().basket_id_entry;
    x.map(e1); x.fill(e1, 20000.0);
    std::string stop1 = x.stop_id();                           // never mapped
    x.om.flatten_now("eod_flatten", 20002.0);                  // client-id cancel of stop1
    for (auto& o : x.orders) if (o.type != 4 && o.working) x.map(o.client);
    x.fill_markets(20002.0);                                   // trade 1 closed, recancel queued
    x.auto_map = true;
    x.om.on_signal(OrbSignal::SELL, 20001.0, "t2 reversal");  // trade 2 opens before the answers
    x.fill(x.om.position_snapshot().basket_id_entry, 20001.0);
    std::string stop2 = x.stop_id();
    x.process_cancels();                                       // stop1 → Cancellation Failed
    ASSERT_EQ(x.stop_id(), stop2);                             // current stop kept
    ASSERT(x.find(stop2)->working);
    x.map(stop1);                                              // late map → cancel by server id
    x.process_cancels();
    ASSERT(!x.find(stop1)->working);
    x.fill(stop2, 20016.0);
    x.assert_clean("prev_trade_cancel_failed");
}
// 21. audit #4: a stuck exit is cancelled by SERVER id before the retry
TEST(stuck_exit_retry_cancels_old_exit_by_server_id) {
    MockExchange x; enter(x, OrbSignal::BUY, 20000.0);
    x.om.flatten_now("eod_flatten", 19995.0);
    x.process_cancels();                                       // stop gone
    std::string old_exit = x.om.position_snapshot().basket_id_exit;
    x.om.retry_stuck_exit(19990.0);                            // old exit never filled
    x.process_cancels();
    ASSERT(!x.find(old_exit)->working);                        // really cancelled
    x.fill(x.om.position_snapshot().basket_id_exit, 19990.0);
    x.assert_clean("stuck_exit");
}
// 22. audit #4: EOD flatten while the entry is still pending cancels it by server id
TEST(flatten_pending_entry_cancels_by_server_id) {
    MockExchange x;
    x.om.on_signal(OrbSignal::SELL, 20000.0, "entry");
    x.flush();
    std::string entry = x.om.position_snapshot().basket_id_entry;
    x.om.flatten_now("eod_flatten", 20000.0);
    x.process_cancels();
    ASSERT(!x.find(entry)->working);
    x.assert_clean("pending_entry_flatten");
}
// 23. audit #4: entry cancelled before its server id mapped → re-sent when it maps
TEST(pending_entry_timeout_cancel_resent_on_late_map) {
    MockExchange x; x.auto_map = false;
    x.om.on_signal(OrbSignal::BUY, 20000.0, "entry");
    std::string entry = x.om.position_snapshot().basket_id_entry;
    ASSERT(x.om.pending_entry_timeout_check(0));               // times out → cancel by client id
    x.process_cancels();                                       // refused
    ASSERT(x.find(entry)->working);
    x.map(entry);                                              // late map → re-sent by server id
    x.assert_clean("entry_timeout_late_map");
}

// 24. audit #6: the OLD stop fires while its cancel (trail/breakeven replace) is in flight —
//     it is the exit: trade closed, replacement stop cancelled, nothing left
TEST(replaced_stop_fills_before_cancel_is_the_exit) {
    MockExchange x; enter(x, OrbSignal::SELL, 20000.0);
    std::string old_stop = x.stop_id();
    x.om.check_trail_and_stop(19996.0);                       // BE: cancel old, new stop sent
    x.flush();
    std::string repl = x.stop_id();
    ASSERT(repl != old_stop);
    x.cancel_reqs.clear();                                    // the old cancel has not landed…
    x.fill(old_stop, 20015.0);                                // …and the old stop fires
    ASSERT(x.om.is_flat());                                   // treated as the exit
    ASSERT(!x.om.is_entry_halted());
    x.assert_clean("replaced_stop_fill");                     // replacement cancelled
}
// 25. audit #5: an unwind that RESTS is retried — NetReconciler re-acts after retry_ms
TEST(net_reconciler_retries_a_mismatch_that_survives_the_unwind) {
    NetReconciler r;                                             // clocks are steady_clock ms (never 0)
    ASSERT(r.observe(false, 100000, 5000, 15000) == NetReconciler::Verdict::MISMATCH_WAIT);
    ASSERT(r.observe(false, 105000, 5000, 15000) == NetReconciler::Verdict::MISMATCH_ACT);
    ASSERT(r.observe(false, 110000, 5000, 15000) == NetReconciler::Verdict::MISMATCH_WAIT);
    ASSERT(r.observe(false, 120000, 5000, 15000) == NetReconciler::Verdict::MISMATCH_ACT);  // retry
    ASSERT(r.observe(false, 121000, 5000, 15000) == NetReconciler::Verdict::MISMATCH_WAIT);
    ASSERT(r.observe(true, 122000, 5000, 15000) == NetReconciler::Verdict::OK);
    NetReconciler once;                                        // retry_ms=0: old behaviour
    once.observe(false, 100000, 5000); ASSERT(once.observe(false, 105000, 5000) == NetReconciler::Verdict::MISMATCH_ACT);
    ASSERT(once.observe(false, 160000, 5000) == NetReconciler::Verdict::MISMATCH_WAIT);
}
// 26. audit #8: a PARTIAL entry (1 of 2 filled, rest working) is a consistent state —
//     the reconciler must not unwind it while the entry is still in flight
TEST(partial_entry_is_consistent_while_pending) {
    MockExchange x;
    x.om.on_signal(OrbSignal::SELL, 20000.0, "entry");
    std::string entry = x.om.position_snapshot().basket_id_entry;
    x.fill(entry, 20000.0, 1);
    ASSERT(x.om.net_qty_consistent(-1));
    ASSERT(x.om.net_qty_consistent(0) && x.om.net_qty_consistent(-2));
    ASSERT(!x.om.net_qty_consistent(+1) && !x.om.net_qty_consistent(-3));
    x.reconcile(20000.0);                                      // no action
    ASSERT_EQ(x.working(), 1);
    x.fill(entry, 20000.0, 1);
    x.fill(x.stop_id(), 20015.0);
    x.assert_clean("partial_entry_consistent");
}

int main() {
    std::cout << "test_trade_end_invariant — every trade end leaves 0 working orders and net 0\n";
    RUN(stop_loss_fill_long);
    RUN(stop_loss_fill_short);
    RUN(breakeven_stop_fill);
    RUN(trailing_stop_fill_after_steps);
    RUN(eod_session_end_flatten);
    RUN(trend_flip_exit_then_reversal);
    RUN(kill_signal_flatten);
    RUN(broker_day_pnl_halt);
    RUN(exit_before_stop_server_id_mapped);
    RUN(cancel_failed_flat_stop_fires_is_unwound);
    RUN(cancel_failed_in_trade_readopts);
    RUN(partial_stop_fill_waits_for_complete);
    RUN(partial_market_exit_waits_for_complete);
    RUN(partial_entry_fill_then_complete);
    RUN(partial_stop_without_complete_reconciles);
    RUN(restart_mid_trade_ghost_is_unwound_and_stop_cancelled);
    RUN(orphan_fill_while_flat_is_unwound);
    RUN(duplicate_exit_report_after_close_is_not_a_ghost);
    RUN(rejected_stop_releases_db_guard);
    RUN(cancel_failed_from_previous_trade_is_not_readopted);
    RUN(stuck_exit_retry_cancels_old_exit_by_server_id);
    RUN(flatten_pending_entry_cancels_by_server_id);
    RUN(pending_entry_timeout_cancel_resent_on_late_map);
    RUN(replaced_stop_fills_before_cancel_is_the_exit);
    RUN(net_reconciler_retries_a_mismatch_that_survives_the_unwind);
    RUN(partial_entry_is_consistent_while_pending);
    std::cout << "\n" << (tests_run - tests_failed) << "/" << tests_run << " passed\n";
    return tests_failed ? 1 : 0;
}
