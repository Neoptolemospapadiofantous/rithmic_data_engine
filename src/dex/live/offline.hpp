#pragma once
/*  offline.hpp — `mm_live --mode offline`: one recorded Lighter archive day through the WHOLE live pipeline (engine,
    signer, sender, account stream parser, reconciler, dead-man, journal) against the in-process FakeVenue. No socket
    is opened anywhere: HTTP and both WebSockets are in-memory (fake_venue.hpp), time is the archive's receive time.

    At the end the engine shuts down exactly as on SIGTERM and the invariants are checked:
      * the exchange has no open order and no scheduled cancel-all left (the dead-man was aborted);
      * engine position == exchange position == Σ journalled fills (signed);
      * journalled fills (unique trade ids) == the exchange's fills == fills the engine applied;
      * every order the engine tracked is terminal; no held (unattributed) fill;
      * the engine's nonce stream agrees with the exchange's next nonce; nothing is frozen or unconfirmed.
    Any violation → exit 1.                                                                                     */

#include "dex/live/driver.hpp"
#include "dex/live/engine.hpp"
#include "dex/live/fake_venue.hpp"
#include "dex/live/mm_live_config.hpp"
#include "zstd_daily.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace lighter::live {

struct OfflineResult {
    bool ok = false;
    std::vector<std::string> violations;
    nlohmann::json summary = nlohmann::json::object();
};

// Unique journalled fills (trade id → signed qty) across every journal file of `label` in `dir`.
inline std::map<int64_t, int64_t> journal_fills(const std::string& dir, const std::string& label) {
    std::map<int64_t, int64_t> out;
    std::error_code ec;
    for (const auto& f : std::filesystem::directory_iterator(dir, ec)) {
        std::string n = f.path().filename().string();
        if (n.rfind(label + "-", 0) != 0 || f.path().extension() != ".jsonl") continue;
        std::ifstream in(f.path());
        std::string line;
        while (std::getline(in, line)) {
            auto j = nlohmann::json::parse(line, nullptr, false);
            if (!j.is_object() || j.value("ev", "") != "fill") continue;
            int64_t q = j.value("qty", int64_t{0});
            out[j.value("trade_id", int64_t{0})] = j.value("side", "") == "sell" ? -q : q;
        }
    }
    return out;
}

struct OfflineOptions {
    std::string archive;
    int64_t max_events = 0;        // 0 = the whole file
    int64_t start_position = 0;    // the fake account's position at start (adoption test)
    uint32_t seed = 1;
    double late_fill_prob = 0.2;   // fills inside the cancel delay delivered after the confirm
    std::function<void(const std::string&)> log;
    std::function<bool()> stop;    // SIGTERM: stop feeding, shut down cleanly, check the invariants
};

