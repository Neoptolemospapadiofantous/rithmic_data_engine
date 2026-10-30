#pragma once
/*  engine.hpp — the live Lighter market maker (mm_live), single-threaded and event-driven.

    The engine owns every decision and no socket. A driver (driver.hpp) moves WebSocket frames in and out and calls
    tick(); REST and sendTx go through an injected HttpClient. The same engine therefore runs against mainnet
    (net.hpp), against the in-process FakeVenue in the offline soak, and in the tests. It runs on ONE thread — the
    dedicated signing/sending thread (a signature costs ~0.43 ms); WebSocket I/O happens on the transports' own
    threads.

    Decision path: the public book → hl::Simulator::quote_target / quote_allowed (the backtest's own code, fed the
    EXCHANGE position as inventory) → one post-only order per side, kept while within the requote band, cancelled
    when the target moves or quoting stops; a create waits until the old order on its side is terminal.

    Safety:
      * Inventory = the exchange's position. Fills are applied by trade id with market and side (they move the position
        the moment they arrive, also for an order the tracker does not know); the positions stream and REST are
        compared with it, and a difference that survives 3 s is adopted (exchange wins), quoting halts, an alert goes
        out; three adoptions in a UTC day make the halt sticky.
      * Every tx: nonce reserved → signed → journalled (fsync) → sent. API code 200 = Accepted-pending until the
        sequencer result arrives on account_tx. A timeout is UNKNOWN (never "failed"): the nonce stream freezes,
        nextNonce + reconciliation decide. 429/405 → the limiter backs off 60 s.
      * Reconciler every 10 s (5 s while the account stream is down, at once after a reconnect, an Unknown order, a
        freeze or a 429): exchange open orders vs ours (an order we do not hold is cancelled; one not placed by any
        run of ours halts quoting — "account used by something else"), position (above), leverage (the market's
        IMF must equal the configured leverage — the engine never changes it), collateral guard.
      * Dead-man: a scheduled cancel-all re-armed every 20 s (schedule alone; abort + schedule only after a 21712),
        deadline = server time + 5 min 15 s; quoting needs a CONFIRMED deadline ≥ 60 s ahead. Aborted on shutdown only
        when the cancel-all is confirmed applied and REST shows no open order — otherwise it stays armed.
      * Journal failure, KILL file, a loss stop, an unowned order or fill, a self-trade, a liquidation: sticky halt,
        cancel-all, alert (KILL and loss stops also flatten with reduce-only IOC capped 0.5 % through the touch).
      * Start: cancel-all, wait until REST shows no open order, adopt the exchange position, arm the dead-man.
        Stop: cancel-all, wait (≤ 10 s), abort the dead-man, status.                                             */

#include "dex/lighter_feed.hpp"
#include "dex/mm_paper.hpp"
#include "dex/bn_pull.hpp"
#include "dex/live/account_feed.hpp"
#include "dex/live/alerts.hpp"
#include "dex/live/gateway.hpp"
#include "dex/live/gateway_config.hpp"
#include "dex/live/journal.hpp"
#include "dex/live/market_spec.hpp"
#include "dex/live/nonce.hpp"
#include "dex/live/order_state.hpp"
#include "dex/live/probe.hpp"
#include "dex/live/rest_api.hpp"
#include "dex/live/risk.hpp"
#include "dex/live/sender.hpp"
#include "dex/live/signer.hpp"
#include "dex/live/transport.hpp"
#include "dex/live/tx_scheduler.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace lighter::live {

// ── clocks ────────────────────────────────────────────────────────────────────────────────────────────────────
struct Clock {
    virtual ~Clock() = default;
    virtual int64_t now_ms() = 0;
    virtual void sleep_ms(int64_t ms) = 0;
};
struct WallClock : Clock {
    int64_t now_ms() override { return wall_now_ms(); }
    void sleep_ms(int64_t ms) override { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
};
// Tests and the offline soak: time moves only when told to. on_sleep lets the in-process venue run while the
// engine waits (e.g. the 300 ms cancel delay during startup).
struct ManualClock : Clock {
    explicit ManualClock(int64_t t0) : t(t0) {}
    int64_t now_ms() override { return t; }
    void sleep_ms(int64_t ms) override { t += ms; if (on_sleep) on_sleep(t); }
    void set(int64_t ms) { if (ms > t) t = ms; }
    int64_t t;
    std::function<void(int64_t)> on_sleep;
};

// Server clock estimate: median of (exchange timestamp − receive time) over the last 200 book messages (R-CLK-2).
class ClockOffset {
public:
    void push(int64_t exch_ms, int64_t recv_ms) {
        if (exch_ms <= 0) return;
        w_.push_back(exch_ms - recv_ms);
        if (w_.size() > 200) w_.pop_front();
        if (w_.size() >= 20 && w_.size() % 10 == 0) {
            int64_t m = median();
            if (have_ && std::llabs(m - last_) > 1'000) ++jumps_;
            last_ = m;
            have_ = true;
        }
    }
    int64_t median() const {
        if (w_.empty()) return 0;
        std::vector<int64_t> v(w_.begin(), w_.end());
        std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(v.size() / 2), v.end());
        return v[v.size() / 2];
    }
    int64_t offset() const { return have_ ? last_ : median(); }
    int64_t jumps() const { return jumps_; }

private:
    std::deque<int64_t> w_;
    int64_t last_ = 0, jumps_ = 0;
    bool have_ = false;
};

// ── config ────────────────────────────────────────────────────────────────────────────────────────────────────
// Overrides of the frozen strategy (mmpaper::frozen_params) from config "engine.strategy" (2026-10-27, founder: the
// strategy that goes live must be the optimised one). −1 = keep the frozen value. Ranges are checked in
// build_mm_live_config; only these knobs exist — size, inventory cap and fees stay with the risk config / the venue.
struct StrategyOverride {
    double half_width_ticks = -1, min_spread_ticks = -1, imb_pull = -1, pause_move_ticks = -1, requote_ticks = -1;
    int64_t pause_ms = -1;
    // Binance-lead pull (src/dex/bn_pull.hpp, 2026-10-28): 0 = off. Not a hl::Params knob — the engine drives the
    // quoter's overlay from a Binance bookTicker stream (Driver's reference WebSocket).
    int64_t bn_pull_w_ms = 0, bn_pull_hold_ms = 0;
    double bn_pull_x_bps = 0;
    bnpull::Params bn_pull() const { bnpull::Params b; b.w_ms = bn_pull_w_ms; b.x_bps = bn_pull_x_bps; b.hold_ms = bn_pull_hold_ms; return b; }
    void apply(hl::Params& p) const {
        if (half_width_ticks >= 0) p.half_width_ticks = half_width_ticks;
        if (min_spread_ticks >= 0) p.min_spread_ticks = min_spread_ticks;
        if (imb_pull >= 0) p.imb_pull = imb_pull;
        if (pause_move_ticks >= 0) p.pause_move_ticks = pause_move_ticks;
        if (pause_ms >= 0) p.pause_ms = pause_ms;
        if (requote_ticks >= 0) p.requote_ticks = requote_ticks;
    }
};

struct EngineConfig {
    Mode mode = Mode::Offline;
    std::string label = "lighter_main";
    int64_t account_index = 0;
    int api_key_index = 0;
    MarketSpec spec;
    int hl_sz_decimals = 2;
    StrategyOverride strategy;                          // engine.strategy (empty = the frozen setting)
    RiskConfig risk;
    std::string state_dir, journal_dir, status_file;
    int64_t max_collateral_micro = 50 * kMicro;
    double max_margin_pct = 30.0;                       // pull quotes while margin usage is above this
    int rate_budget = 54, rate_reserve = 24;            // ALL requests (REST + sendTx): quoting may use budget − reserve/min
    int send_budget = 30, send_reserve = 6;             // sendTx requests alone (the 40/min per-tx-type limit is unconfirmed)
    int max_txs_per_batch = 15;
    int64_t ack_timeout_ms = 3'000;
    int64_t reconcile_every_ms = 10'000, reconcile_down_every_ms = 5'000, reconcile_min_gap_ms = 2'000;
    // A periodic reconcile runs its REST reads on the engine thread: one slow read (2.6–3.7 s, 10 times on
    // 2026-10-28) tripped the 2 s stall guard and cost 10 s without quotes. Periodic reconciles use this per-read
    // timeout instead of the client's 8 s; after this many consecutive timed-out ones the next uses the full timeout,
    // so the REST equity the loss stops run on can never go stale. 0 = off.
    int reconcile_fast_timeout_ms = 800, reconcile_fast_max_fails = 3;
    int64_t deadman_rearm_ms = 20'000;
    int64_t deadman_window_ms = 315'000;                // ≥ Lighter's 5 min MinOrderCancelAllPeriod + margin
    int64_t order_expiry_ms = 600'000;                  // post-only quotes expire after 10 min (2nd dead-man layer)
    int64_t feed_silence_ms = 15'000, stale_ms = 120'000;
    int64_t status_every_ms = 5'000;
    int64_t unknown_alert_ms = 30'000;
    int64_t mismatch_grace_ms = 3'000;
    int64_t accepted_settle_ms = 20'000;                // accepted-pending with no sequencer result this long → nextNonce
    int flatten_max_tries = 5;
    int flatten_cap_bp = 50;                            // 0.5 % through the touch
    int64_t shutdown_wait_ms = 10'000;
};

// Persisted, monotonic run id (client_order_index = run_id << 20 | seq never repeats across runs).
inline int64_t next_run_id(const std::string& state_dir) {
    std::filesystem::create_directories(state_dir);
    std::string p = state_dir + "/run_id";
    int64_t last = 0;
    { std::ifstream f(p); f >> last; }
    int64_t next = last + 1;
    std::string tmp = p + ".tmp";
    { std::ofstream f(tmp, std::ios::trunc); f << next << "\n"; if (!f) throw std::runtime_error("cannot write " + tmp); }
    if (std::rename(tmp.c_str(), p.c_str()) != 0) throw std::runtime_error("cannot persist run id");
    return next;
}

// ── engine ────────────────────────────────────────────────────────────────────────────────────────────────────
class Engine : public AccountFeed::Sink {
public:
    struct Deps {
        Signer& signer;
        HttpClient& http;
        Clock& clock;
        RiskGovernor& risk;
        Journal& journal;
        Alerter& alerts;
        std::function<bool(int64_t now_s)> armed;   // ArmingGate (probe/live); offline: always true
        std::function<void(const std::string&)> log;
    };

    Engine(EngineConfig cfg, Deps d, int64_t run_id)
        : cfg_(std::move(cfg)), d_(std::move(d)), run_id_(run_id), alloc_(run_id), nm_(cfg_.api_key_index),
          limiter_(cfg_.rate_budget, cfg_.rate_reserve), send_limiter_(cfg_.send_budget, cfg_.send_reserve),
          batcher_(cfg_.max_txs_per_batch),
          feed_(cfg_.account_index, Specs{{cfg_.spec.market_index, cfg_.spec}},
                [this](int64_t deadline, int64_t now_s) { return d_.signer.create_auth_token(deadline, now_s, cfg_.api_key_index, cfg_.account_index); }),
          rest_(d_.http, cfg_.account_index, Specs{{cfg_.spec.market_index, cfg_.spec}},
                [this] { return feed_.rest_token(d_.clock.now_ms()); },
                RestGate{[this] { return limiter_.can_send(d_.clock.now_ms(), true); },
                         [this] { limiter_.record(d_.clock.now_ms()); persist_rate(d_.clock.now_ms()); },
                         [this] { on_rate_limited(d_.clock.now_ms(), "REST"); }}),
          sender_(d_.http), quoter_(quoter_params()), bn_(cfg_.strategy.bn_pull()) {
        sizing_.quote_micro = cfg_.risk.quote_micro;
        sizing_.cap_micro = cfg_.risk.max_order_micro;
        d_.risk.register_market(cfg_.spec.market_index);
        load_rate(d_.clock.now_ms());
    }

