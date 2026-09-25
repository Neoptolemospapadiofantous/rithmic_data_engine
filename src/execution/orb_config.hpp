#pragma once
/*  ═══════════════════════════════════════════════════════════════════════════
    orb_config.hpp — NQ Micro ORB execution engine configuration

    Loaded from config/orb_config.json at startup.
    All numeric defaults reflect the best-known Legends 50K params.
    ═══════════════════════════════════════════════════════════════════════════ */
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

// ─── Contract constants (tick geometry is same for NQ and MNQ) ───────────────
inline constexpr double NQ_TICK_SIZE     = 0.25;   // minimum price increment (NQ and MNQ)
inline constexpr double NQ_COMMISSION    = 2.0;    // $ per side (NQ only)
inline constexpr double MNQ_COMMISSION   = 0.50;   // $ per side (MNQ: exchange+NFA+brokerage)
// Per-tick dollar values differ by contract: NQ=$5.00, MNQ=$0.50
inline constexpr double NQ_TICK_VALUE    = 5.00;   // NQ only — do NOT use for MNQ slippage
inline constexpr double MNQ_TICK_VALUE   = 0.50;   // MNQ = 2.0 $/pt × 0.25 tick

// ─── US Eastern Time offset (EDT=4, EST=5) ───────────────────────────────────
// Proper DST rule: second Sunday of March at 07:00 UTC (2:00 AM EST)
//                  first Sunday of November at 06:00 UTC (2:00 AM EDT)
#include <ctime>
inline int us_et_offset(const struct tm& utc_tm) {
    int year  = utc_tm.tm_year + 1900;
    int month = utc_tm.tm_mon + 1;
    int mday  = utc_tm.tm_mday;
    int hour  = utc_tm.tm_hour;

    if (month < 3 || month > 11) return 5;  // EST
    if (month > 3 && month < 11) return 4;  // EDT

    // Day-of-week for 1st of month (Sakamoto's algorithm, 0=Sunday)
    auto first_dow = [](int y, int m) -> int {
        static const int t[] = {0,3,2,5,0,3,5,1,4,6,2,4};
        y -= (m < 3);
        return (y + y/4 - y/100 + y/400 + t[m-1] + 1) % 7;
    };

    if (month == 3) {
        int fd        = first_dow(year, 3);
        int first_sun = (fd == 0) ? 1 : (8 - fd);
        int second_sun = first_sun + 7;
        if (mday < second_sun) return 5;
        if (mday > second_sun) return 4;
        return (hour >= 7) ? 4 : 5;   // spring forward at 07:00 UTC
    }
    // month == 11
    int fd        = first_dow(year, 11);
    int first_sun = (fd == 0) ? 1 : (8 - fd);
    if (mday < first_sun) return 4;
    if (mday > first_sun) return 5;
    return (hour < 6) ? 4 : 5;        // fall back at 06:00 UTC
}

// ─── Trading date (CME / prop-firm day) ──────────────────────────────────────
// The futures trading day — and a prop firm's daily loss limit (Tradeify resets at
// 17:00 CT = 18:00 ET) — runs 18:00 ET → 17:00 ET. Trades from 18:00 ET onward belong
// to the NEXT date, so an evening session never inherits the day that just closed.
// Returns YYYY-MM-DD for the Unix time `tt`.
inline std::string trading_date_str(time_t tt) {
    struct tm utc_tm;
    gmtime_r(&tt, &utc_tm);
    time_t et_t = tt - us_et_offset(utc_tm) * 3600;
    struct tm et_tm;
    gmtime_r(&et_t, &et_tm);
    if (et_tm.tm_hour >= 18) { et_t += 24 * 3600; gmtime_r(&et_t, &et_tm); }
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &et_tm);
    return buf;
}

