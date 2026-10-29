#pragma once
/*  fake_venue.hpp — the in-process Lighter the offline soak and the tests run the engine against. NO SOCKETS.

    FakeVenue wraps fake::FakeExchange and speaks Lighter's wire formats so the engine's real code paths run:
      * HttpClient: POST /api/v1/sendTx and /sendTxBatch (form-encoded, parsed exactly as the server would), GET
        nextNonce, account, accountActiveOrders / accountInactiveOrders / trades (these three require an
        Authorization token minted by the real signer: "deadline:account:key:sig", deadline in the future),
        orderBookDetails.
      * FakeAccountWs (WsConn): {"type":"connected"} on open; each subscribe answered with a "subscribed/<name>"
        snapshot built from the exchange state; afterwards "update/<name>" frames converted from the exchange's events
        (acks, cancels, fills → orders + trades + positions + user_stats) and account_tx results from its tx log.
        Auth channels reject a missing/expired token with error 20013.
      * QueueWs (WsConn): a plain in-memory socket — the offline soak pushes the recorded public archive into it.
      * on_public(): the archive's books set the exchange's best bid/ask; its trades hit our resting orders on the
        far side of the trade price (size-capped) — an optimistic fill model: the soak checks invariants, not P&L.
    Fault knobs (tests): drop_next_sends (timeout with/without applying), fail_next_rest, drop_account_ws,
    hold_account_events, suppress_tx_stream, imf (leverage), collateral.                                         */

#include "dex/lighter_feed.hpp"
#include "dex/live/account_feed.hpp"
#include "dex/live/fake_exchange.hpp"
#include "dex/live/market_spec.hpp"
#include "dex/live/transport.hpp"

#include <nlohmann/json.hpp>

#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace lighter::live::fake {

class QueueWs : public WsConn {
public:
    bool open() override {
        if (on_open) on_open();          // tests: e.g. advance the clock to model a slow TLS handshake
        if (refuse_opens > 0) { --refuse_opens; return false; }
        open_ = true;
        ++opens;
        return true;
    }
    bool send(const std::string& text) override { if (!open_) return false; sent.push_back(text); return true; }
    std::optional<std::string> poll() override {
        if (!open_ || in_.empty()) return std::nullopt;
        std::string m = std::move(in_.front());
        in_.pop_front();
        return m;
    }
    bool is_open() const override { return open_; }
    void close() override { open_ = false; in_.clear(); }
    void push(std::string m) { if (open_) in_.push_back(std::move(m)); }
    size_t pending() const { return in_.size(); }
    std::vector<std::string> sent;
    int opens = 0, refuse_opens = 0;
    std::function<void()> on_open;

private:
    bool open_ = false;
    std::deque<std::string> in_;
};

class FakeVenue : public HttpClient {
public:
    FakeVenue(FakeExchange& ex, MarketSpec spec, int api_key, std::function<int64_t()> now)
        : ex_(ex), spec_(std::move(spec)), key_(api_key), now_(std::move(now)) {}

    // ── knobs ─────────────────────────────────────────────────────────────────────────────────────────────────
    int64_t collateral_micro = 20 * kMicro;     // starting cash
    std::string imf = "20.00";                  // the market's initial margin fraction (%): 20 = 5×
    bool imf_present = true;
    int drop_next_sends = 0;                    // next N send requests get no answer …
    bool drop_applies = true;                   // … after being applied (true) or before reaching the exchange
    int fail_next_rest = 0;                     // next N GETs get no answer
    std::vector<int> rest_timeouts;             // the timeout each GET was sent with (in order)
    bool hold_account_events = false;           // queue stream events instead of delivering them
    bool suppress_tx_stream = false;            // never emit account_tx updates
    bool suppress_stats = false;                // never emit user_stats updates (equity then only via REST)
    int drop_next_stream_fills = 0;             // the stream silently loses the next N fills (trade + position + order frames)
    int64_t counterparty = 999;
    std::string symbol_override;                // orderBookDetails symbol (tests: metadata mismatch)
    size_t snapshot_trades = 50;                // fills in the account_all_trades snapshot (Lighter sends ~50)
    bool phantom_open_order = false;            // accountActiveOrders always shows one open order no cancel removes
    int rest_calls = 0, send_calls = 0;

