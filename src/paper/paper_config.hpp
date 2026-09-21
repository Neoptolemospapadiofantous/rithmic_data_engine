#pragma once
/*  ═══════════════════════════════════════════════════════════════════════════
    paper_config.hpp — paper fleet JSON config loader

    Local copy of the find_top_key/json_* idiom from orb_config.hpp (which we
    must NOT edit). Adds a balanced-object extractor so the "strategies" array
    of objects can be parsed: each element is extracted as a substring and then
    parsed with the same top-level-key extractors.

    Fleet config schema (config/paper_fleet.json):
    {
      "account_label": "tradeify",
      "symbol": "MNQ", "exchange": "CME",
      "point_value": 2.0, "tick_size": 0.25,
      "starting_balance": 25000.0,
      "daily_loss_limit": -500.0,          // ACCOUNT envelope daily loss
      "trailing_drawdown_cap": 1000.0,     // ACCOUNT envelope trailing DD
      "consistency_cap_pct": 0.30,
      "commission_rt": 1.0,
      "slippage_ticks": 1,
      "poll_ms": 100,
      "feed_gap_reset_secs": 300,          // tick hole > this: flat orb/trend strategies reset (no entries on the jump)
      "strategy_daily_loss_limit": -250.0, // per-strategy halt threshold
      "strategies": [
        {"id": "orb_5_15_10", "engine": "orb", "enabled": true,
         "params": { ... OrbConfig field overrides ... }}
      ]
    }
    ═══════════════════════════════════════════════════════════════════════════ */
#include "orb_config.hpp"

#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace paper {

struct FleetStrategy {
    std::string id;
    std::string engine = "orb";
    bool        enabled = true;
    std::string params_json;  // raw "{...}" text of the params object
};

struct FleetConfig {
    std::string account_label = "tradeify";
    std::string symbol        = "MNQ";
    std::string exchange      = "CME";
    // Feed symbol: what the collector actually writes to `ticks` (Rithmic
    // front-month, e.g. "NQ") — may differ from the traded instrument
    // label (`symbol`, e.g. "MNQ"; same price series, micro point value).
    // Empty → poll `symbol`.
    std::string feed_symbol;
    // Intermarket reference symbol polled alongside the primary feed and
    // fanned out to MTF strategies as 1m bars (SMT/correlation module).
    // Empty → no reference feed (SMT stays inert).
    std::string reference_symbol;
    double      point_value   = 2.0;
    double      tick_size     = 0.25;
    double      starting_balance      = 25000.0;
    double      daily_loss_limit      = -500.0;   // account envelope
    double      trailing_drawdown_cap = 1000.0;   // account envelope
    double      consistency_cap_pct   = 0.30;
    double      commission_rt         = 1.0;
    int         slippage_ticks        = 1;
    int         poll_ms               = 100;
    double      strategy_daily_loss_limit = -250.0;  // per-strategy halt
    int         feed_gap_reset_secs   = 300;         // tick hole longer than this → flat orb/trend strategies restart their session

    std::vector<FleetStrategy> strategies;

