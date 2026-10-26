#pragma once
/*  account_feed.hpp — the authenticated account WebSocket (lighter_account_spec.md §2–§4, §7) as a pure protocol
    state machine, plus the parsers for Lighter's Order / Trade / Position / user_stats / Transaction shapes that the
    REST reconciler (rest_api.hpp) shares.

    Protocol (no socket here — the driver moves frames; tests feed frames directly):
      * on_open(): nothing is sent until the server's {"type":"connected"}; then one subscribe per channel:
          account_all_orders/<acct>  (auth)   open orders, snapshot then deltas
          account_all_trades/<acct>           fills (snapshot = ~50 recent, then NEW fills only)
          account_all_positions/<acct>        positions (snapshot then changed markets)
          user_stats/<acct>                   collateral / portfolio value / margin usage
          account_tx/<acct>          (auth)   our transactions' sequencer results (Accepted-pending → final)
        live() once every channel's "subscribed/<name>" snapshot has arrived; a snapshot missing 15 s after the
        subscribe → reconnect.
      * auth token: CreateAuthToken with deadline = now + 8 h − 60 s (Lighter's maximum is 8 h). Renewed every 6 h by
        re-subscribing the two auth channels on the live connection with the fresh token (a re-subscribe returns a
        fresh snapshot); if that snapshot does not arrive within 15 s, or the old token is within 5 min of its
        deadline, the connection is recycled (reconnect = fresh snapshots). Whether the server drops a live
        subscription when its token expires is UNCONFIRMED (spec §2) — renewing 2 h early makes it irrelevant.
      * keepalive: {"type":"ping"} every 60 s (the server drops a client silent for 2 min); a server {"type":"ping"}
        is answered with {"type":"pong"}; no frame at all for 150 s → reconnect.
      * {"type":"shutdown"} → reconnect now. Error frames ({"error":{code,message}}, no channel): 30003 (already
        subscribed) is ignored, an auth error (20001/20013) re-mints the token and reconnects, anything else
        reconnects. Every reconnect request carries its reason.
    The engine (engine.hpp) answers a reconnect with: no new orders, cancel-all, REST backfill of fills down to the
    last seen trade_id, a full reconcile, and quoting only once live() again.                                  */

#include "dex/live/market_spec.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace lighter::live {

// ── value helpers ─────────────────────────────────────────────────────────────────────────────────────────────
inline int64_t jint(const nlohmann::json& j, const char* key, int64_t def = 0) {
    if (!j.is_object() || !j.contains(key)) return def;
    const auto& v = j[key];
    if (v.is_number_integer()) return v.get<int64_t>();
    if (v.is_number_unsigned()) return int64_t(v.get<uint64_t>());
    if (v.is_number_float()) return int64_t(v.get<double>());
    if (v.is_string()) {
        const auto& s = v.get_ref<const std::string&>();
        if (s.empty()) return def;
        char* end = nullptr;
        long long x = std::strtoll(s.c_str(), &end, 10);
        return end && *end == '\0' ? int64_t(x) : def;
    }
    if (v.is_boolean()) return v.get<bool>() ? 1 : 0;
    return def;
}
// Prefer the exact "<key>_str" copy of a large id (ids reach 1e16, past a double's precision).
inline int64_t jid(const nlohmann::json& j, const std::string& key) {
    const std::string ks = key + "_str";
    if (j.is_object() && j.contains(ks)) return jint(j, ks.c_str());
    return jint(j, key.c_str());
}
inline std::string jstr(const nlohmann::json& j, const char* key) {
    if (!j.is_object() || !j.contains(key)) return {};
    const auto& v = j[key];
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number()) return v.dump();
    return {};
}
inline bool jbool(const nlohmann::json& j, const char* key) {
    if (!j.is_object() || !j.contains(key)) return false;
    const auto& v = j[key];
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number()) return v.get<double>() != 0;
    if (v.is_string()) return v.get<std::string>() == "true" || v.get<std::string>() == "1";
    return false;
}