    const MarketSpec& spec() const { return spec_; }
    FakeExchange& exchange() { return ex_; }

    // ── HTTP ──────────────────────────────────────────────────────────────────────────────────────────────────
    HttpResponse request(const HttpRequest& r) override {
        ex_.set_now(now_());
        std::string path = r.target.substr(0, r.target.find('?'));
        auto q = form_decode(r.target.find('?') == std::string::npos ? "" : r.target.substr(r.target.find('?') + 1));
        if (r.method == "POST" && (path == "/api/v1/sendTx" || path == "/api/v1/sendTxBatch")) return send(path, r.body);
        ++rest_calls;
        rest_timeouts.push_back(r.timeout_ms);
        if (fail_next_rest > 0) { --fail_next_rest; return {0, true, "", "injected REST failure"}; }
        if (path == "/api/v1/nextNonce") return ok({{"code", 200}, {"nonce", ex_.next_nonce(std::stoi(form_get(q, "api_key_index")))}});
        if (path == "/api/v1/orderBookDetails") {
            nlohmann::json d = {{"symbol", symbol_override.empty() ? spec_.symbol : symbol_override}, {"market_id", spec_.market_index},
                                {"size_decimals", spec_.size_decimals}, {"price_decimals", spec_.price_decimals},
                                {"min_base_amount", format_decimal(spec_.min_base_amount, spec_.size_decimals)},
                                {"min_quote_amount", format_decimal(spec_.min_quote_micro, 6)}};
            return ok({{"code", 200}, {"order_book_details", nlohmann::json::array({d})}});
        }
        if (path == "/api/v1/account") {
            nlohmann::json pos = nlohmann::json::array();
            pos.push_back(position_json(spec_.market_index, ex_.position(spec_.market_index)));
            for (auto& [m, p] : ex_.positions()) if (m != spec_.market_index && p != 0) pos.push_back(position_json(m, p));
            int64_t pf = portfolio_micro();
            nlohmann::json a = {{"index", ex_.account_index()}, {"l1_address", "0xfake"}, {"collateral", format_decimal(pf, 6)},
                                {"total_asset_value", format_decimal(pf, 6)}, {"positions", pos}};
            return ok({{"code", 200}, {"total", 1}, {"accounts", nlohmann::json::array({a})}});
        }
        if (path == "/api/v1/accountActiveOrders" || path == "/api/v1/accountInactiveOrders" || path == "/api/v1/trades") {
            if (!auth_ok(r)) return {401, true, R"({"code":20013,"message":"invalid auth"})", ""};
            if (path == "/api/v1/trades") return trades_page(q);
            nlohmann::json arr = nlohmann::json::array();
            int64_t market = form_get(q, "market_id").empty() ? 255 : std::stoll(form_get(q, "market_id"));
            if (path == "/api/v1/accountActiveOrders") {
                for (const auto& o : ex_.active_orders()) if (market == 255 || o.market == market) arr.push_back(order_json(o));
                if (phantom_open_order) arr.push_back(order_json(OrderView{0, (int64_t{1} << 48) + 999999, spec_.market_index, 82, 0, 1, false, 2, 1, false, 0}));
            } else {
                size_t lim = form_get(q, "limit").empty() ? 100 : size_t(std::stoul(form_get(q, "limit")));
                for (const auto& o : ex_.inactive_orders(lim)) if (market == 255 || o.market == market) arr.push_back(order_json(o));
            }
            return ok({{"code", 200}, {"orders", arr}, {"next_cursor", ""}});
        }
        return {404, true, R"({"code":404,"message":"not found"})", ""};
    }