    // ── lifecycle ─────────────────────────────────────────────────────────────────────────────────────────────
    // Startup (R-START-1): spec check, capital guard, nonce, cancel-all + wait for zero open orders, adopt the
    // exchange position, trade-id watermark, risk baseline, dead-man. Returns false on a fatal refusal.
    bool start() {
        int64_t now = d_.clock.now_ms();
        try {
            d_.journal.open_for(now);
            jrec({{"ev", "start"}, {"run_id", run_id_}, {"mode", mode_name(cfg_.mode)}, {"label", cfg_.label},
                  {"market", cfg_.spec.symbol}, {"market_index", cfg_.spec.market_index}}, true);
        } catch (const JournalError& e) { return refuse("journal unusable at start: " + std::string(e.what())); }
        auto spec = rest_.order_book_details(cfg_.spec.market_index);
        if (!spec) return refuse("orderBookDetails unavailable (" + rest_.last_error() + ")");
        if (spec->size_decimals != cfg_.spec.size_decimals || spec->price_decimals != cfg_.spec.price_decimals ||
            spec->min_base_amount != cfg_.spec.min_base_amount || spec->min_quote_micro != cfg_.spec.min_quote_micro ||
            (!spec->symbol.empty() && spec->symbol != cfg_.spec.symbol))
            return refuse("market metadata differs from the config (" + spec->symbol + ")");
        auto acct = rest_.account();
        if (!acct) return refuse("account unavailable (" + rest_.last_error() + ")");
        if (acct->collateral_micro > cfg_.max_collateral_micro)
            return refuse("capital guard: collateral $" + usd(acct->collateral_micro) + " above max $" + usd(cfg_.max_collateral_micro));
        for (const auto& p : acct->positions)
            if (p.market != cfg_.spec.market_index && p.nonzero()) return refuse("position in an unconfigured market " + std::to_string(p.market));
        auto nn = rest_.next_nonce(cfg_.api_key_index);
        if (!nn) return refuse("nextNonce unavailable (" + rest_.last_error() + ")");
        nm_.resync(*nn);
        jrec({{"ev", "nonce_sync"}, {"next", *nn}}, true);
        // cancel-all, then wait until the exchange shows no open order
        bool sent = send_cancel_all_now("startup");
        if (!sent || !wait_no_open_orders(10'000)) {
            // review 2: retry once; if orders still rest (or REST cannot show otherwise), leave a SCHEDULED cancel-all
            // armed before giving up, so nothing stays on the book unprotected after this process exits
            log("[REC] open orders remain after the startup cancel-all: retrying once");
            bool retry = send_cancel_all_now("startup retry");
            if (!retry || !wait_no_open_orders(10'000)) {
                int64_t t = d_.clock.now_ms();
                Intent sc; sc.kind = IntentKind::CancelAll; sc.market = kNilMarketIndex; sc.ca_tif = CancelAllTif::Scheduled;
                sc.ca_time_ms = server_now(t) + cfg_.deadman_window_ms;
                bool armed = dispatch({sc}, t, Origin::DeadMan, false);
                return refuse(std::string("open orders remain 20 s after two startup cancel-alls; a scheduled cancel-all ") +
                              (armed ? "was sent (deadline in " + std::to_string(cfg_.deadman_window_ms / 1000) + " s)"
                                     : "could NOT be sent — cancel by hand in the Lighter app NOW"));
            }
        }
        acct = rest_.account();   // re-read: a fill may have landed during the cancel delay
        if (!acct) return refuse("account unavailable after cancel-all");
        int64_t pos = 0;
        for (const auto& p : acct->positions) {
            if (p.market == cfg_.spec.market_index && p.market_known) {
                pos = p.base; imf_ = p.imf; have_imf_ = true; entry_px_ = p.entry_px; entry_pos_ = p.base;
            }
        }
        tracker_.set_position(cfg_.spec.market_index, pos);
        exch_pos_ = pos;
        have_exch_pos_ = true;
        auto tr = rest_.trades_since(INT64_MAX, 1, 1);
        if (!tr) return refuse("trades unavailable (" + rest_.last_error() + ")");
        watermark_ = last_trade_id_ = rest_trade_cursor_ = tr->newest_id;
        collateral_ = acct->collateral_micro;
        equity_ = acct->portfolio_micro;
        equity_known_ = true;
        now = d_.clock.now_ms();
        try { if (d_.risk.on_equity(equity_, now / 1000) == RiskAction::CancelAllAndFlatten) risk_stop(now); }
        catch (const RiskError& e) { return refuse(std::string("risk state: ") + e.what()); }
        check_leverage(now);
        jrec({{"ev", "adopt"}, {"position", pos}, {"trade_watermark", watermark_}, {"collateral_micro", collateral_},
              {"equity_micro", equity_}, {"imf", imf_}}, true);
        if (pos != 0) alert("adopt", "start: adopted exchange position " + fmt_base(pos) + " " + cfg_.spec.symbol, now);
        started_ = true;
        // the dead-man is armed by tick() once the account stream is live, so its sequencer result is seen
        alert("start", "start " + std::string(mode_name(cfg_.mode)) + " " + cfg_.label + " " + cfg_.spec.symbol + " run " +
                       std::to_string(run_id_) + " position " + fmt_base(pos) + ", equity $" + usd(equity_), now);
        next_reconcile_ms_ = now + 1'000;
        return true;
    }

    // SIGTERM path (R-DMS-5): stop quoting, cancel-all, wait for zero open orders, abort the dead-man, final status.
    bool shutdown(const std::string& why) {
        int64_t now = d_.clock.now_ms();
        stopping_ = true;
        batcher_.drop_quoting();
        log("shutdown: " + why);
        bool clean = true;
        std::string detail;
        if (started_) {
            if (nm_.frozen()) resolve_nonce(now, true);
            bool sent = send_cancel_all_now("shutdown");
            int64_t ca_nonce = sent ? nm_.peek_next() - 1 : -1;
            bool none_open = sent && wait_no_open_orders(cfg_.shutdown_wait_ms);
            // R-DMS-5 + review F1: the dead-man is aborted ONLY when the cancel-all was applied by the sequencer (the
            // server's nextNonce moved past it) AND REST shows zero open orders. Otherwise it stays armed — whatever
            // is left on the book is cancelled by the exchange at the deadline — and the stop is reported unclean.
            auto nn = none_open ? rest_.next_nonce(cfg_.api_key_index) : std::nullopt;
            bool applied = nn && ca_nonce >= 0 && *nn > ca_nonce;
            now = d_.clock.now_ms();
            if (applied && none_open) {
                Intent ab; ab.kind = IntentKind::CancelAll; ab.market = kNilMarketIndex; ab.ca_tif = CancelAllTif::Abort;
                if (dispatch({ab}, now, Origin::DeadMan, false)) { deadman_aborted_ = true; detail = "orders cancelled, dead-man aborted"; }
                else { clean = false; detail = "orders cancelled; dead-man abort NOT sent — it stays armed"; }
            } else {
                clean = false;
                detail = std::string(!sent ? "cancel-all NOT accepted" : !none_open ? "open orders remain after 10 s"
                                                                                   : "cancel-all not confirmed applied") +
                         " — dead-man LEFT ARMED, check the account";
            }
        }
        try { jrec({{"ev", "stop"}, {"why", why}, {"clean", clean}, {"detail", detail}, {"position", tracker_.position(mkt())}}, true); } catch (...) {}
        write_status(d_.clock.now_ms(), true);
        alert("stop", std::string("stop (") + why + "): " + (clean ? detail : "NOT clean: " + detail) +
                      ", position " + fmt_base(tracker_.position(mkt())), d_.clock.now_ms());
        return clean;
    }

    // ── account WebSocket ─────────────────────────────────────────────────────────────────────────────────────
    void on_account_open(int64_t now) { feed_.on_open(now); account_open_ = true; jrec_soft({{"ev", "account_ws"}, {"state", "open"}}); }
    void on_account_frame(const std::string& text, int64_t now) {
        auto out = feed_.on_frame(text, now, *this);
        for (auto& f : out) account_out_.push_back(std::move(f));
    }
    void on_account_closed(int64_t now) {
        bool was_live = feed_.live();
        feed_.on_closed(now);
        account_open_ = false;
        have_ws_pos_ = false;          // the streamed position is stale from here until the next snapshot
        backfill_needed_ = true;
        jrec_soft({{"ev", "account_ws"}, {"state", "closed"}});
        if (started_ && was_live) {
            // R-MD-6: no new orders, cancel-all, REST reconcile every 5 s until the stream is back
            set_condition("account_feed", true, false, Pull::CancelAll, "account stream down", now);
            next_reconcile_ms_ = std::min(next_reconcile_ms_, now + 500);
        }
    }
    std::vector<std::string> take_account_out() { auto v = std::move(account_out_); account_out_.clear(); return v; }
    bool account_reconnect_wanted() const { return feed_.want_reconnect(); }
    std::string account_reconnect_reason() const { return feed_.reconnect_reason(); }
    void clear_account_reconnect() { feed_.clear_reconnect(); }
    const AccountFeed& account_feed() const { return feed_; }

    // ── public WebSocket (order book) ────────────────────────────────────────────────────────────────────────
    void on_public_open(int64_t now) {
        public_open_ = true;
        public_reconnect_ = false;
        last_public_ms_ = now;
        last_public_ping_ms_ = now;
        book_ = lighter::Book{};
        book_snapshot_ = false;
        for (const char* ch : {"order_book/", "trade/"})
            public_out_.push_back(std::string(R"({"type":"subscribe","channel":")") + ch + std::to_string(mkt()) + "\"}");
    }
    void on_public_frame(const std::string& text, int64_t now) {
        auto j = nlohmann::json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object()) return;
        on_public_json(j, now);
    }
    void on_public_json(const nlohmann::json& j, int64_t now) {
        last_public_ms_ = now;
        if (j.value("type", "") == "ping") { public_out_.push_back(R"({"type":"pong"})"); return; }
        if (lighter::market_of(j, "order_book") != mkt()) return;
        auto r = book_.apply(j);
        if (r == lighter::Book::Apply::Gap) {
            ++gaps_;
            book_snapshot_ = false;
            if (started_) {
                // R-MD-1: pull this market's quotes FIRST, then resubscribe for a fresh snapshot
                cancel_market_now("book sequence gap", now);
                set_condition("book_gap", true, false, Pull::None, "order book sequence gap: resubscribing", now);
            }
            public_reconnect_ = true;
            return;
        }
        if (r == lighter::Book::Apply::Snapshot) { book_snapshot_ = true; set_condition("book_gap", false, false, Pull::None, "", now); }
        if ((r == lighter::Book::Apply::Snapshot || r == lighter::Book::Apply::Delta) && book_.valid() && book_snapshot_) {
            hl::Book hb = book_.top(20);
            quoter_.on_book(hb);
            offset_.push(hb.t_ms, now);
            last_book_change_ms_ = now;
            ++books_;
            book_dirty_ = true;
        }
    }
    void on_public_closed(int64_t now) {
        public_open_ = false;
        book_snapshot_ = false;
        if (started_) set_condition("public_feed", true, false, Pull::CancelAll, "public order-book stream down", now);
    }
    std::vector<std::string> take_public_out() { auto v = std::move(public_out_); public_out_.clear(); return v; }

    // ── Binance reference stream (bn_pull; only when engine.strategy.bn_pull_* is set) ─────────────────────────
    // Fail-open: with the stream down or stale the pull is simply never on, and the strategy quotes as before.
    bool ref_wanted() const { return bn_.params().on(); }
    std::string ref_ws_path() const {
        std::string s = cfg_.spec.symbol + "usdt";
        for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
        return "/stream?streams=" + s + "@bookTicker";
    }
    void on_ref_frame(const std::string& text, int64_t now) {
        auto j = nlohmann::json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object() || !j.contains("data") || !j["data"].is_object()) return;
        const auto& d = j["data"];
        if (d.value("e", "") != "bookTicker") return;
        auto num = [](const nlohmann::json& v) { return v.is_string() ? std::strtod(v.get<std::string>().c_str(), nullptr) : v.is_number() ? v.get<double>() : 0.0; };
        const double b = num(d.value("b", nlohmann::json())), a = num(d.value("a", nlohmann::json()));
        if (b > 0 && a >= b) { bn_.on_price(now, (a + b) / 2); ++ref_frames_; }
    }
    void on_ref_open(int64_t now) { ++ref_opens_; log("[BN] Binance reference stream open (" + ref_ws_path() + ")"); (void)now; }
    void on_ref_closed(int64_t now) { log("[BN] Binance reference stream closed: no pulls until it is back"); (void)now; }
    bool public_reconnect_wanted() const { return public_reconnect_; }
    void clear_public_reconnect() { public_reconnect_ = false; }