struct OrbConfig {
    // ── Strategy params ────────────────────────────────────────────
    int    orb_minutes         = 15;   // opening range duration (9:30–9:45 ET)
    double sl_points           = 15.0; // stop-loss distance in points
    double trail_be_trigger    = 3.0;  // MFE required before trailing activates
    double trail_step          = 10.0; // trailing stop distance in points
    int    trail_delay_secs    = 300;  // seconds after fill before trailing starts
    double trail_be_offset     = 1.0;  // SL move to entry + this offset at BE trigger
    double breakout_buffer     = 0.0;  // extra points beyond ORB high/low to confirm break
    double max_entry_offset    = 0.0;  // max pts from ORB level at signal time (0=disabled)
    int    max_daily_trades    = 3;    // max entries per session
    int    last_entry_hour     = 13;   // no new entries at or after this ET hour
    int    eod_flatten_hour    = 15;   // EOD flatten hour (ET)
    int    eod_flatten_min     = 55;   // EOD flatten minute (ET)
    int    news_blackout_min   = 5;    // minutes before/after news event to block entry
    int    stop_cooldown_secs  = 5;    // seconds to block re-entry after any stop exit (0=disabled)
    int    sl_fire_timeout_ms  = 3000; // ms before software SL fires if exchange stop is unresponsive
    int    net_mismatch_grace_ms = 5000; // ms the exchange net position may disagree with ours (in-flight fills) before the difference is unwound and entries halt
    int    qty                 = 1;    // contract quantity per trade

    // ── Session open (defaults: RTH 9:30 ET) ──────────────────────
    int    session_open_hour   = 9;    // ET hour of session open (ORB window start)
    int    session_open_min    = 30;   // ET minute of session open

    // ── Risk rules (Legends 50K Master) ───────────────────────────
    double trailing_drawdown_cap = 2500.0; // max $ drawdown from equity peak
    double consistency_cap_pct   = 0.30;   // no single day > 30% of total profit
    double daily_loss_limit      = -1000.0; // halt if daily_pnl <= this value
    double commission_rt         = 1.0;    // round-trip commission $ per contract

    // ── Rithmic instrument ─────────────────────────────────────────
    std::string symbol         = "MNQ";
    std::string trade_contract = "";    // specific contract e.g. MNQM6 (leave empty = use symbol)
    std::string exchange    = "CME";
    double      point_value = 2.0;    // $/point: MNQ=2.0, NQ=20.0 — read from config
    std::string environment = "legends"; // "legends" or "paper"

    // ── Rithmic MD connection (AMP — TICKER_PLANT) ───────────────────
    // AMP credentials for market data; Legends/Tradeify ORDER_PLANT has
    // its own session via RITHMIC_LEGENDS_* — no session conflict.
    // Market-data source. Provider name from RITHMIC_MD_PROVIDER: a broker name
    // (legends/tradeify/amp → WebSocket TICKER_PLANT login with MD_<PROVIDER>_*
    // creds) or "pg" → no Rithmic MD session at all: ticks are read from the
    // collector's Postgres `ticks` table (same feed the paper fleet uses), so
    // the one TICKER_PLANT session a prop login allows can belong to the
    // 24/7 collector instead of this executor.
    // Paper-fleet book knobs (see paper/paper_quote.hpp). All off by default.
    std::string fill_model        = "last_slip";   // last_slip | bbo
    double      spread_gate_ticks = 0.0;           // block entries when spread > N ticks
    double      spread_gate_rel   = 0.0;           // block when spread > k × rolling mean
    double      imbalance_min     = 0.0;           // longs need bid share ≥ x (shorts ≤ 1−x)
    bool        microprice_lead   = false;         // microprice must lean the entry's way
    double      imbalance_max     = 0.0;           // INVERTED: longs need bid share ≤ x (fade the stacked side)
    double      book_exit_flip    = 0.0;           // in a long: flatten when bid share ≤ x (mirror for shorts)
    bool        book_be_on_flip   = false;         // move the stop to break-even as soon as the book flips against
    double      book_tp_imbalance = 0.0;           // in profit and the book stacks in favour ≥ x → take profit
    double      book_tp_min_pts   = 1.0;           // …only once at least this many points in profit (slippage + commission cover)
    int         book_size_agree   = 0;             // extra contracts when the book agrees with the entry
    int         fill_wait_secs    = 0;             // wait up to N s for spread ≤ 1 tick / microprice lean before filling
    std::string base_id;                           // sibling variants: the strategy this derives from
    std::string overlay;                           // sibling variants: which overlay ("sg","imb","micro","all"…)
    std::string md_provider    = "legends";
    std::string md_feed_symbol = "NQ";   // symbol the collector writes (pg mode)
    int         md_poll_ms     = 100;    // pg mode poll cadence
    bool md_from_pg() const { return md_provider == "pg"; }
    std::string md_user;
    std::string md_password;
    std::string md_system_name  = "Rithmic 01";
    std::string md_url          = "wss://rprotocol-mobile.rithmic.com:443";

