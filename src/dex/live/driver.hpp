#pragma once
/*  driver.hpp — moves frames between the engine and its two WebSockets, and calls tick(). One step():
      account WS: (re)open with back-off 1 s → 60 s when closed; every received frame → engine; engine replies → WS;
                  a reconnect the engine asks for (shutdown notice, auth error, silence, token near deadline) closes it
                  and the next step reopens it (the engine has already cancelled everything on the close).
      public WS:  the same for the order book (a sequence gap or 15 s of silence asks for a resubscribe).
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
    Driver(Engine& e, WsConn& account, WsConn& pub, Clock& clock, std::function<void(const std::string&)> log = {})
        : e_(e), acct_(account), pub_(pub), clock_(clock), log_(std::move(log)) {}

    void step() {
        auto w0 = std::chrono::steady_clock::now();
        int64_t c0 = clock_.now_ms();
        step_inner();
        // a step that blocked > 2 s (REST timeout, WebSocket open) means the engine quoted on an old book: pull quotes
        int64_t blocked = std::max<int64_t>(clock_.now_ms() - c0,
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - w0).count());
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
        if (ws.open()) {
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

    Engine& e_;
    WsConn& acct_;
    WsConn& pub_;
    Clock& clock_;
    std::function<void(const std::string&)> log_;
    SideState acct_state_, pub_state_;
};

}  // namespace lighter::live
