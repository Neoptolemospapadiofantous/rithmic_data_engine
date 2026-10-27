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
        "max_margin_pct": 30,           // pull quotes while margin usage is above this (10..80; small accounts need more)
        "rate_reserve": 24,             // requests/min kept for cancels, the dead-man and REST (quoting gets budget − this)
        "send_budget_per_min": 30,      // sendTx/sendTxBatch requests alone (6 of them reserved for cancels / dead-man)
        "deadman_window_s": 315,        // scheduled cancel-all deadline ahead of server time (≥ 305: Lighter's min is 5 min)
        "order_expiry_s": 600,          // post-only quote expiry (Lighter: 5 min .. 30 days)
        "offline_collateral_usd": 20    // offline soak only: the fake account's cash
      }
    The live market spec is cross-checked against GET orderBookDetails at start; a difference refuses to trade. */

#include "dex/live/engine.hpp"

#include <map>
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
                                               "rate_reserve", "send_budget_per_min", "deadman_window_s", "order_expiry_s", "offline_collateral_usd", "label", "max_margin_pct",
                                               "strategy"};
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
        c.max_margin_pct = e.value("max_margin_pct", 30.0);
        c.rate_reserve = e.value("rate_reserve", 24);
        c.send_budget = e.value("send_budget_per_min", 30);
        c.deadman_window_ms = int64_t(e.value("deadman_window_s", 315)) * 1000;
        c.order_expiry_ms = int64_t(e.value("order_expiry_s", 600)) * 1000;
        out.offline_collateral_micro = int64_t(std::llround(e.value("offline_collateral_usd", 20.0) * kMicro));
        c.label = e.value("label", "");
        if (e.contains("strategy")) {
            const auto& st = e.at("strategy");
            if (!st.is_object()) throw ConfigError("engine.strategy must be an object");
            // name → (field, min, max): only the quoting knobs; size / inventory / fees are not here on purpose
            struct K { double* d; int64_t* i; double lo, hi; };
            std::map<std::string, K> ks = {
                {"half_width_ticks", {&c.strategy.half_width_ticks, nullptr, 0.5, 50}}, {"min_spread_ticks", {&c.strategy.min_spread_ticks, nullptr, 0, 50}},
                {"imb_pull", {&c.strategy.imb_pull, nullptr, 0, 1}}, {"pause_move_ticks", {&c.strategy.pause_move_ticks, nullptr, 0, 10}},
                {"requote_ticks", {&c.strategy.requote_ticks, nullptr, 0, 10}}, {"pause_ms", {nullptr, &c.strategy.pause_ms, 0, 60'000}}};
            for (auto& [k, v] : st.items()) {
                auto it = ks.find(k);
                if (it == ks.end()) throw ConfigError("unknown engine.strategy key '" + k + "'");
                if (!v.is_number()) throw ConfigError("engine.strategy." + k + " must be a number");
                const double x = v.get<double>();
                if (!(x >= it->second.lo && x <= it->second.hi))
                    throw ConfigError("engine.strategy." + k + " must be " + std::to_string(it->second.lo) + ".." + std::to_string(it->second.hi));
                if (it->second.d) *it->second.d = x; else *it->second.i = static_cast<int64_t>(std::llround(x));
            }
        }
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
    if (!(c.max_margin_pct >= 10.0 && c.max_margin_pct <= 80.0)) throw ConfigError("engine.max_margin_pct must be 10..80");
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