// Signed decimal string → micro-units, extra digits TRUNCATED (money fields such as portfolio_value). Throws on junk.
inline int64_t parse_money_micro(const std::string& s) {
    if (s.empty()) throw SpecError("empty amount");
    bool neg = s[0] == '-';
    std::string t = neg || s[0] == '+' ? s.substr(1) : s;
    size_t dot = t.find('.');
    std::string ip = t.substr(0, dot), fp = dot == std::string::npos ? "" : t.substr(dot + 1);
    if (fp.size() > 6) fp.resize(6);
    int64_t v = parse_decimal((ip.empty() ? "0" : ip) + (fp.empty() ? "" : "." + fp), 6);
    return neg ? -v : v;
}

// integer amount → decimal string with `decimals` places ("82", 3 → "0.082")
inline std::string format_decimal(int64_t v, int decimals) {
    bool neg = v < 0;
    uint64_t a = neg ? uint64_t(-(v + 1)) + 1 : uint64_t(v);
    std::string d = std::to_string(a);
    if (decimals > 0) {
        if (int(d.size()) <= decimals) d = std::string(size_t(decimals - int(d.size()) + 1), '0') + d;
        d.insert(d.size() - size_t(decimals), ".");
    }
    return neg ? "-" + d : d;
}

// ── Lighter order status (lighter_account_spec.md §3) ─────────────────────────────────────────────────────────
inline int order_status_code(const nlohmann::json& v) {
    if (v.is_number_integer()) return v.get<int>();
    if (!v.is_string()) return -1;
    static const std::map<std::string, int> m = {
        {"in-progress", 0}, {"pending", 1}, {"open", 2}, {"filled", 3}, {"canceled", 4}, {"canceled-post-only", 5},
        {"canceled-reduce-only", 6}, {"canceled-position-not-allowed", 7}, {"canceled-margin-not-allowed", 8},
        {"canceled-too-much-slippage", 9}, {"canceled-not-enough-liquidity", 10}, {"canceled-self-trade", 11},
        {"canceled-expired", 12}, {"canceled-oco", 13}, {"canceled-child", 14}, {"canceled-liquidation", 15},
        {"canceled-invalid-balance", 16}};
    auto it = m.find(v.get<std::string>());
    return it == m.end() ? -1 : it->second;
}
inline const char* order_status_name(int c) {
    static const char* n[] = {"in-progress", "pending", "open", "filled", "canceled", "canceled-post-only",
        "canceled-reduce-only", "canceled-position-not-allowed", "canceled-margin-not-allowed", "canceled-too-much-slippage",
        "canceled-not-enough-liquidity", "canceled-self-trade", "canceled-expired", "canceled-oco", "canceled-child",
        "canceled-liquidation", "canceled-invalid-balance"};
    return c >= 0 && c <= 16 ? n[c] : "unknown";
}

using Specs = std::map<int64_t, MarketSpec>;

struct AcctOrder {
    int64_t client_index = 0, order_index = 0, market = -1;
    bool is_ask = false;
    int status = -1;
    int64_t base = 0, remaining = 0, filled = 0, price = 0;   // integer units (market known)
    bool market_known = false;
};

struct AcctFill {
    int64_t trade_id = 0, market = -1, client_index = 0, order_index = 0, qty = 0, price = 0, time_ms = 0;
    bool is_ask = false;          // OUR side: true = we sold
    bool maker = true;
    bool self_trade = false;
    bool market_known = false;
    std::string type = "trade";   // trade | liquidation | deleverage | market-settlement
};

struct AcctPosition {
    int64_t market = -1;
    int64_t base = 0;             // signed (sign × position)
    std::string imf;              // initial_margin_fraction, percent ("20.00" = 5×)
    bool market_known = false;
    std::string raw;              // the position text as sent (lets an UNCONFIGURED market's position be seen as non-zero)
    bool nonzero() const {
        if (market_known) return base != 0;
        for (char c : raw) if (c >= '1' && c <= '9') return true;
        return false;
    }
};