    // ── account WS (driven by FakeAccountWs) ─────────────────────────────────────────────────────────────────
    void ws_open() {
        ws_open_ = true;
        subs_.clear();
        out_.clear();
        held_.clear();
        ex_.drain();                        // anything before this connection is in the snapshots
        tx_cursor_ = ex_.tx_log().size();
        out_.push_back(R"({"session_id":"fake","type":"connected"})");
    }
    void ws_close() { ws_open_ = false; subs_.clear(); out_.clear(); held_.clear(); }
    bool ws_is_open() const { return ws_open_; }
    void ws_client_frame(const std::string& text) {
        if (!ws_open_) return;
        auto j = nlohmann::json::parse(text, nullptr, false);
        if (!j.is_object()) return;
        std::string type = j.value("type", "");
        if (type == "ping") { out_.push_back(R"({"type":"pong"})"); return; }
        std::string ch = j.value("channel", "");
        std::string name = ch.substr(0, ch.find('/'));
        if (type == "unsubscribe") { subs_.erase(name); return; }
        if (type != "subscribe") return;
        ++subscribes;
        if ((name == "account_all_orders" || name == "account_tx") && !token_ok(j.value("auth", ""))) {
            out_.push_back(nlohmann::json({{"error", {{"code", 20013}, {"message", "invalid auth: " + name + ":" + std::to_string(ex_.account_index())}}}}).dump());
            return;
        }
        subs_.insert(name);
        out_.push_back(snapshot(name).dump());
    }
    std::optional<std::string> ws_next() {
        if (!ws_open_) return std::nullopt;
        pump_events();
        if (out_.empty()) return std::nullopt;
        std::string m = std::move(out_.front());
        out_.pop_front();
        return m;
    }
    // Deliver what hold_account_events kept back.
    void release_held() { for (auto& m : held_) out_.push_back(std::move(m)); held_.clear(); }
    // The account stream drops: whatever was queued is lost (as on a real cut connection).
    void drop_account_ws() { ws_close(); ++drops; }
    // Push a raw frame into the account stream (tests: shutdown notices, error frames, foreign orders).
    void inject_account_frame(std::string f) { if (ws_open_) out_.push_back(std::move(f)); }
    int subscribes = 0, drops = 0;

    // ── public archive → exchange book and our fills ─────────────────────────────────────────────────────────
    void on_public(const nlohmann::json& m) {
        if (lighter::market_of(m, "order_book") == spec_.market_index) {
            auto r = book_.apply(m);
            if ((r == lighter::Book::Apply::Snapshot || r == lighter::Book::Apply::Delta) && book_.valid()) {
                hl::Book b = book_.top(1);
                bb_ = std::llround(b.bids[0].px * double(pow10i(spec_.price_decimals)));
                ba_ = std::llround(b.asks[0].px * double(pow10i(spec_.price_decimals)));
                mid_ = (bb_ + ba_) / 2;
                ex_.set_book(spec_.market_index, bb_, ba_);
            }
        } else if (lighter::market_of(m, "trade") == spec_.market_index) {
            for (const auto& t : lighter::parse_trades(m)) {
                int64_t px = std::llround(t.px * double(pow10i(spec_.price_decimals)));
                int64_t sz = std::llround(t.sz * double(pow10i(spec_.size_decimals)));
                if (sz <= 0) continue;
                for (const auto& o : ex_.resting(spec_.market_index)) {
                    bool hit = t.buy_aggr ? (o.is_ask && o.price <= px) : (!o.is_ask && o.price >= px);
                    if (!hit || sz <= 0) continue;
                    int64_t q = std::min(sz, o.base - o.filled);
                    if (o.client_index > 0 && ex_.hit(o.client_index, q)) { sz -= q; ++public_hits; }
                }
            }
        }
    }
    void set_book(int64_t bid, int64_t ask) { bb_ = bid; ba_ = ask; mid_ = (bid + ask) / 2; ex_.set_book(spec_.market_index, bid, ask); }
    int64_t mid() const { return mid_; }
    int public_hits = 0;

