#pragma once
/*  mm_live_config.hpp — the "engine" section of an mm_live config, parsed strictly (unknown keys refused) into an
    EngineConfig on top of the gateway config (gateway_config.hpp: mainnet-only host/chain, mode, arming, key file,
    risk). Example: docs/mm_live_runbook.md.

      "engine": {
        "market_spec": {"market_index": 27, "size_decimals": 3, "price_decimals": 3, "min_base": "0.045", "min_quote": "10"},
        "hl_sz_decimals": 2,            // Hyperliquid size decimals of the coin: the strategy's tick unit (frozen setting)
        "journal_dir": "data/mm_live/journal",
        "status_file": "data/mm_live/status.json",
        "max_collateral_usd": 50,       // capital guard: refuse to trade an account holding more
        "rate_reserve": 24,             // requests/min kept for cancels, the dead-man and REST (quoting gets budget − this)
        "send_budget_per_min": 30,      // sendTx/sendTxBatch requests alone (6 of them reserved for cancels / dead-man)
        "deadman_window_s": 315,        // scheduled cancel-all deadline ahead of server time (≥ 305: Lighter's min is 5 min)
        "order_expiry_s": 600,          // post-only quote expiry (Lighter: 5 min .. 30 days)
        "offline_collateral_usd": 20    // offline soak only: the fake account's cash
      }
    The live market spec is cross-checked against GET orderBookDetails at start; a difference refuses to trade. */

#include "dex/live/engine.hpp"
#include "dex/live/gateway_config.hpp"

#include <set>
#include <string>

namespace lighter::live {

struct MmLiveConfig {
    GatewayConfig gw;
    EngineConfig engine;
    int64_t offline_collateral_micro = 20 * kMicro;
};

inline MmLiveConfig build_mm_live_config(const GatewayConfig& gw) {
    MmLiveConfig out;
    out.gw = gw;
    const nlohmann::json& e = gw.engine;
    static const std::set<std::string> keys = {"market_spec", "hl_sz_decimals", "journal_dir", "status_file", "max_collateral_usd",
                                               "rate_reserve", "send_budget_per_min", "deadman_window_s", "order_expiry_s", "offline_collateral_usd", "label"};
    for (auto& [k, v] : e.items()) if (!keys.count(k)) throw ConfigError("unknown engine key '" + k + "'");
    if (!e.contains("market_spec") || !e.at("market_spec").is_object()) throw ConfigError("engine.market_spec required");
    if (!e.contains("hl_sz_decimals")) throw ConfigError("engine.hl_sz_decimals required (the strategy's tick unit)");
    EngineConfig& c = out.engine;
    try {
        const auto& ms = e.at("market_spec");
        static const std::set<std::string> mk = {"market_index", "size_decimals", "price_decimals", "min_base", "min_quote"};
        for (auto& [k, v] : ms.items()) if (!mk.count(k)) throw ConfigError("unknown market_spec key '" + k + "'");
        c.spec = MarketSpec::from_strings(gw.market, ms.at("market_index").get<int>(), ms.at("size_decimals").get<int>(),
                                          ms.at("price_decimals").get<int>(), ms.at("min_base").get<std::string>(),
                                          ms.at("min_quote").get<std::string>());
        check_market_index(c.spec.market_index);
        c.hl_sz_decimals = e.at("hl_sz_decimals").get<int>();
        c.journal_dir = e.value("journal_dir", gw.state_dir + "/journal");
        c.status_file = e.value("status_file", gw.state_dir + "/status.json");
        c.max_collateral_micro = int64_t(std::llround(e.value("max_collateral_usd", 50.0) * kMicro));
        c.rate_reserve = e.value("rate_reserve", 24);
        c.send_budget = e.value("send_budget_per_min", 30);
        c.deadman_window_ms = int64_t(e.value("deadman_window_s", 315)) * 1000;
        c.order_expiry_ms = int64_t(e.value("order_expiry_s", 600)) * 1000;
        out.offline_collateral_micro = int64_t(std::llround(e.value("offline_collateral_usd", 20.0) * kMicro));
        c.label = e.value("label", "");
    } catch (const nlohmann::json::exception& x) {
        throw ConfigError(std::string("engine: ") + x.what());
    } catch (const SpecError& x) {
        throw ConfigError(std::string("engine.market_spec: ") + x.what());
    } catch (const RangeError& x) {
        throw ConfigError(std::string("engine.market_spec: ") + x.what());
    }
    if (c.hl_sz_decimals < 0 || c.hl_sz_decimals > 6) throw ConfigError("engine.hl_sz_decimals must be 0..6");
    if (c.deadman_window_ms < 305'000 || c.deadman_window_ms > 3'600'000)
        throw ConfigError("engine.deadman_window_s must be 305..3600 (Lighter's scheduled cancel-all minimum is 5 min)");
    if (c.order_expiry_ms < kMinOrderExpiryAheadMs + 60'000 || c.order_expiry_ms > 86'400'000)
        throw ConfigError("engine.order_expiry_s must be 360..86400");
    if (c.rate_reserve < 6 || c.rate_reserve >= gw.rate_budget_per_min) throw ConfigError("engine.rate_reserve must be 6..rate_budget_per_min-1");
    if (c.max_collateral_micro <= 0) throw ConfigError("engine.max_collateral_usd must be > 0");
    if (c.send_budget <= c.send_reserve || c.send_budget > gw.rate_budget_per_min)
        throw ConfigError("engine.send_budget_per_min must be 7..rate_budget_per_min (default 30: Lighter's per-tx-type 40/min is unconfirmed)");
    c.mode = gw.mode;
    c.account_index = gw.account_index;
    c.api_key_index = gw.api_key_index;
    c.risk = gw.risk;
    c.state_dir = gw.state_dir;
    c.rate_budget = gw.rate_budget_per_min;
    c.max_txs_per_batch = gw.max_txs_per_batch;
    // R-NET-7: every journal / status / log name carries the network and the mode
    if (c.label.empty()) c.label = gw.mode == Mode::Live ? "lighter_main" : gw.mode == Mode::Probe ? "lighter_main_probe" : "lighter_main_offline";
    return out;
}

}  // namespace lighter::live