struct AcctStats {
    int64_t collateral_micro = 0, portfolio_micro = 0;
    double margin_usage_pct = 0;
    bool ok = false;
};

struct AcctTx {
    std::string hash;
    int type = 0, status = -1, api_key = -1;
    int64_t nonce = -1, account = 0;
    std::string ae;               // AppError text ("" = none)
    int app_error = 0;            // its numeric code; -1 = non-empty but no code found
};

// leverage = 100 / IMF%. Returns 0 when unreadable.
inline double leverage_from_imf(const std::string& imf) {
    if (imf.empty()) return 0;
    char* end = nullptr;
    double v = std::strtod(imf.c_str(), &end);
    return v > 0 ? 100.0 / v : 0;
}

inline AcctOrder parse_order(const nlohmann::json& o, const Specs& specs) {
    AcctOrder r;
    r.client_index = jid(o, "client_order_index");
    r.order_index = jid(o, "order_index");
    r.market = jint(o, "market_index", -1);
    r.is_ask = jbool(o, "is_ask");
    r.status = o.contains("status") ? order_status_code(o["status"]) : -1;
    if (auto it = specs.find(r.market); it != specs.end()) {
        const MarketSpec& m = it->second;
        try {
            r.base = m.base_int(jstr(o, "initial_base_amount"));
            r.remaining = m.base_int(jstr(o, "remaining_base_amount"));
            std::string f = jstr(o, "filled_base_amount");
            r.filled = f.empty() ? r.base - r.remaining : m.base_int(f);
            r.price = m.price_int(jstr(o, "price"));
            r.market_known = true;
        } catch (const SpecError&) {
            r.market_known = false;
        }
    }
    return r;
}

// One trade → our fill, or nullopt when the account is on neither side.
inline std::optional<AcctFill> parse_fill(const nlohmann::json& t, int64_t me, const Specs& specs) {
    int64_t ask_acct = jint(t, "ask_account_id"), bid_acct = jint(t, "bid_account_id");
    bool ask = ask_acct == me, bid = bid_acct == me;
    if (!ask && !bid) return std::nullopt;
    AcctFill f;
    f.trade_id = jid(t, "trade_id");
    f.market = jint(t, "market_id", -1);
    f.self_trade = ask && bid;
    f.is_ask = ask;
    f.client_index = ask ? jid(t, "ask_client_id") : jid(t, "bid_client_id");
    f.order_index = ask ? jid(t, "ask_id") : jid(t, "bid_id");
    f.maker = jbool(t, "is_maker_ask") == f.is_ask;
    f.time_ms = jint(t, "timestamp");
    f.type = t.contains("type") ? jstr(t, "type") : "trade";
    if (auto it = specs.find(f.market); it != specs.end()) {
        try {
            f.qty = it->second.base_int(jstr(t, "size"));
            f.price = it->second.price_int(jstr(t, "price"));
            f.market_known = true;
        } catch (const SpecError&) {}
    }
    return f;
}

inline AcctPosition parse_position(const nlohmann::json& p, const Specs& specs) {
    AcctPosition r;
    r.market = jint(p, "market_id", -1);
    r.imf = jstr(p, "initial_margin_fraction");
    r.raw = jstr(p, "position");
    if (auto it = specs.find(r.market); it != specs.end()) {
        try {
            int64_t b = it->second.base_int(jstr(p, "position"));
            r.base = jint(p, "sign", 1) < 0 ? -b : b;
            r.market_known = true;
        } catch (const SpecError&) {}
    }
    return r;
}

inline AcctStats parse_stats(const nlohmann::json& s) {
    AcctStats r;
    try {
        r.collateral_micro = parse_money_micro(jstr(s, "collateral"));
        r.portfolio_micro = parse_money_micro(jstr(s, "portfolio_value"));
        std::string mu = jstr(s, "margin_usage");
        r.margin_usage_pct = mu.empty() ? 0 : std::strtod(mu.c_str(), nullptr);
        r.ok = true;
    } catch (const SpecError&) {}
    return r;
}