    // Equity: cash + position × mid (micro-USD).
    int64_t portfolio_micro() const {
        int64_t cash = collateral_micro;
        for (const auto& t : ex_.trades()) {
            int64_t n = spec_.notional_micro_floor(t.qty, t.price);
            cash += t.is_ask ? n : -n;
        }
        int64_t pos = ex_.position(spec_.market_index);
        int64_t pv = pos >= 0 ? spec_.notional_micro_floor(pos, mid_) : -spec_.notional_micro_floor(-pos, mid_);
        return cash + pv;
    }

private:
    HttpResponse ok(const nlohmann::json& j) const { return {200, true, j.dump(), ""}; }

    HttpResponse send(const std::string& path, const std::string& body) {
        ++send_calls;
        auto f = form_decode(body);
        std::vector<std::pair<uint8_t, std::string>> txs;
        if (path == "/api/v1/sendTx") {
            txs.push_back({uint8_t(std::stoi(form_get(f, "tx_type"))), form_get(f, "tx_info")});
        } else {
            auto types = nlohmann::json::parse(form_get(f, "tx_types"), nullptr, false);
            auto infos = nlohmann::json::parse(form_get(f, "tx_infos"), nullptr, false);
            if (!types.is_array() || !infos.is_array() || types.size() != infos.size()) return {400, true, R"({"code":21000,"message":"bad batch"})", ""};
            for (size_t i = 0; i < types.size(); ++i) {
                if (!infos[i].is_string()) return {400, true, R"({"code":21000,"message":"tx_infos must be strings"})", ""};
                txs.push_back({uint8_t(types[i].get<int>()), infos[i].get<std::string>()});
            }
        }
        if (drop_next_sends > 0) {
            --drop_next_sends;
            if (drop_applies) ex_.send_batch(txs);
            return {0, true, "", "injected timeout"};
        }
        SendResult res = ex_.send_batch(txs);
        nlohmann::json b = {{"code", res.code}, {"message", res.message}};
        if (res.code == 200) {
            if (txs.size() == 1) b["tx_hash"] = "fakehash";
            else b["tx_hash"] = nlohmann::json::array();
        }
        return {res.http, true, b.dump(), ""};
    }

    bool token_ok(const std::string& tok) const {
        // "deadline:account:key:sig" — the real signer's token; deadline must be in the future, ≤ 8 h ahead.
        size_t a = tok.find(':'), b = a == std::string::npos ? a : tok.find(':', a + 1), c = b == std::string::npos ? b : tok.find(':', b + 1);
        if (c == std::string::npos || c + 1 >= tok.size()) return false;
        int64_t deadline = std::strtoll(tok.substr(0, a).c_str(), nullptr, 10);
        int64_t acct = std::strtoll(tok.substr(a + 1, b - a - 1).c_str(), nullptr, 10);
        int key = std::atoi(tok.substr(b + 1, c - b - 1).c_str());
        int64_t now_s = now_() / 1000;
        return acct == ex_.account_index() && key == key_ && deadline > now_s && deadline <= now_s + 8 * 3600;
    }
    bool auth_ok(const HttpRequest& r) const {
        for (const auto& [k, v] : r.headers) if (k == "Authorization") return token_ok(v);
        return false;
    }