    // ── Rithmic ORDER connection (Legends — ORDER_PLANT) ───────────
    std::string rithmic_user;
    std::string rithmic_password;
    std::string rithmic_system_name = "LegendsTrading";
    std::string rithmic_url         = "wss://ritpz01001.01.rithmic.com:443";
    std::string app_name            = "nepa:OentexNQBot";
    std::string app_version         = "1.0";

    // ── Rithmic account (ORDER_PLANT) ──────────────────────────────
    std::string account_id   = "";   // e.g. LTARAPAPA502114908626
    std::string fcm_id       = "";   // Rithmic FCM identifier (usually empty)
    std::string ib_id        = "";   // Rithmic IB identifier (usually empty)
    std::string trade_route  = "simulator";  // Legends route; NEVER use "Rithmic Order Routing"

    // ── Instance identity ──────────────────────────────────────────
    std::string account_label    = "legends";         // DB tag: "legends", "tradeify", …
    std::string strategy         = "ORB";             // DB strategy tag — must differ per strategy sharing an account
    // Which strategy class the executor runs: "orb" (OrbStrategy) or "trend" (TrendStrategy,
    // mode/params read from the same file by TrendConfig::from_json_string). One engine per
    // process; the per-account instance lock keeps two engines off one account.
    std::string engine           = "orb";
    std::string order_env_prefix = "RITHMIC_LEGENDS"; // prefix for ORDER_PLANT env vars

    // ── Account ───────────────────────────────────────────────────
    double starting_balance = 50000.0; // actual Rithmic account balance at last sync

    // ── MD watchdog ───────────────────────────────────────────────
    int tick_timeout_s = 30; // reconnect MD if no tick received for this many seconds (0=disabled)

    // ── Cycling ORB mode ──────────────────────────────────────────
    // When true the executor exits after max_daily_trades completes (position flat).
    // The wrapper script immediately restarts it with a fresh cycle_start_epoch so
    // count_today_trades only counts trades from the new cycle, not prior ones.
    // cycle_timeout_mins: wrapper kills executor after this many minutes if no trades
    // completed (e.g. overnight with no ORB breakout). Default: orb_minutes + 30.
    bool    cycle_mode         = false;
    int64_t cycle_start_epoch  = 0;   // Unix timestamp; seed only counts trades after this (0=all)
    int     cycle_timeout_mins = 0;   // 0 = let wrapper use default (orb_minutes + 30)

    // ── Safety ────────────────────────────────────────────────────
    bool dry_run = true;   // true = log signals only, no real orders

    // ── Database ──────────────────────────────────────────────────
    std::string pg_host     = "localhost";
    std::string pg_port     = "5432";
    std::string pg_db       = "rithmic";
    std::string pg_user     = "rithmic_user";
    std::string pg_password;

    std::string pg_connstr() const {
        return "host="      + pg_escape_kv(pg_host)     +
               " port="     + pg_escape_kv(pg_port)     +
               " dbname="   + pg_escape_kv(pg_db)       +
               " user="     + pg_escape_kv(pg_user)     +
               " password=" + pg_escape_kv(pg_password) +
               " connect_timeout=10"
               " application_name=nq_executor_" + account_label;
    }