inline int app_error_code(const std::string& ae) {
    if (ae.empty()) return 0;
    for (size_t i = 0; i < ae.size(); ++i) {
        size_t j = i;
        while (j < ae.size() && ae[j] >= '0' && ae[j] <= '9') ++j;
        if (j - i >= 5) return std::atoi(ae.substr(i, j - i).c_str());
        if (j > i) i = j;
    }
    return -1;
}

inline AcctTx parse_tx(const nlohmann::json& t) {
    AcctTx r;
    r.hash = jstr(t, "hash");
    r.type = int(jint(t, "type"));
    r.status = int(jint(t, "status", -1));
    r.nonce = jint(t, "nonce", -1);
    r.api_key = int(jint(t, "api_key_index", -1));
    r.account = jint(t, "account_index");
    std::string ei = jstr(t, "event_info");
    if (!ei.empty()) {
        auto e = nlohmann::json::parse(ei, nullptr, false);
        if (e.is_object()) r.ae = jstr(e, "ae");
    }
    if (r.ae.empty() && t.contains("ae")) r.ae = jstr(t, "ae");
    r.app_error = app_error_code(r.ae);
    return r;
}

// ── the feed ──────────────────────────────────────────────────────────────────────────────────────────────────
class AccountFeed {
public:
    struct Sink {
        virtual ~Sink() = default;
        virtual void on_orders(const std::vector<AcctOrder>& orders, bool snapshot, int64_t now_ms) = 0;
        virtual void on_fills(const std::vector<AcctFill>& fills, bool snapshot, int64_t now_ms) = 0;
        virtual void on_positions(const std::vector<AcctPosition>& pos, bool snapshot, int64_t now_ms) = 0;
        virtual void on_stats(const AcctStats& s, int64_t now_ms) = 0;
        virtual void on_txs(const std::vector<AcctTx>& txs, int64_t now_ms) = 0;
        virtual void on_feed_note(const std::string& what, int64_t now_ms) = 0;   // logging / journal
    };
    using MintFn = std::function<std::string(int64_t deadline_s, int64_t now_s)>;

    static constexpr int64_t kPingEveryMs = 60'000;
    static constexpr int64_t kSilenceMs = 150'000;
    static constexpr int64_t kSnapshotTimeoutMs = 15'000;
    static constexpr int64_t kRenewEveryMs = 6LL * 3600'000;
    static constexpr int64_t kTokenLifeS = 8 * 3600 - 60;
    static constexpr int64_t kForceRecycleBeforeS = 300;

    AccountFeed(int64_t account, Specs specs, MintFn mint) : acct_(account), specs_(std::move(specs)), mint_(std::move(mint)) {}

    static std::vector<std::string> channel_names() {
        return {"account_all_orders", "account_all_trades", "account_all_positions", "user_stats", "account_tx"};
    }
    static bool needs_auth(const std::string& name) { return name == "account_all_orders" || name == "account_tx"; }

    void on_open(int64_t now_ms) {
        open_ = true; connected_ = false; subscribed_ = false; renewing_ = false;
        snaps_.clear(); renew_pending_.clear();
        want_reconnect_ = false; reason_.clear();
        last_frame_ms_ = now_ms; last_ping_ms_ = now_ms; opened_ms_ = now_ms;
    }
    void on_closed(int64_t) {
        open_ = false; connected_ = false; subscribed_ = false; renewing_ = false;
        snaps_.clear(); renew_pending_.clear();
    }
    bool renewing() const { return renewing_; }