    nlohmann::json position_json(int64_t market, int64_t pos) const {
        nlohmann::json p = {{"market_id", market}, {"symbol", market == spec_.market_index ? spec_.symbol : "OTHER"},
                            {"position", format_decimal(std::llabs(pos), market == spec_.market_index ? spec_.size_decimals : 3)},
                            {"sign", pos < 0 ? -1 : 1}, {"open_order_count", 0}};
        if (market == spec_.market_index && imf_present) p["initial_margin_fraction"] = imf;
        return p;
    }
    nlohmann::json order_json(const OrderView& o) const {
        return {{"order_index", o.order_index}, {"order_index_str", std::to_string(o.order_index)},
                {"client_order_index", o.client_index}, {"client_order_index_str", std::to_string(o.client_index)},
                {"market_index", o.market}, {"owner_account_index", ex_.account_index()}, {"is_ask", o.is_ask},
                {"status", order_status_name(o.status)},
                {"initial_base_amount", format_decimal(o.base, spec_.size_decimals)},
                {"remaining_base_amount", format_decimal(o.base - o.filled, spec_.size_decimals)},
                {"filled_base_amount", format_decimal(o.filled, spec_.size_decimals)},
                {"price", format_decimal(o.price, spec_.price_decimals)},
                {"time_in_force", o.tif == 2 ? "post-only" : o.tif == 0 ? "immediate-or-cancel" : "good-till-time"},
                {"reduce_only", o.reduce_only}};
    }
    nlohmann::json trade_json(const TradeRecord& t) const {
        bool we_ask = t.is_ask;
        int64_t me = ex_.account_index();
        return {{"trade_id", t.trade_id}, {"trade_id_str", std::to_string(t.trade_id)}, {"type", "trade"},
                {"market_id", t.market}, {"size", format_decimal(t.qty, spec_.size_decimals)},
                {"price", format_decimal(t.price, spec_.price_decimals)},
                {"ask_id", we_ask ? t.order_index : 0}, {"bid_id", we_ask ? 0 : t.order_index},
                {"ask_client_id", we_ask ? t.client_index : 0}, {"ask_client_id_str", std::to_string(we_ask ? t.client_index : 0)},
                {"bid_client_id", we_ask ? 0 : t.client_index}, {"bid_client_id_str", std::to_string(we_ask ? 0 : t.client_index)},
                {"ask_account_id", we_ask ? me : counterparty}, {"bid_account_id", we_ask ? counterparty : me},
                {"is_maker_ask", t.maker ? we_ask : !we_ask}, {"timestamp", t.time_ms}};
    }
    nlohmann::json stats_json() const {
        int64_t pf = portfolio_micro();
        return {{"collateral", format_decimal(pf, 6)}, {"portfolio_value", format_decimal(pf, 6)},
                {"leverage", "0.00"}, {"available_balance", format_decimal(pf, 6)}, {"margin_usage", "0.00"}};
    }
    std::string chan(const std::string& name) const { return name + ":" + std::to_string(ex_.account_index()); }