    static FleetConfig from_file(const std::string& path) {
        std::ifstream f(path);
        if (!f) throw std::runtime_error("Cannot open fleet config: " + path);
        std::string text((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());

        FleetConfig c;
        c.account_label = json_str(text, "account_label", c.account_label);
        c.symbol        = json_str(text, "symbol",        c.symbol);
        c.exchange      = json_str(text, "exchange",      c.exchange);
        c.feed_symbol   = json_str(text, "feed_symbol",   c.feed_symbol);
        c.reference_symbol = json_str(text, "reference_symbol", c.reference_symbol);
        c.point_value   = json_dbl(text, "point_value",   c.point_value);
        c.tick_size     = json_dbl(text, "tick_size",     c.tick_size);
        c.starting_balance      = json_dbl(text, "starting_balance",      c.starting_balance);
        c.daily_loss_limit      = json_dbl(text, "daily_loss_limit",      c.daily_loss_limit);
        c.trailing_drawdown_cap = json_dbl(text, "trailing_drawdown_cap", c.trailing_drawdown_cap);
        c.consistency_cap_pct   = json_dbl(text, "consistency_cap_pct",   c.consistency_cap_pct);
        c.commission_rt         = json_dbl(text, "commission_rt",         c.commission_rt);
        c.slippage_ticks        = json_int(text, "slippage_ticks",        c.slippage_ticks);
        c.poll_ms               = json_int(text, "poll_ms",               c.poll_ms);
        c.feed_gap_reset_secs   = json_int(text, "feed_gap_reset_secs",   c.feed_gap_reset_secs);
        c.strategy_daily_loss_limit = json_dbl(text, "strategy_daily_loss_limit",
                                               c.strategy_daily_loss_limit);

        // Extract the "strategies" array, then each balanced {...} object in it.
        std::string arr = extract_value(text, "strategies");
        if (arr.empty() || arr.front() != '[')
            throw std::runtime_error("Fleet config missing \"strategies\" array: " + path);

        for_each_object(arr, [&](const std::string& obj) {
            FleetStrategy s;
            s.id      = json_str(obj, "id", "");
            s.engine  = json_str(obj, "engine", "orb");
            s.enabled = json_bool(obj, "enabled", true);
            s.params_json = extract_value(obj, "params");
            if (s.params_json.empty()) s.params_json = "{}";
            if (s.id.empty())
                throw std::runtime_error("Fleet strategy entry missing \"id\"");
            c.strategies.push_back(std::move(s));
        });
        if (c.strategies.empty())
            throw std::runtime_error("Fleet config has zero strategies: " + path);
        return c;
    }

    // Build an OrbConfig for one strategy: tradeify-base defaults with the
    // strategy's params object overlaid field-by-field.
    OrbConfig orb_config_for(const FleetStrategy& s) const {
        OrbConfig c;
        // Base = config/tradeify_config.json values (not the struct defaults).
        c.orb_minutes        = 5;
        c.sl_points          = 15.0;
        c.trail_step         = 10.0;
        c.trail_be_trigger   = 3.0;
        c.trail_delay_secs   = 300;
        c.trail_be_offset    = 1.0;
        c.max_daily_trades   = 3;
        c.last_entry_hour    = 23;
        c.eod_flatten_hour   = 15;
        c.eod_flatten_min    = 55;
        c.session_open_hour  = 9;
        c.session_open_min   = 30;
        c.qty                = 1;
        c.stop_cooldown_secs = 5;
        c.symbol        = symbol;
        c.exchange      = exchange;
        c.point_value   = point_value;
        c.account_label = account_label;
        c.strategy      = s.id;
        c.starting_balance      = starting_balance;
        c.commission_rt         = commission_rt;
        c.daily_loss_limit      = strategy_daily_loss_limit;
        // Per-strategy trailing-DD / consistency are delegated to the account
        // envelope in the engine; keep the per-strategy RiskManager focused on
        // the daily-loss gate only.
        c.trailing_drawdown_cap = 1e9;
        c.consistency_cap_pct   = 1.0;
        c.dry_run = true;

        const std::string& p = s.params_json;
        c.orb_minutes        = json_int(p, "orb_minutes",        c.orb_minutes);
        c.sl_points          = json_dbl(p, "sl_points",          c.sl_points);
        c.trail_be_trigger   = json_dbl(p, "trail_be_trigger",   c.trail_be_trigger);
        c.trail_step         = json_dbl(p, "trail_step",         c.trail_step);
        c.trail_delay_secs   = json_int(p, "trail_delay_secs",   c.trail_delay_secs);
        c.trail_be_offset    = json_dbl(p, "trail_be_offset",    c.trail_be_offset);
        c.breakout_buffer    = json_dbl(p, "breakout_buffer",    c.breakout_buffer);
        c.max_entry_offset   = json_dbl(p, "max_entry_offset",   c.max_entry_offset);
        c.max_daily_trades   = json_int(p, "max_daily_trades",   c.max_daily_trades);
        c.last_entry_hour    = json_int(p, "last_entry_hour",    c.last_entry_hour);
        c.eod_flatten_hour   = json_int(p, "eod_flatten_hour",   c.eod_flatten_hour);
        c.eod_flatten_min    = json_int(p, "eod_flatten_min",    c.eod_flatten_min);
        c.news_blackout_min  = json_int(p, "news_blackout_min",  c.news_blackout_min);
        c.stop_cooldown_secs = json_int(p, "stop_cooldown_secs", c.stop_cooldown_secs);
        c.qty                = json_int(p, "qty",                c.qty);
        c.session_open_hour  = json_int(p, "session_open_hour",  c.session_open_hour);
        c.session_open_min   = json_int(p, "session_open_min",   c.session_open_min);
        c.daily_loss_limit   = json_dbl(p, "daily_loss_limit",   c.daily_loss_limit);
        return c;
    }

    // ── JSON helpers (same idiom as orb_config.hpp, kept local) ─────────────
    static size_t find_top_key(const std::string& s, const std::string& key) {
        const std::string needle = "\"" + key + "\"";
        int  depth  = 0;
        bool in_str = false;
        for (size_t i = 0; i < s.size(); ++i) {
            char c = s[i];
            if (in_str) {
                if (c == '\\') { ++i; continue; }
                if (c == '"') in_str = false;
                continue;
            }
            if (c == '"') {
                if (depth == 1 && s.compare(i, needle.size(), needle) == 0) {
                    size_t j = i + needle.size();
                    while (j < s.size() &&
                           (s[j] == ' ' || s[j] == '\t' || s[j] == '\r' || s[j] == '\n')) ++j;
                    if (j < s.size() && s[j] == ':') return i;
                }
                in_str = true;
                continue;
            }
            if (c == '{' || c == '[') ++depth;
            else if (c == '}' || c == ']') --depth;
        }
        return std::string::npos;
    }

    // Return the raw text of the value for a top-level key (object, array,
    // string, or scalar), brace-matched. Empty string if absent.
    static std::string extract_value(const std::string& s, const std::string& key) {
        auto pos = find_top_key(s, key);
        if (pos == std::string::npos) return "";
        auto colon = s.find(':', pos);
        if (colon == std::string::npos) return "";
        auto vp = s.find_first_not_of(" \t\r\n", colon + 1);
        if (vp == std::string::npos) return "";
        char c = s[vp];
        if (c == '{' || c == '[') {
            char open = c, close = (c == '{') ? '}' : ']';
            int depth = 0;
            bool in_str = false;
            for (size_t i = vp; i < s.size(); ++i) {
                char ch = s[i];
                if (in_str) {
                    if (ch == '\\') { ++i; continue; }
                    if (ch == '"') in_str = false;
                    continue;
                }
                if (ch == '"') { in_str = true; continue; }
                if (ch == open) ++depth;
                else if (ch == close) {
                    if (--depth == 0) return s.substr(vp, i - vp + 1);
                }
            }
            return "";
        }
        if (c == '"') {
            for (size_t i = vp + 1; i < s.size(); ++i) {
                if (s[i] == '\\') { ++i; continue; }
                if (s[i] == '"') return s.substr(vp, i - vp + 1);
            }
            return "";
        }
        auto end = s.find_first_of(",}]\r\n\t ", vp);
        return s.substr(vp, end == std::string::npos ? end : end - vp);
    }

    // Invoke fn(substring) for each top-level {...} object inside a "[...]" text.
    template <typename Fn>
    static void for_each_object(const std::string& arr, Fn fn) {
        int  depth  = 0;
        bool in_str = false;
        size_t obj_start = std::string::npos;
        for (size_t i = 0; i < arr.size(); ++i) {
            char c = arr[i];
            if (in_str) {
                if (c == '\\') { ++i; continue; }
                if (c == '"') in_str = false;
                continue;
            }
            if (c == '"') { in_str = true; continue; }
            if (c == '{') {
                if (depth == 1) obj_start = i;
                ++depth;
            } else if (c == '}') {
                --depth;
                if (depth == 1 && obj_start != std::string::npos) {
                    fn(arr.substr(obj_start, i - obj_start + 1));
                    obj_start = std::string::npos;
                }
            } else if (c == '[') {
                ++depth;
            } else if (c == ']') {
                --depth;
            }
        }
    }

    static int json_int(const std::string& s, const std::string& key, int def) {
        std::string v = extract_value(s, key);
        if (v.empty()) return def;
        try { return std::stoi(v); } catch (...) { return def; }
    }
    static double json_dbl(const std::string& s, const std::string& key, double def) {
        std::string v = extract_value(s, key);
        if (v.empty()) return def;
        try { return std::stod(v); } catch (...) { return def; }
    }
    static bool json_bool(const std::string& s, const std::string& key, bool def) {
        std::string v = extract_value(s, key);
        if (v == "true")  return true;
        if (v == "false") return false;
        return def;
    }
    static std::string json_str(const std::string& s, const std::string& key,
                                const std::string& def) {
        std::string v = extract_value(s, key);
        if (v.size() < 2 || v.front() != '"') return def;
        std::string out;
        for (size_t i = 1; i + 1 < v.size(); ++i) {
            char c = v[i];
            if (c == '\\' && i + 1 < v.size() - 1) { out += v[i + 1]; ++i; continue; }
            out += c;
        }
        return out;
    }
};

} // namespace paper