    // Returns the frames to send in reply (subscribes, pong).
    std::vector<std::string> on_frame(const std::string& text, int64_t now_ms, Sink& sink) {
        std::vector<std::string> out;
        last_frame_ms_ = now_ms;
        auto j = nlohmann::json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object()) { ++bad_frames_; return out; }
        if (j.contains("error")) { on_error(j["error"], now_ms, sink); return out; }
        const std::string type = j.value("type", "");
        if (type == "connected") {
            connected_ = true;
            subscribe_all(now_ms, out);
            return out;
        }
        if (type == "ping") { out.push_back(R"({"type":"pong"})"); return out; }
        if (type == "pong") return out;
        if (type == "shutdown") { request_reconnect("server shutdown notice"); return out; }
        const std::string ch = j.value("channel", "");
        const size_t colon = ch.find(':');
        const std::string name = ch.substr(0, colon);
        if (colon == std::string::npos || std::strtoll(ch.c_str() + colon + 1, nullptr, 10) != acct_) { ++ignored_; return out; }
        const bool snapshot = type.rfind("subscribed/", 0) == 0;
        if (!snapshot && type.rfind("update/", 0) != 0) { ++ignored_; return out; }
        if (snapshot) {
            snaps_.insert(name);
            if (renewing_) { renew_pending_.erase(name); if (renew_pending_.empty()) renewing_ = false; }
        }
        dispatch(name, j, snapshot, now_ms, sink);
        return out;
    }

    // Timers: keepalive, silence, snapshot timeout, token renewal. Returns frames to send.
    std::vector<std::string> tick(int64_t now_ms) {
        std::vector<std::string> out;
        if (!open_) return out;
        if (now_ms - last_frame_ms_ >= kSilenceMs) { request_reconnect("no frame for 150 s"); return out; }
        if (now_ms - last_ping_ms_ >= kPingEveryMs) { out.push_back(R"({"type":"ping"})"); last_ping_ms_ = now_ms; }
        if (subscribed_ && !live() && now_ms - subscribed_ms_ >= kSnapshotTimeoutMs) {
            request_reconnect("snapshots missing 15 s after subscribe");
            return out;
        }
        if (renewing_ && now_ms - renew_ms_ >= kSnapshotTimeoutMs) {
            request_reconnect("token renewal: no fresh snapshot within 15 s");
            return out;
        }
        if (subscribed_ && !renewing_ && now_ms - token_minted_ms_ >= kRenewEveryMs) renew(now_ms, out);
        if (subscribed_ && token_deadline_s_ > 0 && now_ms / 1000 >= token_deadline_s_ - kForceRecycleBeforeS)
            request_reconnect("auth token near its deadline");
        return out;
    }

    bool live() const {
        if (!open_ || !subscribed_) return false;
        for (const auto& n : channel_names()) if (!snaps_.count(n)) return false;
        return true;
    }
    bool open() const { return open_; }
    bool want_reconnect() const { return want_reconnect_; }
    const std::string& reconnect_reason() const { return reason_; }
    void clear_reconnect() { want_reconnect_ = false; reason_.clear(); }
    int64_t token_deadline_s() const { return token_deadline_s_; }
    int64_t token_minted_ms() const { return token_minted_ms_; }
    const std::string& token() const { return token_; }
    int64_t renewals() const { return renewals_; }
    int64_t bad_frames() const { return bad_frames_; }
    // Current auth token for REST (minted here so REST and WS share one 6-hourly renewal). Mints when none/old.
    const std::string& rest_token(int64_t now_ms) {
        if (token_.empty() || now_ms - token_minted_ms_ >= kRenewEveryMs) mint(now_ms);
        return token_;
    }