    nlohmann::json snapshot(const std::string& name) const {
        nlohmann::json j = {{"channel", chan(name)}, {"type", "subscribed/" + name}};
        if (name == "account_all_orders") {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& o : ex_.active_orders()) arr.push_back(order_json(o));
            j["orders"] = {{std::to_string(spec_.market_index), arr}};
        } else if (name == "account_all_trades") {
            nlohmann::json arr = nlohmann::json::array();
            const auto& tr = ex_.trades();
            for (size_t i = tr.size() > snapshot_trades ? tr.size() - snapshot_trades : 0; i < tr.size(); ++i) arr.push_back(trade_json(tr[i]));
            j["trades"] = {{std::to_string(spec_.market_index), arr}};
        } else if (name == "account_all_positions") {
            j["positions"] = {{std::to_string(spec_.market_index), position_json(spec_.market_index, ex_.position(spec_.market_index))}};
        } else if (name == "user_stats") {
            j["stats"] = stats_json();
            j["timestamp"] = now_();
        } else if (name == "account_tx") {
            j["txs"] = nlohmann::json::array();
        }
        return j;
    }

    void emit(const std::string& name, nlohmann::json j) {
        if (!subs_.count(name)) return;
        j["channel"] = chan(name);
        j["type"] = "update/" + name;
        (hold_account_events ? held_ : out_).push_back(j.dump());
    }

    void pump_events() {
        for (const auto& e : ex_.drain()) {
            std::string mk = std::to_string(e.market);
            switch (e.type) {
                case FakeEvent::Ack:
                case FakeEvent::Cancelled:
                case FakeEvent::Modified:
                    if (auto o = ex_.find_view(e.order_index ? e.order_index : e.client_index)) {
                        OrderView v = *o;
                        if (e.type == FakeEvent::Cancelled) v.status = e.status;   // the status this event reported
                        emit("account_all_orders", {{"orders", {{mk, nlohmann::json::array({order_json(v)})}}}});
                    }
                    break;
                case FakeEvent::Fill: {
                    if (drop_next_stream_fills > 0) { --drop_next_stream_fills; break; }
                    for (const auto& t : ex_.trades())
                        if (t.trade_id == e.trade_id) { emit("account_all_trades", {{"trades", {{mk, nlohmann::json::array({trade_json(t)})}}}}); break; }
                    emit("account_all_positions", {{"positions", {{mk, position_json(e.market, ex_.position(e.market))}}}});
                    if (auto o = ex_.find_view(e.order_index)) emit("account_all_orders", {{"orders", {{mk, nlohmann::json::array({order_json(*o)})}}}});
                    if (!suppress_stats) emit("user_stats", {{"stats", stats_json()}, {"timestamp", now_()}});
                    break;
                }
                case FakeEvent::TxFailed: break;   // reported through account_tx
            }
        }
        const auto& log = ex_.tx_log();
        for (; tx_cursor_ < log.size(); ++tx_cursor_) {
            if (suppress_tx_stream) continue;
            const TxRecord& t = log[tx_cursor_];
            nlohmann::json ei = {{"ae", t.app_error ? std::to_string(t.app_error) + ": fake app error" : ""}};
            nlohmann::json tx = {{"hash", "fake" + std::to_string(t.nonce)}, {"type", t.type}, {"status", t.status}, {"nonce", t.nonce},
                                 {"api_key_index", t.api_key}, {"account_index", ex_.account_index()}, {"event_info", ei.dump()}};
            emit("account_tx", {{"txs", nlohmann::json::array({tx})}});
        }
        if (!suppress_stats && now_() - last_stats_ms_ >= 5'000) { last_stats_ms_ = now_(); emit("user_stats", {{"stats", stats_json()}, {"timestamp", now_()}}); }
    }

    HttpResponse trades_page(const std::vector<std::pair<std::string, std::string>>& q) {
        const auto& tr = ex_.trades();
        size_t limit = form_get(q, "limit").empty() ? 100 : size_t(std::stoul(form_get(q, "limit")));
        size_t start = form_get(q, "cursor").empty() ? 0 : size_t(std::stoul(form_get(q, "cursor")));
        nlohmann::json arr = nlohmann::json::array();
        size_t i = start;
        for (; i < tr.size() && arr.size() < limit; ++i) arr.push_back(trade_json(tr[tr.size() - 1 - i]));   // newest first
        return ok({{"code", 200}, {"trades", arr}, {"next_cursor", i < tr.size() ? std::to_string(i) : ""}});
    }

    FakeExchange& ex_;
    MarketSpec spec_;
    int key_;
    std::function<int64_t()> now_;
    bool ws_open_ = false;
    std::set<std::string> subs_;
    std::deque<std::string> out_;
    std::deque<std::string> held_;
    size_t tx_cursor_ = 0;
    int64_t last_stats_ms_ = 0;
    lighter::Book book_;
    int64_t bb_ = 0, ba_ = 0, mid_ = 0;
};

class FakeAccountWs : public WsConn {
public:
    explicit FakeAccountWs(FakeVenue& v) : v_(v) {}
    bool open() override { if (refuse_opens > 0) { --refuse_opens; return false; } v_.ws_open(); ++opens; return true; }
    bool send(const std::string& text) override { if (!v_.ws_is_open()) return false; sent.push_back(text); v_.ws_client_frame(text); return true; }
    std::optional<std::string> poll() override { return v_.ws_next(); }
    bool is_open() const override { return v_.ws_is_open(); }
    void close() override { v_.ws_close(); }
    std::vector<std::string> sent;
    int opens = 0, refuse_opens = 0;

private:
    FakeVenue& v_;
};

}  // namespace lighter::live::fake