inline OfflineResult run_offline_soak(const MmLiveConfig& mc, Signer& signer, RiskGovernor& risk, Journal& journal,
                                      Alerter& alerts, int64_t run_id, const OfflineOptions& o) {
    OfflineResult res;
    const EngineConfig& ec = mc.engine;
    ZstdLines in(o.archive);
    std::string line;
    // first message → start time
    int64_t t0 = 0;
    std::vector<std::pair<int64_t, nlohmann::json>> lead;
    while (in.next(line)) {
        auto j = nlohmann::json::parse(line, nullptr, false);
        if (!j.is_object() || !j.contains("m")) continue;
        t0 = j.value("r", int64_t{0}) / 1000;
        lead.push_back({t0, j["m"]});
        break;
    }
    if (!t0) { res.violations.push_back("archive empty or unreadable: " + o.archive); return res; }

    ManualClock clock(t0);
    fake::FakeExchange ex(ec.account_index, t0, o.seed);
    ex.set_next_nonce(ec.api_key_index, 1);
    ex.set_late_fill_prob(o.late_fill_prob);
    ex.set_min_cancel_all_ms(5 * 60'000);   // Lighter's MinOrderCancelAllPeriod
    ex.set_schedule_mode(fake::FakeExchange::ScheduleMode::Replace);   // UNCONFIRMED on Lighter; the probe tells
    __int128 min_units = __int128(ec.spec.min_quote_micro) * pow10i(ec.spec.size_decimals + ec.spec.price_decimals) / kMicro;
    ex.set_market(ec.spec.market_index, ec.spec.min_base_amount, int64_t(min_units));
    ex.set_position(ec.spec.market_index, o.start_position);
    fake::FakeVenue venue(ex, ec.spec, ec.api_key_index, [&clock] { return clock.now_ms(); });
    venue.collateral_micro = mc.offline_collateral_micro;
    char imf[32];
    std::snprintf(imf, sizeof imf, "%.2f", 100.0 / ec.risk.leverage);
    venue.imf = imf;
    clock.on_sleep = [&](int64_t t) { ex.set_now(t); };
    fake::FakeAccountWs aws(venue);
    fake::QueueWs pws;

    auto log = [&](const std::string& s) { if (o.log) o.log(s); };
    Engine eng(ec, Engine::Deps{signer, venue, clock, risk, journal, alerts, {}, log}, run_id);
    for (auto& [t, m] : lead) venue.on_public(m);
    if (!eng.start()) { res.violations.push_back("engine refused to start"); return res; }
    Driver drv(eng, aws, pws, clock, log);
    drv.step();

    int64_t events = 1;
    // Per-step invariants on the EXCHANGE's view (review F7): at no instant may the account hold more than one of our
    // orders per side, an order outside the size rules, a position above the cap or gross exposure above the cap.
    std::map<std::string, int64_t> step_violations;
    std::map<std::string, std::string> first_violation;
    int64_t max_order_base = 0, steps_checked = 0, returns_to_flat = 0;
    bool was_open_pos = false;
    const SizingRules sizing{ec.risk.quote_micro, ec.risk.max_order_micro, 10'500};
    auto step_check = [&]() {
        ++steps_checked;
        auto bad = [&](const std::string& kind, const std::string& what) {
            if (step_violations[kind]++ == 0) first_violation[kind] = what + " at t=" + std::to_string(clock.now_ms());
        };
        int bids = 0, asks = 0;
        int64_t bid_rem = 0, ask_rem = 0, top_px = 0;
        for (const auto& o : ex.resting(ec.spec.market_index)) {
            if (o.client_index <= 0) continue;
            (o.is_ask ? asks : bids)++;
            max_order_base = std::max(max_order_base, o.base);
            (o.is_ask ? ask_rem : bid_rem) += o.base - o.filled;
            top_px = std::max(top_px, o.price);
            if (o.reduce_only) continue;
            std::string why = validate_order(ec.spec, sizing, o.base, o.price);
            if (!why.empty()) bad("order_size", "order " + std::to_string(o.client_index) + ": " + why);
        }
        if (bids > 1 || asks > 1) bad("two_orders_one_side", std::to_string(bids) + " bids / " + std::to_string(asks) + " asks resting");
        int64_t pos = ex.position(ec.spec.market_index);
        if (max_order_base > 0 && std::llabs(pos) > int64_t(ec.risk.max_position_quotes) * max_order_base &&
            std::llabs(pos) > std::llabs(o.start_position))
            bad("position_cap", "position " + std::to_string(pos) + " > " + std::to_string(ec.risk.max_position_quotes) + " × " + std::to_string(max_order_base));
        // the gross cap's definition (engine exposure_after): worst-case net exposure, valued at max(order px, mid)
        int64_t worst = std::max<int64_t>({pos + bid_rem, -pos + ask_rem, 0});
        int64_t gross = ec.spec.notional_micro_ceil(worst, std::max(top_px, venue.mid()));
        if (gross > ec.risk.max_gross_micro * 105 / 100)   // 5 %: a held position re-prices with the market
            bad("gross_cap", "worst-case exposure $" + std::to_string(double(gross) / kMicro));
        if (pos != 0) was_open_pos = true;
        else if (was_open_pos) { was_open_pos = false; ++returns_to_flat; }
    };
    auto feed = [&](int64_t t, const nlohmann::json& m) {
        clock.set(t);
        ex.set_now(clock.now_ms());
        venue.on_public(m);
        pws.push(m.dump());
        drv.step();
        step_check();
    };
    for (auto& [t, m] : lead) feed(t, m);
    while (in.next(line)) {
        if (o.max_events && events >= o.max_events) break;
        if (o.stop && o.stop()) { log("signal: stopping the soak early"); break; }
        auto j = nlohmann::json::parse(line, nullptr, false);
        if (!j.is_object() || !j.contains("m")) continue;
        int64_t t = j.value("r", int64_t{0}) / 1000;
        if (t <= 0) continue;
        feed(t, j["m"]);
        ++events;
    }
    int64_t t_end = clock.now_ms();
    // shutdown exactly as on SIGTERM, then let the venue deliver what is still in flight
    bool clean = eng.shutdown("offline: end of archive");
    for (int k = 0; k < 20; ++k) { clock.sleep_ms(500); drv.step(); }
    eng.tick(clock.now_ms());

    // ── invariants ────────────────────────────────────────────────────────────────────────────────────────────
    auto fail = [&](const std::string& v) { res.violations.push_back(v); };
    for (const auto& [kind, n] : step_violations)
        if (n) fail("per-step " + kind + " violated " + std::to_string(n) + " time(s), first: " + first_violation[kind]);
    if (!clean) fail("shutdown was not clean");
    if (ex.open_orders() != 0) fail(std::to_string(ex.open_orders()) + " open order(s) left on the exchange");
    if (ex.scheduled_cancel_alls() != 0) fail("a scheduled cancel-all (dead-man) is still queued after shutdown");
    int64_t ex_pos = ex.position(ec.spec.market_index);
    if (eng.position() != ex_pos) fail("engine position " + std::to_string(eng.position()) + " != exchange " + std::to_string(ex_pos));
    auto jf = journal_fills(ec.journal_dir, ec.label);
    int64_t jsum = o.start_position;
    for (auto& [id, q] : jf) jsum += q;
    if (jsum != ex_pos) fail("Σ journalled fills " + std::to_string(jsum) + " != exchange position " + std::to_string(ex_pos));
    if (int64_t(jf.size()) != int64_t(ex.trades().size())) fail("journalled fills " + std::to_string(jf.size()) + " != exchange fills " + std::to_string(ex.trades().size()));
    if (eng.fills() != int64_t(ex.trades().size())) fail("engine applied " + std::to_string(eng.fills()) + " fills, exchange has " + std::to_string(ex.trades().size()));
    for (const auto& [coi, ord] : eng.tracker().all())
        if (is_fillable(ord.state)) fail("order " + std::to_string(coi) + " still " + state_name(ord.state));
    if (eng.tracker().held_fills() != 0) fail("held (unattributed) fills remain");
    if (eng.nonces().frozen()) fail("nonce stream frozen at the end");
    if (!eng.nonces().unconfirmed().empty()) fail("unconfirmed nonces at the end");
    if (eng.nonces().peek_next() != ex.next_nonce(ec.api_key_index))
        fail("engine next nonce " + std::to_string(eng.nonces().peek_next()) + " != exchange " + std::to_string(ex.next_nonce(ec.api_key_index)));

    res.ok = res.violations.empty();
    res.summary = {{"archive", o.archive}, {"events", events}, {"hours", double(t_end - t0) / 3.6e6},
                   {"fills", ex.trades().size()}, {"journal_fills", jf.size()}, {"position", ex_pos},
                   {"sends", eng.sends()}, {"exchange_requests", ex.accepted_requests()}, {"next_nonce", ex.next_nonce(ec.api_key_index)},
                   {"deadman_arms", eng.deadman_arms()}, {"dead_man_fired", ex.dead_man_fired()},
                   {"reconciles", eng.reconciles()}, {"mismatch_acts", eng.mismatch_acts()},
                   {"late_fills", eng.tracker().late_fills()}, {"duplicate_fills", eng.tracker().duplicate_fills()},
                   {"post_only_rejects", eng.tracker().post_only_rejects()}, {"rate_limited", eng.rate_limited_events()},
                   {"public_hits", venue.public_hits}, {"account_reconnects", drv.account_reconnects()},
                   {"equity_usd", double(venue.portfolio_micro()) / kMicro}, {"halts", eng.halt_reasons()},
                   {"steps_checked", steps_checked}, {"returns_to_flat", returns_to_flat}, {"violations", res.violations}};
    return res;
}

}  // namespace lighter::live