private:
    void mint(int64_t now_ms) {
        int64_t now_s = now_ms / 1000;
        int64_t deadline = now_s + kTokenLifeS;
        token_ = mint_(deadline, now_s);
        token_deadline_s_ = deadline;
        token_minted_ms_ = now_ms;
    }
    std::string sub_frame(const std::string& name, bool with_auth) const {
        nlohmann::json s = {{"type", "subscribe"}, {"channel", name + "/" + std::to_string(acct_)}};
        if (with_auth) s["auth"] = token_;
        return s.dump();
    }
    void subscribe_all(int64_t now_ms, std::vector<std::string>& out) {
        if (token_.empty() || now_ms - token_minted_ms_ >= kRenewEveryMs ||
            now_ms / 1000 >= token_deadline_s_ - kForceRecycleBeforeS) mint(now_ms);
        for (const auto& n : channel_names()) out.push_back(sub_frame(n, needs_auth(n)));
        subscribed_ = true;
        subscribed_ms_ = now_ms;
    }
    void renew(int64_t now_ms, std::vector<std::string>& out) {
        mint(now_ms);
        for (const auto& n : channel_names()) {
            if (!needs_auth(n)) continue;
            out.push_back(nlohmann::json({{"type", "unsubscribe"}, {"channel", n + "/" + std::to_string(acct_)}}).dump());
            out.push_back(sub_frame(n, true));
            renew_pending_.insert(n);
        }
        renewing_ = true;
        renew_ms_ = now_ms;
        ++renewals_;
    }
    void request_reconnect(const std::string& why) {
        if (!want_reconnect_) { want_reconnect_ = true; reason_ = why; }
    }
    void on_error(const nlohmann::json& e, int64_t now_ms, Sink& sink) {
        int code = int(jint(e, "code"));
        std::string msg = jstr(e, "message");
        sink.on_feed_note("account ws error " + std::to_string(code) + ": " + msg, now_ms);
        if (code == 30003) return;   // already subscribed
        if (code == 20001 || code == 20013) { token_.clear(); request_reconnect("auth error " + std::to_string(code)); return; }
        request_reconnect("ws error " + std::to_string(code) + ": " + msg);
    }

    void dispatch(const std::string& name, const nlohmann::json& j, bool snapshot, int64_t now_ms, Sink& sink) {
        if (name == "account_all_orders") {
            std::vector<AcctOrder> v;
            if (j.contains("orders") && j["orders"].is_object())
                for (auto& [mk, arr] : j["orders"].items())
                    if (arr.is_array()) for (const auto& o : arr) v.push_back(parse_order(o, specs_));
            sink.on_orders(v, snapshot, now_ms);
        } else if (name == "account_all_trades") {
            std::vector<AcctFill> v;
            if (j.contains("trades") && j["trades"].is_object())
                for (auto& [mk, arr] : j["trades"].items())
                    if (arr.is_array()) for (const auto& t : arr) if (auto f = parse_fill(t, acct_, specs_)) v.push_back(*f);
            sink.on_fills(v, snapshot, now_ms);
        } else if (name == "account_all_positions") {
            std::vector<AcctPosition> v;
            if (j.contains("positions") && j["positions"].is_object())
                for (auto& [mk, p] : j["positions"].items()) if (p.is_object()) v.push_back(parse_position(p, specs_));
            sink.on_positions(v, snapshot, now_ms);
        } else if (name == "user_stats") {
            if (j.contains("stats")) sink.on_stats(parse_stats(j["stats"]), now_ms);
        } else if (name == "account_tx") {
            std::vector<AcctTx> v;
            if (j.contains("txs") && j["txs"].is_array()) for (const auto& t : j["txs"]) v.push_back(parse_tx(t));
            sink.on_txs(v, now_ms);
        } else {
            ++ignored_;
        }
    }

    int64_t acct_;
    Specs specs_;
    MintFn mint_;
    bool open_ = false, connected_ = false, subscribed_ = false, renewing_ = false;
    std::set<std::string> snaps_, renew_pending_;
    bool want_reconnect_ = false;
    std::string reason_;
    std::string token_;
    int64_t token_deadline_s_ = 0, token_minted_ms_ = 0;
    int64_t last_frame_ms_ = 0, last_ping_ms_ = 0, opened_ms_ = 0, subscribed_ms_ = 0, renew_ms_ = 0;
    int64_t renewals_ = 0, bad_frames_ = 0, ignored_ = 0;
};

}  // namespace lighter::live
