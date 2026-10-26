#pragma once
/*  rest_api.hpp — the read-only REST calls the live engine needs (lighter_account_spec.md §5), typed.

      nextNonce               GET /api/v1/nextNonce?account_index=&api_key_index=            → {code, nonce}
      account                 GET /api/v1/account?by=index&value=                             → accounts[0]: collateral,
                                                                                                 positions[] (+ IMF), l1_address
      accountActiveOrders     GET /api/v1/accountActiveOrders?account_index=&market_id=  AUTH → orders[] (open)
      accountInactiveOrders   GET /api/v1/accountInactiveOrders?account_index=&market_id=&limit=  AUTH → orders[]
      trades                  GET /api/v1/trades?account_index=&sort_by=trade_id&sort_dir=desc&limit=&cursor=  AUTH
      orderBookDetails        GET /api/v1/orderBookDetails?market_id=                         → decimals + minimums

    Every call goes through a Gate first (the shared rate budget: Standard counts REST and sendTx together, 60/min)
    and reports HTTP 429/405 back to it. A call the gate refuses, or that fails, returns nullopt — the caller retries
    on its own schedule; nothing here loops.                                                                   */

#include "dex/live/account_feed.hpp"
#include "dex/live/transport.hpp"

#include <nlohmann/json.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace lighter::live {

struct RestGate {
    std::function<bool()> allow;          // budget left for one (safety-class) request?
    std::function<void()> record;         // one request spent
    std::function<void()> rate_limited;   // the server answered 429/405
};

struct AccountInfo {
    int64_t collateral_micro = 0;
    int64_t portfolio_micro = 0;          // collateral + unrealised, if the endpoint reports it (else = collateral)
    std::string l1_address;
    std::vector<AcctPosition> positions;
};

