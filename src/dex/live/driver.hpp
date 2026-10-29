#pragma once
/*  driver.hpp — moves frames between the engine and its two WebSockets, and calls tick(). One step():
      account WS: (re)open with back-off 1 s → 60 s when closed; every received frame → engine; engine replies → WS;
                  a reconnect the engine asks for (shutdown notice, auth error, silence, token near deadline) closes it
                  and the next step reopens it (the engine has already cancelled everything on the close).
      public WS:  the same for the order book (a sequence gap or 15 s of silence asks for a resubscribe).
      reference WS (optional, 2026-10-28): Binance bookTicker for the Binance-lead pull — opened only when the engine
                  wants it, same back-off; frames → engine.on_ref_frame. Its loss only turns the pull off.
      engine.tick(now).
    Live: called every ~20 ms on the engine thread. Offline soak / tests: called after every archive message.   */

#include "dex/live/engine.hpp"
#include "dex/live/transport.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <string>

namespace lighter::live {

class Driver {
public:
    Driver(Engine& e, WsConn& account, WsConn& pub, Clock& clock, std::function<void(const std::string&)> log = {},
           WsConn* ref = nullptr)
        : e_(e), acct_(account), pub_(pub), ref_(ref), clock_(clock), log_(std::move(log)) {}

    void step() {
        auto w0 = std::chrono::steady_clock::now();
        int64_t c0 = clock_.now_ms();
        essential_open_ms_ = 0;
        step_inner();
        // a step that blocked > 2 s (REST timeout, WebSocket open) means the engine quoted on an old book: pull quotes.
        // Time spent (re)opening the ACCOUNT or BOOK stream does not count (2026-10-28): either stream down means every
        // quote was already cancelled (or none exists yet at start), so no stale quote rested meanwhile — counting it
        // cost 10 s without quotes after every restart (3.7 s of opens). The Binance stream's open still counts.
        int64_t blocked = std::max<int64_t>(clock_.now_ms() - c0,
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - w0).count()) - essential_open_ms_;
        if (blocked > kStallMs) { e_.on_stall(blocked, clock_.now_ms()); flush(); }
    }
    static constexpr int64_t kStallMs = 2'000;

private:
    void step_inner() {
        int64_t now = clock_.now_ms();
        side(acct_, acct_state_, now, true);
        side(pub_, pub_state_, now, false);
        while (auto m = acct_.poll()) e_.on_account_frame(*m, clock_.now_ms());
        while (auto m = pub_.poll()) e_.on_public_frame(*m, clock_.now_ms());
        if (ref_ && e_.ref_wanted()) {
            ref_side(now);
            while (auto m = ref_->poll()) e_.on_ref_frame(*m, clock_.now_ms());
        }
        e_.tick(clock_.now_ms());
        flush();
        if (e_.account_reconnect_wanted()) {
            if (log_) log_("account stream: reconnecting (" + e_.account_reconnect_reason() + ")");
            e_.clear_account_reconnect();
            acct_.close();
        }
        if (e_.public_reconnect_wanted()) {
            if (log_) log_("public stream: resubscribing");
            e_.clear_public_reconnect();
            pub_.close();
        }
    }
public:
    void flush() {
        for (auto& f : e_.take_account_out()) acct_.send(f);
        for (auto& f : e_.take_public_out()) pub_.send(f);
    }
    int64_t ref_reconnects() const { return ref_state_.opens > 0 ? ref_state_.opens - 1 : 0; }
    int64_t account_reconnects() const { return acct_state_.opens > 0 ? acct_state_.opens - 1 : 0; }
    int64_t public_reconnects() const { return pub_state_.opens > 0 ? pub_state_.opens - 1 : 0; }

private:
    struct SideState { bool was_open = false; int64_t next_try = 0; int64_t backoff = 1'000; int64_t opens = 0; };

    void side(WsConn& ws, SideState& st, int64_t now, bool account) {
        if (ws.is_open()) return;
        if (st.was_open) {
            st.was_open = false;
            if (account) e_.on_account_closed(now); else e_.on_public_closed(now);
            st.next_try = now + 1'000;
            if (log_) log_(std::string(account ? "account" : "public") + " stream closed: " + ws.last_error());
        }
        if (now < st.next_try) return;
        const auto o0 = std::chrono::steady_clock::now();
        const int64_t oc0 = clock_.now_ms();
        const bool ok = ws.open();
        essential_open_ms_ += std::max<int64_t>(clock_.now_ms() - oc0,   // the same two clocks the stall guard reads
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - o0).count());
        if (ok) {
            st.was_open = true;
            st.backoff = 1'000;
            ++st.opens;
            if (account) e_.on_account_open(clock_.now_ms()); else e_.on_public_open(clock_.now_ms());
            flush();
        } else {
            st.next_try = now + st.backoff;
            st.backoff = std::min<int64_t>(st.backoff * 2, 60'000);
            if (log_) log_(std::string(account ? "account" : "public") + " stream open failed: " + ws.last_error());
        }
    }

    void ref_side(int64_t now) {
        SideState& st = ref_state_;
        if (ref_->is_open()) return;
        if (st.was_open) {
            st.was_open = false;
            e_.on_ref_closed(now);
            st.next_try = now + 1'000;
            if (log_) log_("reference stream closed: " + ref_->last_error());
        }
        if (now < st.next_try) return;
        if (ref_->open()) {
            st.was_open = true;
            st.backoff = 1'000;
            ++st.opens;
            e_.on_ref_open(clock_.now_ms());
        } else {
            st.next_try = now + st.backoff;
            st.backoff = std::min<int64_t>(st.backoff * 2, 60'000);
            if (log_) log_("reference stream open failed: " + ref_->last_error());
        }
    }

    Engine& e_;
    WsConn& acct_;
    WsConn& pub_;
    WsConn* ref_ = nullptr;
    Clock& clock_;
    std::function<void(const std::string&)> log_;
    SideState acct_state_, pub_state_, ref_state_;
    int64_t essential_open_ms_ = 0;   // wall time this step spent opening the account / book stream
};

}  // namespace lighter::live