    // ── the clock tick ────────────────────────────────────────────────────────────────────────────────────────
    void tick(int64_t now) {
        // Lighter drops a client that SENDS nothing for 2 min (answering its pings is not enough): ping every 60 s,
        // as the account feed and the recorder do (2026-10-27 live: the public stream closed every 2 min without it)
        if (public_open_ && now - last_public_ping_ms_ >= 60'000) { public_out_.push_back(R"({"type":"ping"})"); last_public_ping_ms_ = now; }
        if (!started_ || stopping_) return;
        try {
            tick_inner(now);
        } catch (const JournalError& e) {
            journal_failure(e.what(), now);
        }
    }

    // ── probe (mode probe): one far post-only bid, ack, cancel, verify, dead-man re-arm + abort ────────────────
    bool probe_done() const { return probe_.done; }
    bool probe_ok() const { return probe_.ok; }
    const nlohmann::json& probe_report() const { return probe_.report; }

    // ── introspection (status, tests) ─────────────────────────────────────────────────────────────────────────
    const OrderTracker& tracker() const { return tracker_; }
    const NonceManager& nonces() const { return nm_; }
    const RateLimiter& limiter() const { return limiter_; }
    bool started() const { return started_; }
    bool quoting_allowed(int64_t now) const { return quoting_block(now).empty(); }
    std::string quoting_block(int64_t now) const {
        if (!started_) return "not started";
        if (stopping_) return "stopping";
        if (cfg_.mode == Mode::Probe) return "mode probe";
        if (!halts_.empty()) return "halt: " + halts_.begin()->first;
        if (!feed_.live()) return "account stream not live";
        if (!public_open_ || !book_snapshot_ || !book_.valid() || !quoter_.has_book()) return "no order book";
        if (now - last_public_ms_ > cfg_.feed_silence_ms) return "public stream silent";
        if (now - last_book_change_ms_ > cfg_.stale_ms) return "book stale";
        if (nm_.frozen()) return "nonce frozen";
        if (limiter_.backing_off(now)) return "rate-limit back-off";
        if (!leverage_ok_) return "leverage not verified";
        if (!equity_known_) return "equity unknown";
        if (!deadman_ok(now)) return "dead-man not armed";
        if (deadman_rearming_) return "dead-man re-arming (abort + schedule in flight)";
        if (d_.risk.halted(now / 1000)) return "risk halt";
        return {};
    }
    bool halted(const std::string& reason) const { return halts_.count(reason) > 0; }
    std::vector<std::string> halt_reasons() const { std::vector<std::string> v; for (auto& [k, _] : halts_) v.push_back(k); return v; }
    int64_t exchange_position() const { return exch_pos_; }
    int64_t position() const { return tracker_.position(mkt()); }
    int64_t deadman_deadline() const { return deadman_deadline_ms_; }
    int deadman_failures() const { return deadman_failures_; }
    int64_t deadman_arms() const { return deadman_arms_; }
    bool deadman_aborted() const { return deadman_aborted_; }
    int64_t rate_limited_events() const { return rate_limited_events_; }
    int64_t fills() const { return fills_; }
    int64_t mismatch_acts() const { return mismatch_acts_; }
    int64_t unknown_cancels() const { return foreign_cancels_; }
    int64_t reconciles() const { return reconciles_; }
    int64_t last_trade_id() const { return last_trade_id_; }
    int64_t sends() const { return sender_.requests(); }
    int64_t sequencer_rejects() const { return seq_rejects_; }
    int64_t cancel_races() const { return cancel_races_; }
    int64_t fast_reconcile_fails() const { return fast_rec_fails_; }
    const ClockOffset& clock_offset() const { return offset_; }
    nlohmann::json status(int64_t now) const { return status_json(now); }
    void request_reconcile(int64_t now) { next_reconcile_ms_ = std::min(next_reconcile_ms_, now); }
    // The engine thread was blocked for `ms` (a slow REST call, a WebSocket open): the book it quoted on is old.
    // Quotes are pulled and quoting waits 10 s of normal running.
    void on_stall(int64_t ms, int64_t now) {
        if (!started_ || stopping_) return;
        stall_until_ = now + 10'000;
        set_condition("stall", true, false, Pull::Quotes, "engine thread blocked " + std::to_string(ms) + " ms: quotes pulled", now);
    }

    // AccountFeed::Sink — public so the feed can call it; the engine is the only sink.
    void on_orders(const std::vector<AcctOrder>& orders, bool snapshot, int64_t now) override;
    void on_fills(const std::vector<AcctFill>& fills, bool snapshot, int64_t now) override;
    void on_positions(const std::vector<AcctPosition>& pos, bool snapshot, int64_t now) override;
    void on_stats(const AcctStats& s, int64_t now) override;
    void on_txs(const std::vector<AcctTx>& txs, int64_t now) override;
    void on_feed_note(const std::string& what, int64_t now) override { log(what); jrec_soft({{"ev", "account_note"}, {"what", what}}); (void)now; }

private:
    enum class Origin { Quote, Direct, DeadMan, Flatten, Probe };
    enum class Pull { None, Quotes, CancelAll };
    enum class Fate { Pending, Applied, NotApplied };
    struct SentTx {
        Intent intent;
        Origin origin = Origin::Quote;
        std::string hash;
        int64_t sent_ms = 0;
        Fate fate = Fate::Pending;
        int result_code = 0;
    };
    struct Halt { bool sticky = false; int64_t since = 0; std::string detail; };

    int64_t mkt() const { return cfg_.spec.market_index; }
    void log(const std::string& s) const { if (d_.log) d_.log(s); }
    void alert(const std::string& key, const std::string& text, int64_t now) {
        log("[ALERT] " + text);
        d_.alerts.alert(key, cfg_.label + ": " + text, now);
    }
    static std::string usd(int64_t micro) { char b[32]; std::snprintf(b, sizeof b, "%.2f", double(micro) / kMicro); return b; }
    std::string fmt_base(int64_t b) const { return format_decimal(b, cfg_.spec.size_decimals); }

    bool refuse(const std::string& why) {
        log("[REFUSE] " + why);
        alert("refuse", "refusing to trade: " + why, d_.clock.now_ms());
        try { jrec({{"ev", "refuse"}, {"why", why}}, true); } catch (...) {}
        refused_ = why;
        return false;
    }

    // The Binance-lead pull → the quoter's overlay, read by quote_allowed (the same flags requote() honours in paper).
    void apply_bn_pull(int64_t now) {
        if (!bn_.params().on()) return;
        const bnpull::Lead::Out prev = bn_.last();
        const bnpull::Lead::Out o = bn_.at(now);
        auto ov = quoter_.overlay();
        ov.pull_bid = o.bid; ov.pull_ask = o.ask;
        quoter_.set_overlay(ov);
        if ((o.bid && !prev.bid) || (o.ask && !prev.ask))
            jrec_soft({{"ev", "bn_pull"}, {"bid", o.bid}, {"ask", o.ask}, {"lead_bps", std::isfinite(o.lead_bps) ? nlohmann::json(o.lead_bps) : nlohmann::json(nullptr)}});
        const bool stale = bn_.last_sample_ms() < 0 || now - bn_.last_sample_ms() > bn_.params().stale_ms;
        if (stale != ref_stale_ && started_) {
            ref_stale_ = stale;
            log(stale ? "[BN] Binance reference stale: pulls off (quoting as without it)" : "[BN] Binance reference fresh: pulls on");
        }
    }

    // The day's fills from EARLIER runs (today's journal), so the 00:00 UTC "day" alert and status fills_today count the
    // whole UTC day, not just this run (2026-10-28: the midnight summary said "0 fills" after a restart). Best effort.
    void seed_day_from_journal(const std::string& day) {
        const std::string f = cfg_.journal_dir + "/" + cfg_.label + "-" + day + ".jsonl";
        std::ifstream in(f);
        std::string line;
        int64_t n = 0, vol = 0;
        while (std::getline(in, line)) {
            if (line.find("\"ev\":\"fill\"") == std::string::npos) continue;
            auto j = nlohmann::json::parse(line, nullptr, false);
            if (j.is_discarded() || j.value("run", int64_t{-1}) == run_id_) continue;
            ++n;
            vol += cfg_.spec.notional_micro_floor(j.value("qty", int64_t{0}), j.value("price", int64_t{0}));
        }
        if (n == 0) return;
        fills_day_ += n;
        volume_day_micro_ += vol;
        first_fill_alerted_ = true;
        log("[DAY] " + std::to_string(n) + " fill(s) earlier today in previous runs counted in the day summary");
    }

    hl::Params quoter_params() const {
        hl::Params p = mmpaper::frozen_params();
        p.order_usd = double(cfg_.risk.quote_micro) / kMicro;
        p.max_inv_units = cfg_.risk.max_position_quotes;
        p.venue_tick = std::pow(10.0, -cfg_.spec.price_decimals);
        p.strat_tick_rule = hl::TickRule::Hyperliquid;
        p.hl_sz_decimals = cfg_.hl_sz_decimals;
        p.fills = hl::FillModel::NoCross;
        p.track_markouts = false;
        cfg_.strategy.apply(p);
        return p;
    }

    // ── journal ───────────────────────────────────────────────────────────────────────────────────────────────
    void jrec(nlohmann::json r, bool durable) {
        r["run"] = run_id_;
        d_.journal.write(std::move(r), d_.clock.now_ms(), durable);
    }
    // Non-critical notes: a failure still latches the journal (the next critical write halts), never throws here.
    void jrec_soft(nlohmann::json r) {
        try { jrec(std::move(r), false); } catch (const JournalError& e) { journal_failure(e.what(), d_.clock.now_ms()); }
    }
    void journal_failure(const std::string& why, int64_t now) {
        if (journal_failed_) return;
        journal_failed_ = true;
        halts_["journal"] = {true, now, why};
        log("[HALT] journal failure: " + why);
        alert("journal", "JOURNAL FAILURE (" + why + "): halted, cancelling everything", now);
        batcher_.drop_quoting();
        send_cancel_all_now("journal failure", /*journal*/ false);
    }

    // ── halts ─────────────────────────────────────────────────────────────────────────────────────────────────
    void set_condition(const std::string& name, bool active, bool sticky, Pull pull, const std::string& detail, int64_t now) {
        auto it = halts_.find(name);
        if (active) {
            if (it != halts_.end()) return;
            halts_[name] = {sticky, now, detail};
            log("[HALT] " + name + ": " + detail);
            jrec_soft({{"ev", "halt"}, {"reason", name}, {"sticky", sticky}, {"detail", detail}});
            try { d_.journal.sync(now); } catch (const JournalError& e) { journal_failure(e.what(), now); }
            alert("halt:" + name, "HALT " + name + (sticky ? " (sticky)" : "") + ": " + detail, now);
            batcher_.drop_quoting();
            if (pull == Pull::CancelAll) send_cancel_all_now(name);
            else if (pull == Pull::Quotes) pull_quotes(now);
        } else {
            if (it == halts_.end() || it->second.sticky) return;
            halts_.erase(it);
            log("[RESUME] " + name);
            jrec_soft({{"ev", "resume"}, {"reason", name}});
            alert("resume:" + name, "resumed after " + name, now);
        }
    }
    void sticky_halt(const std::string& name, const std::string& detail, int64_t now, bool cancel_all = true) {
        set_condition(name, true, true, cancel_all ? Pull::CancelAll : Pull::None, detail, now);
    }

    // ── sending ───────────────────────────────────────────────────────────────────────────────────────────────
    // Signs, journals, applies tracker transitions, sends one request, and settles what the answer says.
    // Returns true when the API accepted the request (Accepted-pending).
    bool dispatch(std::vector<Intent> intents, int64_t now, Origin origin, bool recorded, bool journal_it = true) {
        if (intents.empty()) return false;
        if (nm_.frozen() || !nm_.synced()) { requeue_safety(intents); return false; }
        bool safety = std::all_of(intents.begin(), intents.end(), [](const Intent& i) {
            return i.kind == IntentKind::Cancel || i.kind == IntentKind::CancelAll || (i.reduce_only && i.tif == TimeInForce::IOC); });
        // two budgets: all requests (REST + sendTx share Standard's 60/min) and sendTx alone (default 30/min, under the
        // unconfirmed 40/min per-tx-type limit)
        // (`recorded`: the batcher already charged send_limiter_ for this request, flush_batcher checked limiter_)
        if (!recorded) {
            if (!send_limiter_.can_send(now, safety) || !limiter_.can_send(now, safety)) { requeue_safety(intents); return false; }
            send_limiter_.record(now);
        }
        limiter_.record(now);
        persist_rate(now);
        // The arming file is re-checked before EVERY request: disarmed → no new exposure is signed. Cancels and the
        // KILL / loss-stop flatten (reduce-only IOC — it can only shrink the position) always go.
        if (d_.armed && !d_.armed(now / 1000)) {
            size_t before = intents.size();
            std::erase_if(intents, [](const Intent& i) {
                bool flatten = i.kind == IntentKind::Create && i.reduce_only && i.tif == TimeInForce::IOC;
                return (i.kind == IntentKind::Create || i.kind == IntentKind::Modify) && !flatten; });
            if (before != intents.size()) {
                log("[TX] not armed: " + std::to_string(before - intents.size()) + " create(s) dropped");
                set_condition("not_armed", true, false, Pull::None, "arming file missing/expired or NO_DEPLOY_MM present", now);
            }
            if (intents.empty()) return false;
        } else if (halts_.count("not_armed")) {
            set_condition("not_armed", false, false, Pull::None, "", now);
        }
        std::vector<std::string> errs;
        SignOptions so;
        so.errors = &errs;
        so.now_ms = now;
        if (origin == Origin::Quote) so.requeue = &batcher_;
        std::vector<SignedTx> txs;
        try {
            txs = sign_batch(d_.signer, nm_, intents, ApiCreds{cfg_.api_key_index, cfg_.account_index}, so);
        } catch (const std::exception& e) {
            log(std::string("[TX] signing failed: ") + e.what());
            return false;
        }
        for (const auto& e : errs) log("[TX] " + e);
        // sign_batch signs in order and returns the signed prefix: tx k belongs to intent k. Intent txs.size() failed
        // to sign; a cancel there is never dropped (retried), and for direct requests every later cancel too (a quote
        // batch's tail already went back to the batcher).
        if (txs.size() < intents.size()) {
            auto from = intents.begin() + std::ptrdiff_t(txs.size());
            requeue_safety(origin == Origin::Quote ? std::vector<Intent>{*from} : std::vector<Intent>(from, intents.end()));
        }
        if (txs.empty()) return false;
        std::vector<Intent> used(intents.begin(), intents.begin() + std::ptrdiff_t(txs.size()));
        if (journal_it) {
            try {
                for (size_t k = 0; k < txs.size(); ++k) jrec(tx_record(used[k], txs[k], origin), false);
                d_.journal.sync(now);
            } catch (const JournalError& e) {
                nm_.release_unsent(txs.front().nonce);   // never sent: nothing to account for
                journal_failure(e.what(), now);
                return false;
            }
        }
        // tracker transitions BEFORE the send: the order exists (as PendingNew / CancelPending) from now on
        for (size_t k = 0; k < txs.size(); ++k) {
            const Intent& i = used[k];
            SentTx s{i, origin, txs[k].tx_hash, now, Fate::Pending, 0};
            sent_[txs[k].nonce] = s;
            nm_.mark_sent(txs[k].nonce);
            apply_before_send(i, now);
        }
        SendTxResult res = sender_.send(txs);
        if (journal_it) jrec_soft({{"ev", "send"}, {"first_nonce", txs.front().nonce}, {"n", txs.size()},
                                   {"result", send_kind_name(res.kind)}, {"http", res.http}, {"code", res.code}, {"msg", res.message}});
        using K = SendTxResult::Kind;
        switch (res.kind) {
            case K::Accepted:
                for (auto& t : txs) nm_.on_accepted(t.nonce);
                return true;
            case K::Unknown:
                log("[TX] outcome UNKNOWN (" + res.message + "): freezing nonces, reconciling");
                for (auto& t : txs) { try { nm_.on_outcome(t.nonce, TxOutcome::Unknown); } catch (const NonceError&) {} }
                alert("tx_unknown", "send outcome unknown (" + res.message + "): nonces frozen until nextNonce + reconcile", now);
                request_reconcile(now);
                return false;
            case K::InvalidNonce:
            case K::RateLimited:
            case K::ApiRejected:
            case K::StandardLimit:
            case K::NotSent: {
                try { nm_.on_outcome(txs.front().nonce, TxOutcome::ApiRejected); } catch (const NonceError& e) { log(e.what()); }
                for (size_t k = 0; k < txs.size(); ++k) undo_not_sent(txs[k].nonce, now);
                if (res.kind == K::InvalidNonce) {
                    nm_.freeze();
                    alert("nonce_freeze", "invalid nonce from the API: nonce stream frozen, resyncing", now);
                } else if (res.kind == K::RateLimited) {
                    on_rate_limited(now, "sendTx");
                } else if (res.kind == K::StandardLimit) {
                    sticky_halt("standard_limit", "Lighter 52002: Standard trading limit reached, only reduce allowed", now);
                } else if (res.kind == K::ApiRejected) {
                    log("[TX] API rejected (" + std::to_string(res.code) + " " + res.message + ")");
                }
                return false;
            }
        }
        return false;
    }

    nlohmann::json tx_record(const Intent& i, const SignedTx& t, Origin o) const {
        static const char* kinds[] = {"cancel_all", "cancel", "modify", "create"};
        nlohmann::json r = {{"ev", "tx"}, {"nonce", t.nonce}, {"hash", t.tx_hash}, {"tx_type", int(t.tx_type)},
                            {"kind", kinds[int(i.kind)]}, {"origin", int(o)}, {"market", i.market}, {"ref", i.order_ref}};
        if (i.kind == IntentKind::Create) {
            r["side"] = i.is_ask ? "ask" : "bid"; r["base"] = i.base; r["price"] = i.price;
            r["tif"] = int(i.tif); r["reduce_only"] = i.reduce_only; r["expiry"] = i.expiry_ms;
        }
        if (i.kind == IntentKind::CancelAll) { r["ca_tif"] = int(i.ca_tif); r["ca_time"] = i.ca_time_ms; }
        return r;
    }

    void apply_before_send(const Intent& i, int64_t now) {
        try {
            switch (i.kind) {
                case IntentKind::Create:
                    tracker_.add_pending(i.order_ref, i.market, i.is_ask, i.base, i.price, now);
                    tracker_.apply_held(i.order_ref, now);
                    break;
                case IntentKind::Cancel:
                    if (tracker_.has(i.order_ref)) tracker_.request_cancel(i.order_ref, now);
                    break;
                case IntentKind::CancelAll:
                    if (i.ca_tif == CancelAllTif::Immediate)
                        for (const auto& [coi, o] : tracker_.all())
                            if (is_fillable(o.state) && (i.market == kNilMarketIndex || o.market == i.market) && o.state != OrderState::CancelPending)
                                tracker_.request_cancel(coi, now);
                    break;
                case IntentKind::Modify: break;
            }
        } catch (const OrderStateError& e) { log(std::string("[ORD] ") + e.what()); }
    }

    // A request that never reached the sequencer: creates are rejected (the slot frees), cancels and cancel-alls are
    // re-queued (their orders stay CancelPending — still exposure — until the exchange confirms).
    void undo_not_sent(int64_t nonce, int64_t now) {
        auto it = sent_.find(nonce);
        if (it == sent_.end()) return;
        SentTx s = it->second;
        sent_.erase(it);
        if (s.origin == Origin::DeadMan) { deadman_failed(now, "not accepted by the API", 0); return; }
        switch (s.intent.kind) {
            case IntentKind::Create:
                try { if (tracker_.has(s.intent.order_ref) && tracker_.get(s.intent.order_ref).state == OrderState::PendingNew)
                          tracker_.on_rejected(s.intent.order_ref, RejectReason::ApiReject, now); }
                catch (const OrderStateError& e) { log(e.what()); }
                break;
            case IntentKind::Cancel:
            case IntentKind::CancelAll:
                pending_safety_.push_back(s.intent);   // retried on the next tick (rate budget permitting)
                break;
            case IntentKind::Modify: break;
        }
    }
    void requeue_safety(const std::vector<Intent>& intents) {
        for (const auto& i : intents) if (i.kind == IntentKind::Cancel || i.kind == IntentKind::CancelAll) pending_safety_.push_back(i);
    }

    bool send_cancel_all_now(const std::string& why, bool journal_it = true) {
        int64_t now = d_.clock.now_ms();
        Intent ca; ca.kind = IntentKind::CancelAll; ca.market = kNilMarketIndex; ca.ca_tif = CancelAllTif::Immediate;
        log("[TX] cancel-all (" + why + ")");
        bool ok = dispatch({ca}, now, Origin::Direct, false, journal_it && !journal_failed_);
        if (!ok && !nm_.frozen()) pending_safety_.push_back(ca);
        return ok;
    }
    void cancel_market_now(const std::string& why, int64_t now) {
        Intent ca; ca.kind = IntentKind::CancelAll; ca.market = mkt(); ca.ca_tif = CancelAllTif::Immediate;
        log("[TX] cancel-all market " + std::to_string(mkt()) + " (" + why + ")");
        if (!dispatch({ca}, now, Origin::Direct, false)) pending_safety_.push_back(ca);
    }

    bool wait_no_open_orders(int64_t budget_ms) {
        int64_t until = d_.clock.now_ms() + budget_ms;
        // a cancel-all lands after Lighter's 300 ms speed bump: "no open orders" read before that proves nothing (and
        // an order placed in that window would be cancelled by it), so the first look is 1 s after the send
        d_.clock.sleep_ms(1'000);
        while (true) {
            auto open = rest_.active_orders(mkt());
            if (open && open->empty()) return true;
            if (d_.clock.now_ms() >= until) return false;
            d_.clock.sleep_ms(1'000);
            flush_safety(d_.clock.now_ms());
        }
    }

    void flush_safety(int64_t now) {
        if (pending_safety_.empty() || nm_.frozen()) return;
        std::vector<Intent> v = std::move(pending_safety_);
        pending_safety_.clear();
        // de-duplicate (cancel by order ref, cancel-all by tif+market)
        std::vector<Intent> u;
        for (auto& i : v) {
            // a cancel of an order that is already filled / cancelled (or forgotten) can only be rejected — on mainnet
            // BEFORE applying (21727), which fails every later tx of the request: re-sending it loops forever and
            // blocks the flatten behind it (2026-10-27 live incident)
            if (i.kind == IntentKind::Cancel &&
                (!tracker_.has(i.order_ref) || is_terminal(tracker_.get(i.order_ref).state))) continue;
            bool dup = std::any_of(u.begin(), u.end(), [&](const Intent& x) {
                return x.kind == i.kind && x.order_ref == i.order_ref && x.market == i.market && x.ca_tif == i.ca_tif; });
            if (!dup) u.push_back(i);
        }
        if (u.size() > size_t(cfg_.max_txs_per_batch)) {
            pending_safety_.assign(u.begin() + cfg_.max_txs_per_batch, u.end());
            u.resize(size_t(cfg_.max_txs_per_batch));
        }
        std::stable_sort(u.begin(), u.end(), [](const Intent& a, const Intent& b) { return int(a.kind) < int(b.kind); });
        if (!dispatch(u, now, Origin::Direct, false)) { /* requeued inside */ }
    }

    // The rate windows survive a crash + restart (state_dir/rate_window.json, tmp + rename after every request), so a
    // restart inside the same minute cannot spend the 60/min twice. A missing/unreadable file starts empty.
    std::string rate_path() const { return cfg_.state_dir.empty() ? "" : cfg_.state_dir + "/rate_window.json"; }
    void persist_rate(int64_t now) {
        if (rate_path().empty()) return;
        nlohmann::json j = {{"all", limiter_.window(now)}, {"send", send_limiter_.window(now)},
                            {"blocked_until", std::max(limiter_.blocked_until(), send_limiter_.blocked_until())}};
        std::string tmp = rate_path() + ".tmp";
        { std::ofstream f(tmp, std::ios::trunc); f << j.dump(); if (!f) return; }
        std::error_code ec;
        std::filesystem::rename(tmp, rate_path(), ec);
    }
    void load_rate(int64_t now) {
        if (rate_path().empty()) return;
        std::ifstream f(rate_path());
        auto j = nlohmann::json::parse(f, nullptr, false);
        if (!j.is_object()) return;
        try {
            int64_t bu = j.value("blocked_until", int64_t{0});
            limiter_.preload(j.value("all", std::vector<int64_t>{}), bu, now);
            send_limiter_.preload(j.value("send", std::vector<int64_t>{}), bu, now);
        } catch (const nlohmann::json::exception&) {}
    }

    void on_rate_limited(int64_t now, const std::string& where) {
        limiter_.on_rate_limited(now);
        send_limiter_.on_rate_limited(now);
        persist_rate(now);
        ++rate_limited_events_;
        rl_times_.push_back(now);
        while (!rl_times_.empty() && rl_times_.front() < now - 600'000) rl_times_.pop_front();
        log("[TX] HTTP 429/405 on " + where + ": backing off 60 s");
        jrec_soft({{"ev", "rate_limited"}, {"where", where}});
        alert("429", "rate limited (429/405) on " + where + ": no requests for 60 s, dead-man still armed", now);
        if (rl_times_.size() > 3) sticky_halt("rate_limit_burst", "more than 3 × 429 in 10 min", now, false);
        request_reconcile(now + 60'000);
    }

    // ── sequencer results (account_tx) ────────────────────────────────────────────────────────────────────────
    void settle(int64_t nonce, TxOutcome o, int code, int64_t now) {
        std::vector<int64_t> resign;
        try { resign = nm_.on_outcome(nonce, o); }
        catch (const NonceError& e) { log(std::string("[TX] ") + e.what()); return; }
        auto it = sent_.find(nonce);
        if (it == sent_.end()) return;
        SentTx s = it->second;
        if (consumes_nonce(o)) {
            s.fate = Fate::Applied;
            sent_.erase(it);
            fates_[nonce] = Fate::Applied;
            // accepted txs below it were settled by the nonce manager's FIFO rule: drop their bookkeeping too
            for (auto j = sent_.begin(); j != sent_.end() && j->first < nonce;)
                if (nm_.tracking(j->first)) ++j;
                else { abort_left_unconfirmed(j->second, now); j = sent_.erase(j); }
        }
        jrec_soft({{"ev", "seq"}, {"nonce", nonce}, {"outcome", int(o)}, {"code", code}});
        if (s.origin == Origin::DeadMan && o != TxOutcome::SequencerRejectedBefore && o != TxOutcome::Unknown)
            on_deadman_tx(s.intent, nonce, consumes_nonce(o) && code == 0, code, now);
        if (o == TxOutcome::Unknown && code > 0) {
            // a rejection whose nonce consumption is unconfirmed (21712/21713/21714): the result is KNOWN, so the
            // resync must never resend these bytes — nextNonce alone decides consumed vs free
            known_result_.insert(nonce);
            if (s.origin == Origin::DeadMan) on_deadman_tx(s.intent, nonce, false, code, now);
            request_reconcile(now);
        }
        if (o == TxOutcome::SequencerFailedApplying && s.intent.kind == IntentKind::Create && tracker_.has(s.intent.order_ref)) {
            try {
                const Order& ord = tracker_.get(s.intent.order_ref);
                if (ord.state == OrderState::PendingNew || ord.state == OrderState::Unknown) {
                    RejectReason rr = code == 21728 ? RejectReason::DuplicateClientIndex : code == 21706 ? RejectReason::MinSize : RejectReason::Other;
                    if (ord.state == OrderState::Unknown) tracker_.reconcile(s.intent.order_ref, kStatusCanceled, ord.filled, 0, now);
                    else tracker_.on_rejected(s.intent.order_ref, rr, now);
                }
            } catch (const OrderStateError& e) { log(e.what()); }
        }
        if (o == TxOutcome::SequencerFailedApplying && s.intent.kind == IntentKind::Cancel) request_reconcile(now);   // filled or gone
        if (o == TxOutcome::SequencerRejectedBefore) {
            ++seq_rejects_;
            // A cancel that lost the race to a fill: the fill is already known (order terminal) when the sequencer
            // rejects the cancel. On 2026-10-28 every one of 31 rejects was this — one per fill. The nonce resync
            // below still runs (later txs of the request failed too) and dispatch stays blocked until it completes;
            // only the halt + Telegram alert are skipped. Any other reject keeps the loud path.
            bool race = s.intent.kind == IntentKind::Cancel && tracker_.has(s.intent.order_ref) &&
                        is_terminal(tracker_.get(s.intent.order_ref).state);
            if (race) {
                ++cancel_races_;
                if (!halts_.count("nonce_frozen")) { quiet_freeze_ = true; quiet_since_ms_ = now; batcher_.drop_quoting(); }   // as the halt would; never quieten a loud freeze
                jrec_soft({{"ev", "cancel_race"}, {"nonce", nonce}, {"coi", s.intent.order_ref}, {"code", code}});
            } else {
                quiet_freeze_ = false;
                alert("seq_reject", "sequencer rejected nonce " + std::to_string(nonce) + " before applying (code " +
                                    std::to_string(code) + "): it and every later tx failed; resyncing", now);
            }
            resign.insert(resign.begin(), nonce);
            for (int64_t n : resign) {
                auto jt = sent_.find(n);
                if (jt == sent_.end()) continue;
                SentTx x = jt->second;
                sent_.erase(jt);
                fates_[n] = Fate::NotApplied;
                if (x.origin == Origin::DeadMan) {   // "every later tx fails too" is spec text, not proof: an abort is read as applied
                    if (x.intent.ca_tif == CancelAllTif::Abort) abort_left_unconfirmed(x, now);
                    else on_deadman_tx(x.intent, n, false, code, now);
                    continue;
                }
                if (x.intent.kind == IntentKind::Create && tracker_.has(x.intent.order_ref)) {
                    not_applied_cois_.insert(x.intent.order_ref);
                    try { tracker_.mark_unknown(x.intent.order_ref, now); } catch (const OrderStateError& e) { log(e.what()); }
                } else if (x.intent.kind == IntentKind::Cancel || x.intent.kind == IntentKind::CancelAll) {
                    pending_safety_.push_back(x.intent);
                }
            }
            request_reconcile(now);
        }
    }

    // ── dead-man ──────────────────────────────────────────────────────────────────────────────────────────────
    // Server-time estimate for the dead-man deadline (R-DMS-2): a local clock running AHEAD of the exchange shows as a
    // negative offset and pulls the deadline in (never out); the pull is capped at 10 s so a burst of late messages
    // cannot push the deadline under Lighter's 5-minute minimum (window 315 s − 10 s ≥ 305 s).
    int64_t server_now(int64_t now) const { return now + std::clamp<int64_t>(offset_.offset(), -10'000, 0); }
    // Re-arm. What a SCHEDULED cancel-all does while one is already queued is UNCONFIRMED (lighter_tx_spec.md §2.7):
    // either it replaces the queued one, or it fails with 21712 ("account has a queued cancel all orders request").
    // Mode Replace (the default) sends the new schedule ALONE: if it replaces, the deadline moves; if it fails, the
    // OLD schedule stays armed — a failed re-arm never leaves the account without a dead-man. Only after the exchange
    // answers 21712 does the engine switch to AbortFirst ([abort, schedule] in one request, consecutive nonces): the
    // only way left to extend, at the cost that a failed schedule there leaves nothing armed — the confirmed deadline
    // is then cleared by the abort's result and quoting stops at once (deadman_ok). The probe reports which mode the
    // exchange needed.
    void arm_deadman(int64_t now) {
        Intent ab; ab.kind = IntentKind::CancelAll; ab.market = kNilMarketIndex; ab.ca_tif = CancelAllTif::Abort;
        Intent sc; sc.kind = IntentKind::CancelAll; sc.market = kNilMarketIndex; sc.ca_tif = CancelAllTif::Scheduled;
        sc.ca_time_ms = server_now(now) + cfg_.deadman_window_ms;
        last_arm_try_ms_ = now;
        ++deadman_arms_;
        std::vector<Intent> v = deadman_abort_first_ ? std::vector<Intent>{ab, sc} : std::vector<Intent>{sc};
        if (dispatch(v, now, Origin::DeadMan, false)) {
            deadman_last_sched_nonce_ = nm_.peek_next() - 1;
            if (deadman_abort_first_) deadman_rearming_ = true;   // no quoting until the new schedule is confirmed
        } else {
            deadman_failed(now, "re-arm not sent", 0);
        }
    }
    // The ONLY sources of dead-man health: a Scheduled tx's own sequencer result (code 0 → its deadline is confirmed)
    // and an Abort's result (executed → nothing is armed any more). A resync that merely shows a nonce consumed proves
    // nothing (it can carry 21714) and is never used.
    // A dead-man tx left the books WITHOUT its own result (FIFO-settled below a later executed nonce, applied per a
    // nextNonce resync, or erased with a sequencer rejection): an ABORT is taken as executed — nothing is armed until a
    // schedule is confirmed — and a SCHEDULE as not confirmed.
    void abort_left_unconfirmed(const SentTx& s, int64_t now) {
        if (s.origin != Origin::DeadMan) return;
        if (s.intent.ca_tif == CancelAllTif::Abort) {
            deadman_deadline_ms_ = 0;
            jrec_soft({{"ev", "deadman_aborted"}, {"source", "implied"}});
        } else if (s.intent.ca_tif == CancelAllTif::Scheduled) {
            deadman_rearming_ = false;
        }
        (void)now;
    }
    void on_deadman_tx(const Intent& i, int64_t nonce, bool ok, int code, int64_t now) {
        if (i.ca_tif == CancelAllTif::Scheduled) {
            if (nonce == deadman_last_sched_nonce_) deadman_rearming_ = false;
            if (ok) {
                deadman_deadline_ms_ = i.ca_time_ms;
                deadman_confirmed_nonce_ = nonce;
                deadman_failures_ = 0;
                deadman_episode_alerted_ = false;
                jrec_soft({{"ev", "deadman_armed"}, {"deadline", i.ca_time_ms}, {"nonce", nonce}});
                return;
            }
            if (code == 21712 && !deadman_abort_first_) {
                deadman_abort_first_ = true;
                jrec_soft({{"ev", "deadman_mode"}, {"mode", "abort_first"}});
                log("[DMS] exchange refuses a schedule while one is queued (21712): re-arming with abort + schedule");
            }
            deadman_failed(now, "schedule rejected (code " + std::to_string(code) + ")", code);
        } else if (i.ca_tif == CancelAllTif::Abort) {
            if (ok) { deadman_deadline_ms_ = 0; jrec_soft({{"ev", "deadman_aborted"}, {"nonce", nonce}}); }
            else deadman_failed(now, "abort rejected (code " + std::to_string(code) + ")", code);
        }
    }
    void deadman_failed(int64_t now, const std::string& why, int code) {
        ++deadman_failures_;
        jrec_soft({{"ev", "deadman_fail"}, {"why", why}, {"code", code}, {"consecutive", deadman_failures_}});
        if (!deadman_episode_alerted_) {   // one alert per episode, not one per 20 s
            deadman_episode_alerted_ = true;
            alert("deadman", "dead-man re-arm failed (" + why + "); the last confirmed deadline is " +
                             (deadman_deadline_ms_ ? std::to_string((deadman_deadline_ms_ - server_now(now)) / 1000) + " s away" : "GONE"), now);
        }
    }
    // Quoting needs a CONFIRMED scheduled cancel-all at least 60 s in the future.
    bool deadman_ok(int64_t now) const { return deadman_deadline_ms_ > server_now(now) + 60'000; }

    // ── flatten (KILL / loss stop) ────────────────────────────────────────────────────────────────────────────
    void risk_stop(int64_t now) {
        sticky_halt("risk_stop", "loss stop: " + d_.risk.state().reason, now);
        flatten_requested_ = true;
    }
    void kill(int64_t now) {
        if (!halts_.count("kill")) sticky_halt("kill", "KILL file present: cancel all, flatten, halted until a human resumes", now);
        flatten_requested_ = true;
    }
    // Position stop: one stuck position must not ride a trend down until the account-level stops fire. Uses the
    // exchange's avg entry only while the position still equals the one that entry was reported for (a fill since the
    // last reconcile makes it stale until the next one); the hold clock starts when the position leaves 0 (or at start).
    void position_stop_step(int64_t now) {
        const RiskConfig& rc = d_.risk.config();
        if (rc.position_stop_micro == 0 && rc.position_max_hold_s == 0) return;
        int64_t pos = tracker_.position(mkt());
        if (pos == 0) pos_since_ms_ = 0;
        else if (pos_since_ms_ == 0) pos_since_ms_ = now;
        if (halts_.count("position_stop")) {
            if (pos == 0 && now >= pos_stop_until_ms_) set_condition("position_stop", false, false, Pull::None, "", now);
            return;
        }
        if (pos == 0 || flatten_requested_ || !book_.valid()) return;
        std::string why;
        if (rc.position_stop_micro < 0 && entry_px_ > 0 && entry_pos_ == pos) {
            int64_t scale = pow10i(cfg_.spec.price_decimals);
            hl::Book hb = book_.top(1);
            int64_t exit_px = std::llround((pos > 0 ? hb.bids[0].px : hb.asks[0].px) * double(scale));
            int64_t unreal = int64_t(__int128(pos) * (exit_px - entry_px_) * kMicro / pow10i(cfg_.spec.size_decimals + cfg_.spec.price_decimals));
            last_unreal_micro_ = unreal;
            if (unreal <= rc.position_stop_micro)
                why = "position " + fmt_base(pos) + " unrealised $" + usd(unreal) + " ≤ stop $" + usd(rc.position_stop_micro);
        }
        if (why.empty() && rc.position_max_hold_s > 0 && now - pos_since_ms_ >= rc.position_max_hold_s * 1000)
            why = "position " + fmt_base(pos) + " held " + std::to_string((now - pos_since_ms_) / 60'000) + " min ≥ " +
                  std::to_string(rc.position_max_hold_s / 60) + " min";
        if (why.empty()) return;
        pos_stop_until_ms_ = now + rc.position_cooldown_s * 1000;
        ++position_stops_;
        set_condition("position_stop", true, false, Pull::CancelAll,
                      why + ": flatten, quotes off " + std::to_string(rc.position_cooldown_s / 60) + " min", now);
        flatten_requested_ = true;
        flatten_tries_ = 0;
        flatten_gave_up_ = false;
    }
    void flatten_step(int64_t now) {
        if (!flatten_requested_) return;
        int64_t pos = tracker_.position(mkt());
        if (pos == 0) {
            if (flatten_tries_ > 0) { jrec_soft({{"ev", "flattened"}}); alert("flatten", "flat after " + std::to_string(flatten_tries_) + " IOC(s)", now); }
            flatten_requested_ = false;
            flatten_tries_ = 0;
            return;
        }
        if (now - last_flatten_ms_ < 2'000 || !book_.valid()) return;
        bool sell = pos > 0;
        if (!tracker_.slot_free(mkt(), sell)) return;   // a quote on that side is still cancel-pending
        if (flatten_tries_ >= cfg_.flatten_max_tries) {
            if (!flatten_gave_up_) { flatten_gave_up_ = true; alert("flatten_fail", "FLATTEN FAILED after " + std::to_string(flatten_tries_) + " tries, position " + fmt_base(pos) + ": act by hand", now); }
            return;
        }
        int64_t scale = pow10i(cfg_.spec.price_decimals);
        hl::Book hb = book_.top(1);
        int64_t bb = std::llround(hb.bids[0].px * double(scale)), ba = std::llround(hb.asks[0].px * double(scale));
        int64_t px = sell ? int64_t(__int128(bb) * (10'000 - cfg_.flatten_cap_bp) / 10'000)
                          : int64_t((__int128(ba) * (10'000 + cfg_.flatten_cap_bp) + 9'999) / 10'000);
        int64_t qty = std::llabs(pos);
        RiskGovernor::OrderCheck c;
        c.market = mkt(); c.is_ask = sell; c.qty = qty; c.reduce_only = true;
        c.exposure = tracker_.exposure(mkt()); c.now_unix_s = now / 1000;
        std::string why = d_.risk.check_order(c);
        if (!why.empty()) { log("[RISK] flatten refused: " + why); last_flatten_ms_ = now; return; }
        Intent f; f.kind = IntentKind::Create; f.market = mkt(); f.is_ask = sell; f.slot = slot_key(mkt(), sell);
        f.order_ref = alloc_.next(); f.base = qty; f.price = px; f.tif = TimeInForce::IOC; f.reduce_only = true; f.expiry_ms = 0;
        ++flatten_tries_;
        last_flatten_ms_ = now;
        jrec_soft({{"ev", "flatten_try"}, {"try", flatten_tries_}, {"qty", qty}, {"price", px}});
        dispatch({f}, now, Origin::Flatten, false);
    }

    // ── quoting ───────────────────────────────────────────────────────────────────────────────────────────────
    int64_t gross_micro() const {
        if (!book_.valid()) return 0;
        hl::Book hb = book_.top(1);
        int64_t mid = std::llround(hb.mid() * double(pow10i(cfg_.spec.price_decimals)));
        int64_t pos = std::llabs(tracker_.position(mkt()));
        __int128 units = tracker_.fillable_notional_units(mkt());
        int64_t orders = int64_t(units * kMicro / pow10i(cfg_.spec.size_decimals + cfg_.spec.price_decimals));
        return cfg_.spec.notional_micro_ceil(pos, mid) + orders;
    }
    // The gross cap's measure (review 2): WORST-CASE net exposure if one more order of `qty` on `is_ask` is added —
    // max(pos + Σ fillable bids, −pos + Σ fillable asks) in base units, valued at the higher of the order price and
    // the mid. Bids and asks cannot both fill against one position, so a closing order never raises it.
    int64_t exposure_after(bool is_ask, int64_t qty, int64_t price, int64_t extra_bid, int64_t extra_ask) const {
        SideExposure e = tracker_.exposure(mkt());
        int64_t lng = e.position + e.bid_fillable + extra_bid + (is_ask ? 0 : qty);
        int64_t sht = -e.position + e.ask_fillable + extra_ask + (is_ask ? qty : 0);
        int64_t w = std::max<int64_t>({lng, sht, 0});
        int64_t px = price;
        if (book_.valid()) px = std::max<int64_t>(px, std::llround(book_.top(1).mid() * double(pow10i(cfg_.spec.price_decimals))));
        return cfg_.spec.notional_micro_ceil(w, px);
    }
    void pull_quotes(int64_t now) {
        for (const auto& [coi, o] : tracker_.all())
            if ((o.state == OrderState::Open || o.state == OrderState::PartiallyFilled) && !o.modify_pending) submit_cancel(coi, now);
    }
    void submit_cancel(int64_t coi, int64_t now) {
        Intent c; c.kind = IntentKind::Cancel; c.market = mkt(); c.order_ref = coi;
        try { c.slot = slot_key(mkt(), tracker_.get(coi).is_ask); } catch (...) {}
        std::string why = batcher_.submit(c, now);
        if (!why.empty()) log("[ORD] cancel refused: " + why);
    }
    void quote(int64_t now) {
        std::string block = quoting_block(now);
        if (!block.empty()) {
            if (cfg_.mode == Mode::Live && started_ && block != last_block_) log("[QUOTE] not quoting: " + block);
            last_block_ = block;
            if (!stopping_ && block.rfind("halt: ", 0) != 0 && block != "nonce frozen" && block != "rate-limit back-off") pull_quotes(now);
            return;
        }
        if (!last_block_.empty()) { log("[QUOTE] quoting"); last_block_.clear(); }
        const int64_t m = mkt();
        const int64_t pscale = pow10i(cfg_.spec.price_decimals);
        const double inv = double(tracker_.position(m)) / double(pow10i(cfg_.spec.size_decimals));
        const int64_t band = int64_t(std::floor(quoter_.requote_band() * double(pscale) + 1e-6));
        const int64_t book_t = quoter_.last_book().t_ms;
        int64_t pend_bid = 0, pend_ask = 0;   // creates from this same pass count toward the worst case
        for (int s = 0; s < 2; ++s) {
            const bool is_ask = s == 1;
            int64_t coi = 0;
            try { coi = tracker_.live_on_side(m, is_ask); }
            catch (const OrderStateError& e) { sticky_halt("two_orders", e.what(), now); return; }
            bool want = quoter_.quote_allowed(!is_ask, inv, book_t, false);
            int64_t target = std::llround(quoter_.quote_target(!is_ask, inv) * double(pscale));
            if (target <= 0) want = false;
            if (coi) {
                const Order& o = tracker_.get(coi);
                if ((o.state == OrderState::Open || o.state == OrderState::PartiallyFilled) && (!want || std::llabs(o.price - target) > band))
                    submit_cancel(coi, now);
                continue;
            }
            if (!want) continue;
            QuoteSize q;
            try { q = size_quote(cfg_.spec, sizing_, target); }
            catch (const SpecError& e) { ++size_refusals_; continue; }
            // A quote that CLOSES the position is sized to the position itself when the two differ by less than the
            // smallest valid order (2026-10-28 live: bought 1.24 AVAX, the $11 sell came out 1.23 at the higher price
            // and left 0.01 of dust no order can ever sell). Only when that size is itself a valid order within the cap.
            if (const int64_t pos = tracker_.position(m); is_ask ? pos > 0 : pos < 0) {
                const int64_t p = pos > 0 ? pos : -pos;
                if (p != q.base && std::llabs(p - q.base) < smallest_valid(cfg_.spec, sizing_, target).base &&
                    validate_order(cfg_.spec, sizing_, p, target).empty() &&
                    cfg_.spec.notional_micro_ceil(p, target) <= sizing_.cap_micro)
                    q = {p, cfg_.spec.notional_micro_floor(p, target)};
            }
            RiskGovernor::OrderCheck c;
            c.market = m; c.is_ask = is_ask; c.qty = q.base;
            c.notional_micro = cfg_.spec.notional_micro_ceil(q.base, target);
            c.max_position_base = int64_t(cfg_.risk.max_position_quotes) * q.base;
            c.exposure = tracker_.exposure(m);
            c.gross_micro = gross_micro();
            c.exposure_after_micro = exposure_after(is_ask, q.base, target, pend_bid, pend_ask);
            c.equity_micro = equity_;
            c.now_unix_s = now / 1000;
            std::string why = d_.risk.check_order(c);
            if (!why.empty()) { ++risk_refusals_; last_risk_refusal_ = why; continue; }
            Intent i; i.kind = IntentKind::Create; i.market = m; i.is_ask = is_ask; i.slot = slot_key(m, is_ask);
            i.order_ref = alloc_.next(); i.base = q.base; i.price = target; i.tif = TimeInForce::PostOnly;
            i.expiry_ms = now + cfg_.order_expiry_ms;
            std::string bad = batcher_.submit(i, now);
            if (!bad.empty()) log("[ORD] create refused: " + bad);
            else (is_ask ? pend_ask : pend_bid) += q.base;
        }
    }
    void flush_batcher(int64_t now) {
        if (nm_.frozen() || !nm_.synced()) return;
        if (!limiter_.can_send(now, true)) return;
        const bool quoting_budget = limiter_.can_send(now, false);
        auto may_create = [this, now, quoting_budget](uint64_t slot) {
            int64_t m = int64_t(slot >> 1);
            bool ask = slot & 1;
            if (!quoting_budget || !tracker_.slot_free(m, ask)) return false;
            return quoting_allowed(now);
        };
        // the batcher applies the sendTx budget (quoting vs the safety reserve) and charges it; the all-request
        // budget was checked just above and is charged in dispatch
        auto batch = batcher_.next_batch(now, send_limiter_, may_create);
        if (batch.empty()) return;
        // final risk re-check of creates against the exposure right now
        std::vector<Intent> go;
        int64_t pend_bid = 0, pend_ask = 0;
        for (auto& i : batch) {
            if (i.kind == IntentKind::Create && !i.reduce_only) {
                RiskGovernor::OrderCheck c;
                c.market = i.market; c.is_ask = i.is_ask; c.qty = i.base;
                c.notional_micro = cfg_.spec.notional_micro_ceil(i.base, i.price);
                c.max_position_base = int64_t(cfg_.risk.max_position_quotes) * i.base;
                c.exposure = tracker_.exposure(i.market); c.gross_micro = gross_micro(); c.equity_micro = equity_;
                c.exposure_after_micro = exposure_after(i.is_ask, i.base, i.price, pend_bid, pend_ask);
                c.now_unix_s = now / 1000;
                if (!d_.risk.check_order(c).empty()) { ++risk_refusals_; continue; }
                (i.is_ask ? pend_ask : pend_bid) += i.base;   // creates in the same request add up
            }
            if (i.kind == IntentKind::Cancel && tracker_.has(i.order_ref) && !is_fillable(tracker_.get(i.order_ref).state)) continue;
            go.push_back(i);
        }
        if (!go.empty()) dispatch(go, now, Origin::Quote, true);
    }

    // ── nonce resolution ──────────────────────────────────────────────────────────────────────────────────────
    void resolve_nonce(int64_t now, bool force = false) {
        if (!force && now - last_nonce_fix_ms_ < 2'000) return;
        last_nonce_fix_ms_ = now;
        auto nn = rest_.next_nonce(cfg_.api_key_index);
        if (!nn) return;
        NonceManager::Resync r;
        try { r = nm_.resync(*nn); }
        catch (const NonceError& e) { sticky_halt("nonce", std::string("nonce stream inconsistent: ") + e.what(), now); return; }
        jrec_soft({{"ev", "nonce_resync"}, {"next", *nn}, {"applied", r.applied}, {"unconfirmed", r.unconfirmed}});
        for (int64_t n : r.applied) {
            fates_[n] = Fate::Applied;
            known_result_.erase(n);
            auto it = sent_.find(n);
            if (it == sent_.end()) continue;
            // a dead-man nonce consumed per nextNonce proves nothing about its result (it can carry 21714): a SCHEDULE is
            // never counted as armed from here; an ABORT is counted as executed (nothing armed) — the safe reading
            abort_left_unconfirmed(it->second, now);
            sent_.erase(it);
        }
        // Unconfirmed: re-send the byte-identical bytes (same nonce, same client index) — the only content ever
        // allowed on such a nonce. An accepted resend becomes Accepted-pending; "invalid nonce" means it was applied.
        for (int64_t n : r.unconfirmed) {
            if (known_result_.count(n)) {   // processed with a rejection AND not consumed per nextNonce: final, never resent
                try { nm_.confirm_rejected_not_consumed(n); } catch (const NonceError& e) { log(e.what()); }
                known_result_.erase(n);
                fates_[n] = Fate::NotApplied;
                if (auto it = sent_.find(n); it != sent_.end()) { abort_left_unconfirmed(it->second, now); sent_.erase(it); }
                continue;
            }
            auto bytes = nm_.resend_bytes(n);
            auto it = sent_.find(n);
            if (!bytes || it == sent_.end()) continue;
            if (!limiter_.can_send(now, true)) break;
            limiter_.record(now);
            send_limiter_.record(now);
            persist_rate(now);
            SignedTx t; t.tx_info = *bytes; t.nonce = n;
            t.tx_type = it->second.intent.kind == IntentKind::Create ? kTxCreateOrder : it->second.intent.kind == IntentKind::Cancel
                        ? kTxCancelOrder : it->second.intent.kind == IntentKind::CancelAll ? kTxCancelAll : kTxModifyOrder;
            SendTxResult res = sender_.send({t});
            jrec_soft({{"ev", "resend"}, {"nonce", n}, {"result", send_kind_name(res.kind)}});
            if (res.kind == SendTxResult::Kind::Accepted) nm_.on_accepted(n);
            else if (res.kind == SendTxResult::Kind::InvalidNonce) { try { nm_.confirm_applied(n); } catch (...) {} }
            else if (res.kind == SendTxResult::Kind::RateLimited) on_rate_limited(now, "resend");
            else if (int64_t ea = tx_expired_at_ms(*bytes); ea > 0 && now > ea) { try { nm_.confirm_not_applied(n, now); fates_[n] = Fate::NotApplied; } catch (...) {} }
        }
        if (nm_.frozen() && nm_.unconfirmed().empty()) {
            auto again = rest_.next_nonce(cfg_.api_key_index);
            if (again) { try { nm_.resync(*again); } catch (const NonceError& e) { sticky_halt("nonce", e.what(), now); } }
        }
        if (!nm_.frozen()) { quiet_freeze_ = false; set_condition("nonce_frozen", false, false, Pull::None, "", now); flush_safety(now); }
        request_reconcile(now);
    }

    // ── reconciliation ────────────────────────────────────────────────────────────────────────────────────────
    void check_leverage(int64_t now) {
        double lev = leverage_from_imf(imf_);
        bool ok = have_imf_ && lev > 0 && std::fabs(lev - double(cfg_.risk.leverage)) < 0.01;
        leverage_ok_ = ok;
        std::string detail = have_imf_ ? "market IMF " + imf_ + "% = " + std::to_string(lev) + "x, config " + std::to_string(cfg_.risk.leverage) + "x"
                                       : "the market's leverage is not readable (set it in the Lighter app)";
        set_condition("leverage", !ok, false, Pull::Quotes, detail, now);
    }

    void reconcile(int64_t now, const std::string& reason) {
        bool fast = reason == "periodic" && cfg_.reconcile_fast_timeout_ms > 0 && fast_rec_fails_ < cfg_.reconcile_fast_max_fails;
        int full = rest_.timeout_ms();
        if (fast) rest_.set_timeout_ms(cfg_.reconcile_fast_timeout_ms);
        bool ok = reconcile_inner(now, reason);
        rest_.set_timeout_ms(full);
        if (ok) fast_rec_fails_ = 0;
        else if (fast) ++fast_rec_fails_;
    }

    // false = the account or open-order read failed (nothing applied)
    bool reconcile_inner(int64_t now, const std::string& reason) {
        ++reconciles_;
        last_reconcile_ms_ = now;
        auto acct = rest_.account();
        if (!acct) { log("[REC] account unavailable: " + rest_.last_error()); return false; }
        auto open = rest_.active_orders(mkt());
        if (!open) { log("[REC] active orders unavailable: " + rest_.last_error()); return false; }
        collateral_ = acct->collateral_micro;
        // Loss stops run on ONE measure: REST total asset value (incl. unrealised) — the start baseline's measure too.
        equity_ = acct->portfolio_micro;
        equity_known_ = true;
        try { if (d_.risk.on_equity(equity_, now / 1000) == RiskAction::CancelAllAndFlatten) risk_stop(now); }
        catch (const RiskError& e) { sticky_halt("risk_state", e.what(), now); }
        if (collateral_ > cfg_.max_collateral_micro)
            sticky_halt("capital_guard", "collateral $" + usd(collateral_) + " above max $" + usd(cfg_.max_collateral_micro), now);
        int64_t rest_pos = 0;
        for (const auto& p : acct->positions) {
            if (p.market == mkt() && p.market_known) { rest_pos = p.base; imf_ = p.imf; have_imf_ = true; entry_px_ = p.entry_px; entry_pos_ = p.base; }
            else if (p.market != mkt() && p.nonzero()) sticky_halt("unconfigured_position", "position in market " + std::to_string(p.market), now);
        }
        check_leverage(now);
        // 1. exchange open orders we do not hold as fillable → cancel; foreign → halt
        std::set<int64_t> exch_open;
        for (const auto& o : *open) {
            exch_open.insert(o.client_index);
            bool ours_now = o.client_index > 0 && tracker_.has(o.client_index) && is_fillable(tracker_.get(o.client_index).state);
            if (ours_now) {
                const Order& t = tracker_.get(o.client_index);
                if (t.state == OrderState::PendingNew || t.state == OrderState::Unknown) {
                    try { if (tracker_.on_ack(o.client_index, o.order_index, now)) submit_cancel(o.client_index, now); }
                    catch (const OrderStateError& e) { log(e.what()); }
                }
                continue;
            }
            // REST can trail the stream: an order of ours that ended within the last 5 s may still show open
            if (o.client_index > 0 && tracker_.has(o.client_index) && now - tracker_.get(o.client_index).since_ms < 5'000) continue;
            unknown_exchange_order(o, now, "reconcile");
        }
        // 2. our fillable orders the exchange does not show open → backfill fills, then look them up
        std::vector<int64_t> missing;
        for (const auto& [coi, o] : tracker_.all())
            if (is_fillable(o.state) && !exch_open.count(coi) && now - o.since_ms >= cfg_.mismatch_grace_ms) missing.push_back(coi);
        // also once a minute, so the REST-confirmed trade cursor keeps up with the stream (a later backfill stays short)
        bool need_trades = !missing.empty() || backfill_needed_ || rest_pos != tracker_.position(mkt()) || reconciles_ % 6 == 0;
        if (need_trades) backfill(now);
        if (!missing.empty()) {
            auto inactive = rest_.inactive_orders(mkt(), 100);
            if (inactive) {
                std::map<int64_t, AcctOrder> by;
                for (const auto& o : *inactive) by[o.client_index] = o;
                for (int64_t coi : missing) {
                    const Order& t = tracker_.get(coi);
                    if (!is_fillable(t.state)) continue;
                    auto it = by.find(coi);
                    try {
                        if (it != by.end() && it->second.status >= 3) {
                            int64_t ex_filled = it->second.market_known ? it->second.filled : t.filled;
                            tracker_.reconcile(coi, it->second.status, ex_filled, it->second.order_index, now);
                            jrec_soft({{"ev", "reconcile_order"}, {"coi", coi}, {"status", it->second.status}, {"filled", ex_filled}, {"source", "reconcile"}});
                        } else if (not_applied_cois_.count(coi) || (t.state == OrderState::Unknown && now - t.since_ms > 60'000 && !nm_.frozen())) {
                            tracker_.reconcile(coi, kStatusCanceled, t.filled, 0, now);   // never reached the book
                            not_applied_cois_.erase(coi);
                            jrec_soft({{"ev", "reconcile_order"}, {"coi", coi}, {"status", "not-placed"}, {"source", "reconcile"}});
                        }
                    } catch (const OrderStateError& e) { log(e.what()); }
                }
            }
        }
        // 3. position: the exchange wins after the grace period
        observe_exchange_position(rest_pos, now, "rest");
        if (tracker_.reconcile_needed() && !backfill_needed_) tracker_.clear_reconcile_flag();
        jrec_soft({{"ev", "reconcile"}, {"why", reason}, {"open", open->size()}, {"rest_pos", rest_pos}, {"pos", tracker_.position(mkt())},
                   {"collateral_micro", collateral_}, {"imf", imf_}});
        if (consistent_since_mismatch_ >= 0) {
            if (rest_pos == tracker_.position(mkt())) ++consistent_since_mismatch_;
            if (consistent_since_mismatch_ >= 2) {
                consistent_since_mismatch_ = -1;
                set_condition("position_mismatch", false, false, Pull::None, "", now);
            }
        }
        if (feed_.live() && halts_.count("account_feed")) set_condition("account_feed", false, false, Pull::None, "", now);
        return true;
    }

    void unknown_exchange_order(const AcctOrder& o, int64_t now, const std::string& where) {
        if (o.status != 2 && o.status != 0 && o.status != 1) return;
        int64_t ref = o.order_index >= kMinExchangeOrderIndex ? o.order_index : o.client_index;
        if (ref <= 0) return;
        auto& last = foreign_cancel_ms_[ref];
        bool earlier_run = o.client_index > 0 && (o.client_index >> 20) >= 1 && (o.client_index >> 20) <= run_id_;
        if (last == 0) {
            ++foreign_cancels_;
            jrec_soft({{"ev", "unknown_order"}, {"coi", o.client_index}, {"oi", o.order_index}, {"where", where}, {"ours", earlier_run}});
            if (earlier_run) alert("unknown_order", "an open order from run " + std::to_string(o.client_index >> 20) + " is still on the book: cancelling", now);
            else sticky_halt("unowned", "account used by something else: open order coi " + std::to_string(o.client_index) +
                                            " oi " + std::to_string(o.order_index) + " is not ours", now);
        }
        if (now - last < 5'000 && last != 0) return;
        last = now;
        Intent c; c.kind = IntentKind::Cancel; c.market = o.market >= 0 ? o.market : mkt(); c.order_ref = ref;
        try { check_market_index(c.market); } catch (...) { c.market = mkt(); }
        if (!dispatch({c}, now, Origin::Direct, false)) pending_safety_.push_back(c);
    }

    void backfill(int64_t now) {
        // from the last trade id REST itself has confirmed complete — not the stream high-water mark: a stream that
        // skipped fills (dropped connection) would otherwise hide them below its newest id. Duplicates are skipped by id.
        auto tr = rest_.trades_since(rest_trade_cursor_);
        if (!tr) { backfill_needed_ = true; return; }
        on_fills(tr->fills, false, now);
        backfill_needed_ = !tr->complete;
        if (tr->complete) rest_trade_cursor_ = std::max(rest_trade_cursor_, tr->newest_id);
        else alert("backfill", "fill backfill incomplete (more than 500 fills since the last REST-confirmed trade)", now);
    }

    void observe_exchange_position(int64_t exch, int64_t now, const char* src) {
        exch_pos_ = exch;
        have_exch_pos_ = true;
        int64_t mine = tracker_.position(mkt());
        int64_t& since = mismatch_since_[src];   // per source: a stale REST view and the stream must each persist on their own
        if (exch == mine) { since = 0; return; }
        if (since == 0) { since = now; request_reconcile(now + cfg_.mismatch_grace_ms); return; }
        if (now - since < cfg_.mismatch_grace_ms) return;
        // R-REC-3: act once — adopt the exchange position, halt quoting, alert
        ++mismatch_acts_;
        mismatch_since_.clear();
        tracker_.set_position(mkt(), exch);
        ws_pos_ = exch;   // the adopted value supersedes an older streamed one until the stream's next update
        std::string day = utc_day_of_ms(now);
        if (day != mismatch_day_) { mismatch_day_ = day; mismatch_today_ = 0; }
        ++mismatch_today_;
        try { jrec({{"ev", "position_adopt"}, {"from", mine}, {"to", exch}, {"source", src}}, true); }
        catch (const JournalError& e) { journal_failure(e.what(), now); }
        if (mismatch_today_ >= 3) sticky_halt("mismatch_repeat", "position mismatch adopted 3 times today", now);
        else set_condition("position_mismatch", true, false, Pull::CancelAll, "position " + fmt_base(mine) + " vs exchange " + fmt_base(exch) +
                                                                                   " (" + src + "): adopted the exchange's", now);
        consistent_since_mismatch_ = 0;
        alert("mismatch", "position mismatch: local " + fmt_base(mine) + " vs exchange " + fmt_base(exch) + " — adopted the exchange's, quoting halted", now);
        request_reconcile(now + 1'000);
    }

    // ── tick body ─────────────────────────────────────────────────────────────────────────────────────────────
    void tick_inner(int64_t now) {
        d_.journal.open_for(now);
        // operator files, every second
        if (now - last_files_ms_ >= 1'000) {
            last_files_ms_ = now;
            if (d_.risk.check_kill() == RiskAction::CancelAllAndFlatten) kill(now);
            bool pause = std::filesystem::exists(cfg_.state_dir + "/PAUSE");
            set_condition("pause", pause, false, Pull::Quotes, "PAUSE file present: quotes pulled, position kept", now);
            if (d_.risk.halted(now / 1000) && !halts_.count("risk_stop")) risk_stop(now);
        }
        // feeds
        if (started_) {
            bool pub_down = !public_open_ || now - last_public_ms_ > cfg_.feed_silence_ms;
            set_condition("public_feed", pub_down, false, Pull::CancelAll, "public order-book stream down or silent 15 s", now);
            if (public_open_ && now - last_public_ms_ > cfg_.feed_silence_ms) public_reconnect_ = true;
            bool stale = book_snapshot_ && now - last_book_change_ms_ > cfg_.stale_ms;
            set_condition("book_stale", stale, false, Pull::Quotes, "no book change for 120 s", now);
            if (feed_.live() && !feed_was_live_) { request_reconcile(now); backfill_needed_ = true; if (!live_since_ms_) live_since_ms_ = now; }   // reconnect: backfill + reconcile
            feed_was_live_ = feed_.live();
            if (offset_.jumps() > clock_jumps_seen_) {
                clock_jumps_seen_ = offset_.jumps();
                set_condition("clock", true, false, Pull::Quotes, "server clock offset moved by more than 1 s", now);
                clock_halt_until_ = now + 30'000;
            }
            if (clock_halt_until_ && now > clock_halt_until_) { clock_halt_until_ = 0; set_condition("clock", false, false, Pull::None, "", now); }
            if (stall_until_ && now > stall_until_) { stall_until_ = 0; set_condition("stall", false, false, Pull::None, "", now); }
        }
        auto fout = feed_.tick(now);
        for (auto& f : fout) account_out_.push_back(std::move(f));
        // order timeouts → Unknown (still exposure) → reconcile
        auto timed_out = tracker_.on_timeout(now, cfg_.ack_timeout_ms);
        if (!timed_out.empty()) {
            for (int64_t coi : timed_out) jrec_soft({{"ev", "order_unknown"}, {"coi", coi}});
            request_reconcile(now);
        }
        for (const auto& [coi, o] : tracker_.all())
            if (o.state == OrderState::Unknown && now - o.since_ms > cfg_.unknown_alert_ms && !unknown_alerted_.count(coi)) {
                unknown_alerted_.insert(coi);
                alert("unknown_order_age", "order " + std::to_string(coi) + " Unknown for more than 30 s", now);
            }
        // nonce stream
        if (nm_.frozen()) {
            if (quiet_freeze_ && now - quiet_since_ms_ > 10'000) quiet_freeze_ = false;   // a race freeze that will not clear is a real one
            if (!quiet_freeze_) set_condition("nonce_frozen", true, false, Pull::None, "nonce stream frozen: resyncing from nextNonce", now);
            resolve_nonce(now);
        } else if (nm_.accepted_pending() > 0) {
            int64_t oldest = INT64_MAX;
            for (const auto& [n, s] : sent_) if (nm_.is_accepted(n)) oldest = std::min(oldest, s.sent_ms);
            if (oldest != INT64_MAX && now - oldest > cfg_.accepted_settle_ms) resolve_nonce(now);
        }
        // reconciliation
        int64_t every = feed_.live() ? cfg_.reconcile_every_ms : cfg_.reconcile_down_every_ms;
        if (now >= next_reconcile_ms_ && now - last_reconcile_ms_ >= cfg_.reconcile_min_gap_ms && !limiter_.backing_off(now)) {
            reconcile(now, "periodic");
            next_reconcile_ms_ = now + every;
        }
        // position watch on the streamed exchange position
        if (have_ws_pos_ && feed_.live()) observe_exchange_position(ws_pos_, now, "stream");
        // dead-man
        if (feed_.live() && now - last_arm_try_ms_ >= cfg_.deadman_rearm_ms && !limiter_.backing_off(now) && !nm_.frozen()) arm_deadman(now);
        {   // quoting needs a confirmed deadline ≥ 60 s ahead; one halt/alert per episode (after a 30 s grace once live)
            bool due = deadman_deadline_ms_ != 0 || (live_since_ms_ && now - live_since_ms_ > 30'000);
            set_condition("deadman", due && !deadman_ok(now), false, Pull::Quotes,
                          "no confirmed dead-man deadline at least 60 s ahead: quotes pulled", now);
        }
        flush_safety(now);
        position_stop_step(now);
        flatten_step(now);
        if (cfg_.mode == Mode::Probe) probe_step(now);
        else { apply_bn_pull(now); quote(now); }
        flush_batcher(now);
        // day roll
        std::string day = utc_day_of_ms(now);
        if (day_.empty()) { day_ = day; seed_day_from_journal(day); }
        if (day != day_) {
            alert("daily", "day " + day_ + ": " + std::to_string(fills_day_) + " fills, $" + usd(volume_day_micro_) + " traded, position " +
                           fmt_base(tracker_.position(mkt())) + ", equity $" + usd(equity_), now);
            day_ = day;
            fills_day_ = 0;
            volume_day_micro_ = 0;
            first_fill_alerted_ = false;
        }
        if (now - last_prune_ms_ >= 60'000) {   // bounded memory/CPU on a 24/7 run
            last_prune_ms_ = now;
            tracker_.prune_terminal(now, 600'000);
            while (fates_.size() > 4'096) fates_.erase(fates_.begin());
            for (auto it = unknown_alerted_.begin(); it != unknown_alerted_.end();)
                it = tracker_.has(*it) ? std::next(it) : unknown_alerted_.erase(it);
        }
        if (now - last_status_ms_ >= cfg_.status_every_ms) write_status(now, false);
        d_.journal.flush_due(now);
    }

    // ── probe ─────────────────────────────────────────────────────────────────────────────────────────────────
    struct ProbeState {
        int step = 0;
        int64_t step_ms = 0;
        int64_t coi = 0;
        int64_t rearm_nonce = -1;
        bool retried = false;
        bool done = false, ok = false;
        nlohmann::json report = nlohmann::json::object();
    } probe_;
    void probe_fail(const std::string& why, int64_t now) {
        probe_.done = true; probe_.ok = false; probe_.report["result"] = "FAIL"; probe_.report["why"] = why;
        jrec_soft({{"ev", "probe"}, {"result", "FAIL"}, {"why", why}});
        alert("probe", "probe FAILED: " + why, now);
    }
    void probe_step(int64_t now) {
        if (probe_.done) return;
        auto& p = probe_;
        auto step_to = [&](int s) { p.step = s; p.step_ms = now; };
        switch (p.step) {
            case 0: {   // wait for the account stream + a book
                if (!feed_.live() || !book_snapshot_ || !book_.valid() || !deadman_ok(now)) { if (p.step_ms == 0) p.step_ms = now; if (now - p.step_ms > 60'000) probe_fail("no account stream, book or confirmed dead-man within 60 s", now); return; }
                if (!leverage_ok_) { p.report["leverage_warning"] = "configured leverage does not match the market IMF " + imf_; }
                p.report["imf"] = imf_;
                p.report["position"] = fmt_base(tracker_.position(mkt()));
                int64_t scale = pow10i(cfg_.spec.price_decimals);
                hl::Book hb = book_.top(1);
                int64_t bb = std::llround(hb.bids[0].px * double(scale)), ba = std::llround(hb.asks[0].px * double(scale));
                std::vector<ProbeStep> plan;
                try { plan = build_probe_plan(cfg_.spec, sizing_, bb, ba, alloc_.next(), now); }
                catch (const std::exception& e) { probe_fail(std::string("probe plan: ") + e.what(), now); return; }
                const CreateOrderParams& c = plan[0].create;
                p.coi = c.client_order_index;
                Intent i; i.kind = IntentKind::Create; i.market = c.market_index; i.is_ask = false; i.slot = slot_key(c.market_index, false);
                i.order_ref = c.client_order_index; i.base = c.base_amount; i.price = c.price; i.tif = TimeInForce::PostOnly;
                i.expiry_ms = c.order_expiry_ms;
                p.report["order"] = {{"coi", p.coi}, {"price", format_decimal(c.price, cfg_.spec.price_decimals)},
                                     {"size", format_decimal(c.base_amount, cfg_.spec.size_decimals)}, {"best_bid", format_decimal(bb, cfg_.spec.price_decimals)}};
                if (!dispatch({i}, now, Origin::Probe, false)) { probe_fail("create not accepted by the API", now); return; }
                step_to(1);
                return;
            }
            case 1: {   // ack
                const Order& o = tracker_.get(p.coi);
                if (o.state == OrderState::Open) { p.report["ack_ms"] = now - p.step_ms; p.report["order_index"] = o.order_index; step_to(2); return; }
                if (o.state == OrderState::Rejected || o.state == OrderState::Cancelled || o.state == OrderState::Filled) {
                    probe_fail(std::string("probe order ended ") + state_name(o.state) + " before the ack", now); return; }
                if (now - p.step_ms > 5'000) { request_reconcile(now); if (now - p.step_ms > 15'000) probe_fail("no ack within 15 s", now); }
                return;
            }
            case 2: {   // cancel
                Intent c; c.kind = IntentKind::Cancel; c.market = mkt(); c.order_ref = p.coi; c.slot = slot_key(mkt(), false);
                if (!dispatch({c}, now, Origin::Probe, false)) { probe_fail("cancel not accepted by the API", now); return; }
                step_to(3);
                return;
            }
            case 3: {
                const Order& o = tracker_.get(p.coi);
                if (o.state == OrderState::Cancelled) { p.report["cancel_ms"] = now - p.step_ms; step_to(4); return; }
                if (o.state == OrderState::Filled || o.filled > 0) { probe_fail("THE PROBE ORDER FILLED", now); return; }
                if (now - p.step_ms > 5'000) { request_reconcile(now); if (now - p.step_ms > 15'000) probe_fail("no cancel confirm within 15 s", now); }
                return;
            }
            case 4: {   // REST: no open orders
                auto open = rest_.active_orders(mkt());
                if (!open) { if (now - p.step_ms > 15'000) probe_fail("active orders unavailable", now); return; }
                if (!open->empty()) { if (now - p.step_ms > 10'000) probe_fail(std::to_string(open->size()) + " open order(s) remain", now); return; }
                p.report["open_orders_after"] = 0;
                // dead-man: the first schedule is confirmed (step 0 waited for it); now one RE-ARM while it is queued —
                // this is what tells Replace from AbortFirst on the real exchange
                p.report["deadman_first_deadline_ms"] = deadman_deadline_ms_;
                arm_deadman(now);
                p.rearm_nonce = deadman_last_sched_nonce_;
                step_to(5);
                return;
            }
            case 5: {
                if (p.rearm_nonce >= 0 && deadman_confirmed_nonce_ == p.rearm_nonce) {
                    p.report["deadman_rearm"] = "executed";
                    p.report["deadman_mode"] = deadman_abort_first_ ? "abort_first (21712: schedule refused while one is queued)" : "replace";
                    p.report["deadman_deadline_ms"] = deadman_deadline_ms_;
                    step_to(6);
                    return;
                }
                if (deadman_failures_ > 0) {
                    if (deadman_abort_first_ && !p.retried) {   // 21712 seen: try once more as abort + schedule
                        p.retried = true;
                        arm_deadman(now);
                        p.rearm_nonce = deadman_last_sched_nonce_;
                        p.step_ms = now;
                        return;
                    }
                    probe_fail("dead-man re-arm failed: scheduled cancel-all semantics differ from the spec", now);
                    return;
                }
                if (now - p.step_ms > 15'000) probe_fail("dead-man re-arm not confirmed within 15 s", now);
                return;
            }
            case 6: {
                p.done = true; p.ok = true; p.report["result"] = "PASS";
                jrec_soft({{"ev", "probe"}, {"result", "PASS"}, {"report", p.report}});
                alert("probe", "probe PASSED: " + p.report.dump(), now);
                return;
            }
        }
    }

    // ── status ────────────────────────────────────────────────────────────────────────────────────────────────
    nlohmann::json status_json(int64_t now) const {
        nlohmann::json orders = nlohmann::json::array();
        for (const auto& [coi, o] : tracker_.all())
            if (is_fillable(o.state))
                orders.push_back({{"coi", coi}, {"side", o.is_ask ? "ask" : "bid"}, {"state", state_name(o.state)},
                                  {"price", format_decimal(o.price, cfg_.spec.price_decimals)}, {"remaining", format_decimal(o.remaining(), cfg_.spec.size_decimals)}});
        nlohmann::json h = nlohmann::json::array();
        for (const auto& [k, v] : halts_) h.push_back({{"reason", k}, {"sticky", v.sticky}, {"detail", v.detail}, {"since", v.since}});
        const auto& rs = d_.risk.state();
        return {{"updated_ms", now}, {"label", cfg_.label}, {"mode", mode_name(cfg_.mode)}, {"run_id", run_id_},
                {"market", cfg_.spec.symbol}, {"started", started_}, {"quoting", quoting_block(now).empty()},
                {"quoting_block", quoting_block(now)}, {"halts", h},
                {"position_stops", position_stops_}, {"unrealised_usd", double(last_unreal_micro_) / kMicro},
                {"position_held_s", pos_since_ms_ ? (now - pos_since_ms_) / 1000 : 0},
                {"position", format_decimal(tracker_.position(mkt()), cfg_.spec.size_decimals)},
                {"exchange_position", format_decimal(exch_pos_, cfg_.spec.size_decimals)},
                {"equity_usd", double(equity_) / kMicro}, {"collateral_usd", double(collateral_) / kMicro},
                {"day_pnl_usd", rs.initialized ? double(equity_ - rs.sod_equity_micro) / kMicro : 0.0},
                {"gross_usd", double(gross_micro()) / kMicro}, {"orders", orders},
                {"nonce_next", nm_.peek_next()}, {"nonce_frozen", nm_.frozen()}, {"accepted_pending", nm_.accepted_pending()},
                {"requests_last_min", const_cast<RateLimiter&>(limiter_).used(now)}, {"rate_limited", rate_limited_events_},
                {"deadman_deadline_ms", deadman_deadline_ms_}, {"deadman_failures", deadman_failures_},
                {"account_stream_live", feed_.live()}, {"token_deadline_s", feed_.token_deadline_s()},
                {"book_age_ms", last_book_change_ms_ ? now - last_book_change_ms_ : -1}, {"server_offset_ms", offset_.offset()},
                {"fills", fills_}, {"fills_today", fills_day_}, {"reconciles", reconciles_}, {"mismatch_acts", mismatch_acts_},
                {"unknown_orders_cancelled", foreign_cancels_}, {"sequencer_rejects", seq_rejects_}, {"cancel_races", cancel_races_}, {"gaps", gaps_},
                {"risk_refusals", risk_refusals_}, {"last_risk_refusal", last_risk_refusal_}, {"size_refusals", size_refusals_},
                {"journal", d_.journal.path()}, {"journal_bytes", d_.journal.bytes()}, {"journal_failed", journal_failed_},
                {"alerts_sent", d_.alerts.sent()},
                {"bn_pull", bn_status(now)}};
    }
    nlohmann::json bn_status(int64_t now) const {
        if (!bn_.params().on()) return {{"on", false}};
        const auto& o = bn_.last();
        return {{"on", true}, {"w_ms", bn_.params().w_ms}, {"x_bps", bn_.params().x_bps}, {"hold_ms", bn_.params().hold_ms},
                {"age_ms", bn_.last_sample_ms() < 0 ? -1 : now - bn_.last_sample_ms()}, {"frames", ref_frames_}, {"opens", ref_opens_},
                {"lead_bps", std::isfinite(o.lead_bps) ? nlohmann::json(o.lead_bps) : nlohmann::json(nullptr)},
                {"pull_bid", o.bid}, {"pull_ask", o.ask}, {"pulls_bid", bn_.pulls_bid()}, {"pulls_ask", bn_.pulls_ask()}};
    }
    void write_status(int64_t now, bool force) {
        if (cfg_.status_file.empty()) return;
        if (!force && now - last_status_ms_ < cfg_.status_every_ms) return;
        last_status_ms_ = now;
        std::string tmp = cfg_.status_file + ".tmp";
        {
            std::ofstream f(tmp, std::ios::trunc);
            f << status_json(now).dump(1) << "\n";
            if (!f) return;
        }
        std::error_code ec;
        std::filesystem::rename(tmp, cfg_.status_file, ec);
    }

    // ── state ─────────────────────────────────────────────────────────────────────────────────────────────────
    EngineConfig cfg_;
    Deps d_;
    int64_t run_id_;
    ClientIndexAllocator alloc_;
    NonceManager nm_;
    RateLimiter limiter_, send_limiter_;
    Batcher batcher_;
    OrderTracker tracker_;
    AccountFeed feed_;
    RestApi rest_;
    Sender sender_;
    hl::Simulator quoter_;
    bnpull::Lead bn_;
    int64_t ref_frames_ = 0, ref_opens_ = 0;
    bool ref_stale_ = true;
    SizingRules sizing_;
    lighter::Book book_;
    ClockOffset offset_;

    bool started_ = false, stopping_ = false, journal_failed_ = false;
    std::string refused_;
    std::map<std::string, Halt> halts_;
    std::vector<std::string> account_out_, public_out_;
    bool feed_was_live_ = false;
    bool account_open_ = false, public_open_ = false, public_reconnect_ = false, book_snapshot_ = false, book_dirty_ = false;
    int64_t last_public_ms_ = 0, last_public_ping_ms_ = 0, last_book_change_ms_ = 0;
    std::map<int64_t, SentTx> sent_;
    std::map<int64_t, Fate> fates_;
    std::set<int64_t> not_applied_cois_;
    std::vector<Intent> pending_safety_;
    std::map<int64_t, int64_t> foreign_cancel_ms_;
    std::set<int64_t> unknown_alerted_;
    std::deque<int64_t> rl_times_;
    int64_t watermark_ = 0, last_trade_id_ = 0, rest_trade_cursor_ = 0;
    bool backfill_needed_ = false;
    int64_t exch_pos_ = 0, ws_pos_ = 0;
    int64_t entry_px_ = 0, entry_pos_ = 0, pos_since_ms_ = 0, pos_stop_until_ms_ = 0, last_unreal_micro_ = 0;
    int64_t position_stops_ = 0;
    bool have_exch_pos_ = false, have_ws_pos_ = false;
    std::string imf_;
    bool have_imf_ = false, leverage_ok_ = false;
    std::map<std::string, int64_t> mismatch_since_;
    int64_t mismatch_acts_ = 0, mismatch_today_ = 0;
    int consistent_since_mismatch_ = -1;
    std::string mismatch_day_;
    int64_t equity_ = 0, collateral_ = 0;
    bool equity_known_ = false;
    int64_t next_reconcile_ms_ = 0, last_reconcile_ms_ = 0, reconciles_ = 0;
    int64_t last_arm_try_ms_ = 0, deadman_deadline_ms_ = 0, deadman_arms_ = 0, deadman_last_sched_nonce_ = -1, deadman_confirmed_nonce_ = -1;
    int deadman_failures_ = 0;
    bool deadman_abort_first_ = false, deadman_episode_alerted_ = false, deadman_aborted_ = false, deadman_rearming_ = false;
    std::set<int64_t> known_result_;   // nonces whose sequencer rejection was seen but whose consumption is unknown
    int64_t live_since_ms_ = 0, stall_until_ = 0;
    int64_t last_nonce_fix_ms_ = 0, last_files_ms_ = 0, last_status_ms_ = 0, last_prune_ms_ = 0;
    int64_t clock_jumps_seen_ = 0, clock_halt_until_ = 0;
    bool flatten_requested_ = false, flatten_gave_up_ = false;
    int flatten_tries_ = 0;
    int64_t last_flatten_ms_ = 0;
    int64_t rate_limited_events_ = 0, fills_ = 0, fills_day_ = 0, volume_day_micro_ = 0, foreign_cancels_ = 0, seq_rejects_ = 0, cancel_races_ = 0;
    bool quiet_freeze_ = false;   // the current nonce freeze came from a cancel-vs-fill race: no halt/alert
    int64_t quiet_since_ms_ = 0;
    int fast_rec_fails_ = 0;
    int64_t gaps_ = 0, books_ = 0, risk_refusals_ = 0, size_refusals_ = 0;
    std::string last_risk_refusal_, last_block_, day_;
    bool first_fill_alerted_ = false;
};

// ── AccountFeed::Sink ─────────────────────────────────────────────────────────────────────────────────────────
inline void Engine::on_orders(const std::vector<AcctOrder>& orders, bool snapshot, int64_t now) {
    std::set<int64_t> seen_open;
    for (const auto& o : orders) {
        if (o.market != mkt()) { unknown_exchange_order(o, now, "stream"); continue; }
        const bool ours = o.client_index > 0 && tracker_.has(o.client_index);
        if (!ours) {
            if (o.status == 2 || o.status == 0 || o.status == 1) unknown_exchange_order(o, now, "stream");
            continue;
        }
        if (o.status == 2 || o.status == 0 || o.status == 1) seen_open.insert(o.client_index);
        try {
            const Order& t = tracker_.get(o.client_index);
            switch (o.status) {
                case 0: case 1: case 2: case 3:
                    if (t.state == OrderState::PendingNew || t.state == OrderState::Unknown) {
                        bool resend = tracker_.on_ack(o.client_index, o.order_index, now);
                        jrec_soft({{"ev", "ack"}, {"coi", o.client_index}, {"oi", o.order_index}, {"status", o.status}});
                        if (resend) submit_cancel(o.client_index, now);
                    } else if (!is_fillable(t.state) && o.status == 2) {
                        // we think it is over, the exchange shows it open: cancel it and look again
                        unknown_exchange_order(o, now, "stream-terminal");
                    }
                    break;
                default:   // a cancel of some kind (4 canceled, 5 post-only, 6 reduce-only, 12 expired, …)
                    if (is_fillable(t.state)) {
                        tracker_.on_cancelled(o.client_index, o.status, now);
                        jrec_soft({{"ev", "cancelled"}, {"coi", o.client_index}, {"status", o.status}});
                        if (o.status == 15) sticky_halt("liquidation", "order cancelled by liquidation", now);
                    }
                    break;
            }
        } catch (const OrderStateError& e) { log(std::string("[ORD] ") + e.what()); request_reconcile(now); }
    }
    if (snapshot) {
        for (const auto& [coi, t] : tracker_.all())
            if ((t.state == OrderState::Open || t.state == OrderState::PartiallyFilled) && !seen_open.count(coi)) { request_reconcile(now); break; }
    }
}

inline void Engine::on_fills(const std::vector<AcctFill>& fills, bool snapshot, int64_t now) {
    for (const auto& f : fills) {
        if (f.trade_id <= watermark_) continue;   // already inside the position adopted at start
        if (!f.market_known || f.market != mkt()) {
            sticky_halt("unconfigured_position", "fill in market " + std::to_string(f.market) + " (not configured)", now);
            continue;
        }
        if (f.self_trade) {
            sticky_halt("self_trade", "SELF-TRADE trade " + std::to_string(f.trade_id), now);
            continue;
        }
        bool applied = false;
        try { applied = tracker_.on_fill(f.trade_id, f.client_index, f.qty, now, f.market, f.is_ask); }
        catch (const OrderStateError& e) { log(e.what()); continue; }
        last_trade_id_ = std::max(last_trade_id_, f.trade_id);
        if (!applied) continue;
        ++fills_;
        ++fills_day_;
        volume_day_micro_ += cfg_.spec.notional_micro_floor(f.qty, f.price);
        try {
            jrec({{"ev", "fill"}, {"trade_id", f.trade_id}, {"coi", f.client_index}, {"oi", f.order_index}, {"side", f.is_ask ? "sell" : "buy"},
                  {"qty", f.qty}, {"price", f.price}, {"maker", f.maker}, {"type", f.type}, {"position", tracker_.position(mkt())},
                  {"source", snapshot ? "snapshot" : "stream"}}, true);
        } catch (const JournalError& e) { journal_failure(e.what(), now); }
        if (!first_fill_alerted_) { first_fill_alerted_ = true; alert("first_fill", "first fill of the day: " + std::string(f.is_ask ? "sold " : "bought ") + fmt_base(f.qty) + " @ " + format_decimal(f.price, cfg_.spec.price_decimals), now); }
        if (f.type == "liquidation" || f.type == "deleverage") sticky_halt("liquidation", f.type + " fill " + std::to_string(f.trade_id), now);
        bool ours = f.client_index > 0 && (f.client_index >> 20) >= 1 && (f.client_index >> 20) <= run_id_;
        if (!ours && f.type == "trade") sticky_halt("unowned", "account used by something else: fill for order coi " + std::to_string(f.client_index) + " is not ours", now);
    }
}

inline void Engine::on_positions(const std::vector<AcctPosition>& pos, bool snapshot, int64_t now) {
    for (const auto& p : pos) {
        if (p.market == mkt() && p.market_known) {
            ws_pos_ = p.base;
            have_ws_pos_ = true;
            if (!p.imf.empty()) { imf_ = p.imf; have_imf_ = true; check_leverage(now); }
        } else if (p.market != mkt() && p.nonzero()) {
            sticky_halt("unconfigured_position", "position in market " + std::to_string(p.market) + " (not configured)", now);
        }
    }
    (void)snapshot;
}

inline void Engine::on_stats(const AcctStats& s, int64_t now) {
    if (!s.ok) return;
    // user_stats feeds the capital guard and margin usage only; the loss stops run on REST total asset value
    // (reconcile, every 10 s) so the start baseline and every later reading are the same measure.
    collateral_ = s.collateral_micro;
    if (collateral_ > cfg_.max_collateral_micro)
        sticky_halt("capital_guard", "collateral $" + usd(collateral_) + " above max $" + usd(cfg_.max_collateral_micro), now);
    if (s.margin_usage_pct > cfg_.max_margin_pct)
        set_condition("margin", true, false, Pull::Quotes, "margin usage above " + std::to_string(int(cfg_.max_margin_pct)) + " %", now);
    else set_condition("margin", false, false, Pull::None, "", now);
}

inline void Engine::on_txs(const std::vector<AcctTx>& in, int64_t now) {
    // in nonce order: one API key is a FIFO, and an abort must be seen before the schedule that follows it
    std::vector<AcctTx> txs = in;
    std::stable_sort(txs.begin(), txs.end(), [](const AcctTx& a, const AcctTx& b) { return a.nonce < b.nonce; });
    for (const auto& t : txs) {
        if (t.account != cfg_.account_index || t.api_key != cfg_.api_key_index) continue;
        if (t.status == 1 || t.status == 3) continue;   // still pending at the sequencer
        if (!nm_.tracking(t.nonce)) continue;
        TxOutcome o;
        int code = t.app_error;
        if (t.status == 2 && code == 0) o = TxOutcome::Executed;
        else if (code > 0) o = classify_sequencer(-1, code);
        else o = TxOutcome::Unknown;
        if (code == 52002) sticky_halt("standard_limit", "Lighter 52002 on nonce " + std::to_string(t.nonce), now);
        if (nm_.is_unconfirmed(t.nonce) && !consumes_nonce(o)) continue;   // the resync path answers these
        settle(t.nonce, o, code, now);
    }
}

}  // namespace lighter::live