class RestApi {
public:
    using TokenFn = std::function<std::string()>;
    RestApi(HttpClient& http, int64_t account, Specs specs, TokenFn token, RestGate gate, int timeout_ms = 8'000)
        : http_(http), acct_(account), specs_(std::move(specs)), token_(std::move(token)), gate_(std::move(gate)),
          timeout_ms_(timeout_ms) {}

    std::optional<int64_t> next_nonce(int api_key) {
        auto j = get("/api/v1/nextNonce?account_index=" + std::to_string(acct_) + "&api_key_index=" + std::to_string(api_key), false);
        if (!j || !j->contains("nonce")) return std::nullopt;
        return jint(*j, "nonce", -1) >= 0 ? std::optional<int64_t>(jint(*j, "nonce")) : std::nullopt;
    }

    std::optional<AccountInfo> account() {
        auto j = get("/api/v1/account?by=index&value=" + std::to_string(acct_), false);
        if (!j || !j->contains("accounts") || !(*j)["accounts"].is_array()) return std::nullopt;
        for (const auto& a : (*j)["accounts"]) {
            if (jint(a, "index", jint(a, "account_index")) != acct_) continue;
            AccountInfo r;
            try {
                r.collateral_micro = parse_money_micro(jstr(a, "collateral"));
                std::string tav = jstr(a, "total_asset_value");
                r.portfolio_micro = tav.empty() ? r.collateral_micro : parse_money_micro(tav);
            } catch (const SpecError&) { return std::nullopt; }
            r.l1_address = jstr(a, "l1_address");
            if (a.contains("positions") && a["positions"].is_array())
                for (const auto& p : a["positions"]) r.positions.push_back(parse_position(p, specs_));
            return r;
        }
        return std::nullopt;
    }

    std::optional<std::vector<AcctOrder>> active_orders(int64_t market) {
        std::vector<AcctOrder> out;
        std::string cursor;
        for (int page = 0; page < 5; ++page) {
            std::string t = "/api/v1/accountActiveOrders?account_index=" + std::to_string(acct_) + "&market_id=" + std::to_string(market);
            if (!cursor.empty()) t += "&cursor=" + url_encode(cursor);
            auto j = get(t, true);
            if (!j || !j->contains("orders") || !(*j)["orders"].is_array()) return std::nullopt;
            for (const auto& o : (*j)["orders"]) out.push_back(parse_order(o, specs_));
            cursor = jstr(*j, "next_cursor");
            if (cursor.empty() || (*j)["orders"].empty()) return out;
        }
        return out;
    }

    std::optional<std::vector<AcctOrder>> inactive_orders(int64_t market, int limit) {
        auto j = get("/api/v1/accountInactiveOrders?account_index=" + std::to_string(acct_) + "&market_id=" +
                     std::to_string(market) + "&limit=" + std::to_string(limit), true);
        if (!j || !j->contains("orders") || !(*j)["orders"].is_array()) return std::nullopt;
        std::vector<AcctOrder> out;
        for (const auto& o : (*j)["orders"]) out.push_back(parse_order(o, specs_));
        return out;
    }

    // Our fills with trade_id > after (newest first from the server; returned oldest first). Walks the cursor until
    // it reaches `after` (or max_pages). `complete` is false when the walk stopped before reaching it.
    struct TradesPage { std::vector<AcctFill> fills; bool complete = true; int64_t newest_id = 0; };
    std::optional<TradesPage> trades_since(int64_t after, int page_size = 100, int max_pages = 5) {
        TradesPage r;
        std::string cursor;
        for (int page = 0; page < max_pages; ++page) {
            std::string t = "/api/v1/trades?account_index=" + std::to_string(acct_) + "&sort_by=trade_id&sort_dir=desc&limit=" +
                            std::to_string(page_size);
            if (!cursor.empty()) t += "&cursor=" + url_encode(cursor);
            auto j = get(t, true);
            if (!j || !j->contains("trades") || !(*j)["trades"].is_array()) return std::nullopt;
            bool reached = false;
            for (const auto& tr : (*j)["trades"]) {
                int64_t id = jid(tr, "trade_id");
                r.newest_id = std::max(r.newest_id, id);
                if (id <= after) { reached = true; continue; }
                if (auto f = parse_fill(tr, acct_, specs_)) r.fills.push_back(*f);
            }
            cursor = jstr(*j, "next_cursor");
            if (reached || cursor.empty() || (*j)["trades"].empty()) {
                std::sort(r.fills.begin(), r.fills.end(), [](const AcctFill& a, const AcctFill& b) { return a.trade_id < b.trade_id; });
                return r;
            }
        }
        r.complete = false;
        std::sort(r.fills.begin(), r.fills.end(), [](const AcctFill& a, const AcctFill& b) { return a.trade_id < b.trade_id; });
        return r;
    }

    std::optional<MarketSpec> order_book_details(int64_t market) {
        auto j = get("/api/v1/orderBookDetails?market_id=" + std::to_string(market), false);
        if (!j || !j->contains("order_book_details") || !(*j)["order_book_details"].is_array()) return std::nullopt;
        for (const auto& d : (*j)["order_book_details"]) {
            if (jint(d, "market_id", -1) != market) continue;
            try {
                return MarketSpec::from_strings(jstr(d, "symbol"), int(market), int(jint(d, "size_decimals", jint(d, "supported_size_decimals"))),
                                                int(jint(d, "price_decimals", jint(d, "supported_price_decimals"))),
                                                jstr(d, "min_base_amount"), jstr(d, "min_quote_amount"));
            } catch (const SpecError&) { return std::nullopt; }
        }
        return std::nullopt;
    }

    int last_status() const { return last_status_; }
    const std::string& last_error() const { return last_error_; }
    int64_t calls() const { return calls_; }

private:
    std::optional<nlohmann::json> get(const std::string& target, bool auth) {
        if (gate_.allow && !gate_.allow()) { last_error_ = "rate budget"; last_status_ = 0; return std::nullopt; }
        if (gate_.record) gate_.record();
        ++calls_;
        HttpRequest r;
        r.method = "GET";
        r.target = target;
        r.timeout_ms = timeout_ms_;
        if (auth) r.headers.push_back({"Authorization", token_()});
        HttpResponse resp = http_.request(r);
        last_status_ = resp.status;
        last_error_ = resp.error;
        if (resp.status == 429 || resp.status == 405) { if (gate_.rate_limited) gate_.rate_limited(); return std::nullopt; }
        if (resp.status != 200) return std::nullopt;
        auto j = nlohmann::json::parse(resp.body, nullptr, false);
        if (j.is_discarded() || !j.is_object()) { last_error_ = "unparseable body"; return std::nullopt; }
        if (j.contains("code") && j["code"].is_number_integer() && j["code"].get<int>() != 200) {
            last_error_ = "code " + std::to_string(j["code"].get<int>());
            return std::nullopt;
        }
        return j;
    }

    HttpClient& http_;
    int64_t acct_;
    Specs specs_;
    TokenFn token_;
    RestGate gate_;
    int timeout_ms_;
    int last_status_ = 0;
    std::string last_error_;
    int64_t calls_ = 0;
};

}  // namespace lighter::live