    // ── Parse from JSON file ───────────────────────────────────────
    // Minimal hand-rolled parser — avoids pulling in nlohmann/json
    // when not already present, but uses it when available via cmake.
    // We use a simple key-value grep approach for robustness.
    static OrbConfig from_file(const fs::path& path) {
        if (!fs::exists(path))
            throw std::runtime_error("Config file not found: " + path.string());

        std::ifstream f(path);
        if (!f) throw std::runtime_error("Cannot open config: " + path.string());

        std::string text((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());

        OrbConfig c;

        // Load env overrides first (same as existing Config pattern)
        load_dotenv(".env");
        c.pg_password = env("PG_PASSWORD", "");
        if (c.pg_password.empty())
            c.pg_password = env("RITHMIC_PG_PASSWORD", "");

        // MD provider selection: RITHMIC_MD_PROVIDER selects which broker feeds market data.
        // Supported values: legends, tradeify, amp
        // Corresponding env vars: MD_{PROVIDER}_USER / _PASSWORD / _SYSTEM / _URL
        {
            std::string provider = env("RITHMIC_MD_PROVIDER", "legends");
            c.md_provider = provider;
            // Uppercase provider name for env var lookup
            std::string up = provider;
            for (char& ch : up) ch = (char)toupper((unsigned char)ch);
            c.md_user        = env(("MD_" + up + "_USER").c_str(),     "");
            c.md_password    = env(("MD_" + up + "_PASSWORD").c_str(),  "");
            c.md_system_name = env(("MD_" + up + "_SYSTEM").c_str(),   c.md_system_name.c_str());
            c.md_url         = env(("MD_" + up + "_URL").c_str(),       c.md_url.c_str());
            fprintf(stderr, "[CONFIG] MD provider=%s user=%s system=%s\n",
                provider.c_str(), c.md_user.c_str(), c.md_system_name.c_str());
        }

        // Instance identity — read first so the prefix drives all credential lookups
        c.account_label    = json_str(text, "account_label",    c.account_label);
        c.strategy         = json_str(text, "strategy",         c.strategy);
        c.engine           = json_str(text, "engine",           c.engine);
        c.order_env_prefix = json_str(text, "order_env_prefix", c.order_env_prefix);

        // ORDER_PLANT credentials — derived from order_env_prefix so any account works
        // without code changes (add a new JSON config + env vars, done).
        {
            std::string pfx = c.order_env_prefix;
            for (char& ch : pfx) ch = (char)toupper((unsigned char)ch);
            c.rithmic_user        = env((pfx + "_USER").c_str(),     "");
            if (c.rithmic_user.empty())
                c.rithmic_user    = json_str(text, "rithmic_user", "");
            c.rithmic_password    = env((pfx + "_PASSWORD").c_str(),  "");
            c.rithmic_system_name = env((pfx + "_SYSTEM").c_str(),   c.rithmic_system_name.c_str());
            c.rithmic_url         = env((pfx + "_URL").c_str(),       c.rithmic_url.c_str());
            // account_id: env var takes precedence; JSON is fallback
            const char* acct_v    = std::getenv((pfx + "_ACCOUNT").c_str());
            if (acct_v && acct_v[0]) c.account_id = acct_v;
        }
        c.app_name    = env("RITHMIC_APP_NAME",    "nepa:OentexNQBot");
        c.app_version = env("RITHMIC_APP_VERSION", "1.0");

        // Pull strategy fields from JSON text with simple extractor
        c.orb_minutes          = json_int(text,  "orb_minutes",          c.orb_minutes);
        c.sl_points            = json_dbl(text,  "sl_points",            c.sl_points);
        c.trail_be_trigger     = json_dbl(text,  "trail_be_trigger",     c.trail_be_trigger);
        c.trail_step           = json_dbl(text,  "trail_step",           c.trail_step);
        c.trail_delay_secs     = json_int(text,  "trail_delay_secs",     c.trail_delay_secs);
        c.trail_be_offset      = json_dbl(text,  "trail_be_offset",      c.trail_be_offset);
        c.breakout_buffer      = json_dbl(text,  "breakout_buffer",      c.breakout_buffer);
        c.max_entry_offset     = json_dbl(text,  "max_entry_offset",     c.max_entry_offset);
        c.max_daily_trades     = json_int(text,  "max_daily_trades",     c.max_daily_trades);
        c.last_entry_hour      = json_int(text,  "last_entry_hour",      c.last_entry_hour);
        c.eod_flatten_hour     = json_int(text,  "eod_flatten_hour",     c.eod_flatten_hour);
        c.eod_flatten_min      = json_int(text,  "eod_flatten_min",      c.eod_flatten_min);
        c.news_blackout_min    = json_int(text,  "news_blackout_min",    c.news_blackout_min);
        c.stop_cooldown_secs   = json_int(text,  "stop_cooldown_secs",   c.stop_cooldown_secs);
        c.sl_fire_timeout_ms   = json_int(text,  "sl_fire_timeout_ms",   c.sl_fire_timeout_ms);
        c.net_mismatch_grace_ms = json_int(text, "net_mismatch_grace_ms", c.net_mismatch_grace_ms);
        c.qty                  = json_int(text,  "qty",                  c.qty);

        c.trailing_drawdown_cap = json_dbl(text, "trailing_drawdown_cap", c.trailing_drawdown_cap);
        c.consistency_cap_pct   = json_dbl(text, "consistency_cap_pct",   c.consistency_cap_pct);
        c.daily_loss_limit      = json_dbl(text, "daily_loss_limit",      c.daily_loss_limit);
        c.commission_rt         = json_dbl(text, "commission_rt",         c.commission_rt);

        c.symbol         = json_str(text, "symbol",         c.symbol);
        c.trade_contract = json_str(text, "trade_contract", c.trade_contract);
        c.md_feed_symbol = json_str(text, "md_feed_symbol", c.md_feed_symbol);
        c.md_poll_ms     = json_int(text, "md_poll_ms",     c.md_poll_ms);
        c.exchange       = json_str(text, "exchange",       c.exchange);
        c.point_value    = json_dbl(text, "point_value",    c.point_value);
        c.environment       = json_str(text, "environment",       c.environment);
        c.starting_balance  = json_dbl(text, "starting_balance",  c.starting_balance);
        // account_id: JSON is fallback if env var (set above) was empty
        if (c.account_id.empty())
            c.account_id = json_str(text, "account_id", c.account_id);
        c.fcm_id           = json_str(text, "fcm_id",           c.fcm_id);
        c.ib_id            = json_str(text, "ib_id",            c.ib_id);
        c.trade_route      = json_str(text, "trade_route",      c.trade_route);
        c.session_open_hour = (int)json_dbl(text, "session_open_hour", c.session_open_hour);
        c.session_open_min  = (int)json_dbl(text, "session_open_min",  c.session_open_min);
        c.tick_timeout_s    = json_int(text, "tick_timeout_s",         c.tick_timeout_s);
        c.cycle_start_epoch  = (int64_t)json_dbl(text, "cycle_start_epoch",  (double)c.cycle_start_epoch);
        c.cycle_timeout_mins = json_int(text, "cycle_timeout_mins", c.cycle_timeout_mins);

        // cycle_mode / dry_run: top-level boolean keys
        c.cycle_mode = json_bool(text, "cycle_mode", c.cycle_mode);
        c.dry_run    = json_bool(text, "dry_run",    c.dry_run);

        // DB overrides from JSON
        c.pg_host = json_str(text, "pg_host", c.pg_host);
        c.pg_port = json_str(text, "pg_port", c.pg_port);
        c.pg_db   = json_str(text, "pg_db",   c.pg_db);
        c.pg_user = json_str(text, "pg_user", c.pg_user);
        if (c.pg_password.empty())
            c.pg_password = json_str(text, "pg_password", "");

        c.validate();
        return c;
    }

    // ── Config validation — called at the end of from_file() ────────
    // Throws std::runtime_error with a FATAL message naming the bad key.
    void validate() const {
        auto need_positive = [](const char* key, double v) {
            if (v <= 0.0)
                throw std::runtime_error(std::string("FATAL: invalid config key '") + key +
                    "' — must be > 0 (got " + std::to_string(v) + ")");
        };
        need_positive("qty",                   (double)qty);
        need_positive("orb_minutes",           (double)orb_minutes);
        need_positive("trail_step",            trail_step);
        need_positive("sl_points",             sl_points);
        need_positive("trailing_drawdown_cap", trailing_drawdown_cap);

        // account_label / strategy are interpolated into SQL identifiers and
        // raw SQL strings (NOTIFY live_tick_<account>, startup ORB query) —
        // restrict to a safe charset at load time.
        auto need_safe_ident = [](const char* key, const std::string& v) {
            if (v.empty() ||
                v.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos)
                throw std::runtime_error(std::string("FATAL: invalid config key '") + key +
                    "' — must match [a-z0-9_]+ (got '" + v + "')");
        };
        need_safe_ident("account_label", account_label);
        // strategy is uppercased ORB-style by convention — allow A-Z too
        if (strategy.empty() ||
            strategy.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos)
            throw std::runtime_error(std::string("FATAL: invalid config key 'strategy'") +
                " — must match [A-Za-z0-9_]+ (got '" + strategy + "')");
        if (engine != "orb" && engine != "trend")
            throw std::runtime_error("FATAL: invalid config key 'engine' — must be \"orb\" or \"trend\" (got '" +
                                     engine + "')");
        // live_trades / live_sessions rows are keyed by the strategy tag — a trend engine
        // writing under "ORB" would be counted as ORB trades and restart-seeded as ORB.
        if (engine == "trend" && strategy == "ORB")
            throw std::runtime_error("FATAL: engine \"trend\" needs its own 'strategy' tag (not \"ORB\")");
    }

private:
    // Wrap a libpq keyword=value value in single quotes, escaping ' and \.
    // Prevents injection via crafted host/password values (H-SEC-3).
    static std::string pg_escape_kv(const std::string& v) {
        std::string out = "'";
        for (char c : v) {
            if (c == '\'') out += "\\'";
            else if (c == '\\') out += "\\\\";
            else out += c;
        }
        out += "'";
        return out;
    }

    // Simple JSON field extractors (no deps).
    // All extractors match keys ONLY at top-level object depth (brace depth 1)
    // and never inside string values — nested objects (e.g. "prop_firm": {...})
    // and "_comment" strings quoting key names cannot shadow real keys.
    //
    // find_top_key: scan the whole document tracking brace/bracket depth and
    // in-string state (with backslash escapes). A match must be the exact
    // quoted key, at depth 1, followed by ':' (modulo whitespace) — a quoted
    // string VALUE equal to "key" is not mistaken for a key.
    static size_t find_top_key(const std::string& s, const std::string& key) {
        const std::string needle = "\"" + key + "\"";
        int  depth  = 0;
        bool in_str = false;
        for (size_t i = 0; i < s.size(); ++i) {
            char c = s[i];
            if (in_str) {
                if (c == '\\') { ++i; continue; }   // skip escaped char
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

    static int json_int(const std::string& s, const std::string& key, int def) {
        auto pos = find_top_key(s, key);
        if (pos == std::string::npos) return def;
        auto colon = s.find(':', pos);
        if (colon == std::string::npos) return def;
        auto vp = s.find_first_not_of(" \t\r\n", colon + 1);
        if (vp == std::string::npos) return def;
        try { return std::stoi(s.substr(vp)); }
        catch (...) { return def; }
    }

    static double json_dbl(const std::string& s, const std::string& key, double def) {
        auto pos = find_top_key(s, key);
        if (pos == std::string::npos) return def;
        auto colon = s.find(':', pos);
        if (colon == std::string::npos) return def;
        auto vp = s.find_first_not_of(" \t\r\n", colon + 1);
        if (vp == std::string::npos) return def;
        try { return std::stod(s.substr(vp)); }
        catch (...) { return def; }
    }

    static bool json_bool(const std::string& s, const std::string& key, bool def) {
        auto pos = find_top_key(s, key);
        if (pos == std::string::npos) return def;
        auto colon = s.find(':', pos);
        if (colon == std::string::npos) return def;
        auto vp = s.find_first_not_of(" \t\r\n", colon + 1);
        if (vp == std::string::npos) return def;
        if (s.compare(vp, 4, "true")  == 0) return true;
        if (s.compare(vp, 5, "false") == 0) return false;
        return def;
    }

    static std::string json_str(const std::string& s,
                                const std::string& key,
                                const std::string& def) {
        auto pos = find_top_key(s, key);
        if (pos == std::string::npos) return def;
        auto colon = s.find(':', pos);
        if (colon == std::string::npos) return def;
        auto vp = s.find_first_not_of(" \t\r\n", colon + 1);
        if (vp == std::string::npos || s[vp] != '"') return def;
        // Read the string honouring backslash escapes (\" and \\) so values
        // containing escaped quotes are not truncated at the first inner quote.
        std::string out;
        for (size_t i = vp + 1; i < s.size(); ++i) {
            char c = s[i];
            if (c == '\\' && i + 1 < s.size()) { out += s[i + 1]; ++i; continue; }
            if (c == '"') return out;
            out += c;
        }
        return def;  // unterminated string
    }

    static void load_dotenv(const fs::path& path) {
        if (!fs::exists(path)) return;
        std::ifstream f(path);
        if (!f) return;
        std::string line;
        while (std::getline(f, line)) {
            auto s = trim(line);
            if (s.empty() || s[0] == '#') continue;
            auto eq = s.find('=');
            if (eq == std::string::npos) continue;
            auto k = trim(s.substr(0, eq));
            auto v = trim(s.substr(eq + 1));
            if (!k.empty() && !std::getenv(k.c_str()))
                setenv(k.c_str(), v.c_str(), 0);
        }
    }

    static std::string env(const char* k, const char* d) {
        const char* v = std::getenv(k);
        return v ? v : d;
    }

    static std::string trim(const std::string& s) {
        const std::string_view ws = " \t\r\n\"";
        auto b = s.find_first_not_of(ws);
        if (b == std::string::npos) return {};
        return s.substr(b, s.find_last_not_of(ws) - b + 1);
    }
};
