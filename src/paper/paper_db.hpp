#pragma once
/*  ═══════════════════════════════════════════════════════════════════════════
    paper_db.hpp — PostgreSQL access for the paper-trading fleet engine

    Owns the paper_* tables (migrations/007_paper_fleet.sql). ensure_schema()
    is idempotent and runs at engine startup, same style as src/db.cpp.

    Also implements the PaperStore interface consumed by PaperBroker so the
    broker is testable without a database.
    ═══════════════════════════════════════════════════════════════════════════ */
#include <chrono>
#include <libpq-fe.h>

#include <optional>
#include <string>
#include <vector>

namespace paper {

// ── Value types ──────────────────────────────────────────────────────────────

struct PaperPositionRow {
    std::string strategy_id;
    int         direction = 0;       // +1 long, -1 short, 0 flat
    int         qty       = 0;
    double      entry_price = 0.0;
    int64_t     entry_time_us = 0;   // micros since epoch (UTC)
    double      stop_price  = 0.0;
    double      target_price = 0.0;
    double      unrealized_pnl = 0.0;
};

struct PaperTradeRow {
    std::string strategy_id;
    std::string account_label;
    std::string symbol;
    std::string direction;           // "LONG" / "SHORT"
    int         qty = 0;
    int64_t     entry_time_us = 0;
    double      entry_price = 0.0;
    int64_t     exit_time_us = 0;
    double      exit_price = 0.0;
    double      pnl_pts = 0.0;
    double      pnl_usd = 0.0;
    double      commission = 0.0;
    double      mae_pts = 0.0;   // max adverse excursion (≤ 0)
    double      mfe_pts = 0.0;   // max favorable excursion
    std::string exit_reason;
    // Book shadow (paper_quote.hpp): the same trade filled at bid/ask, whatever
    // fill model the strategy ran with. −1 spread = no fresh quote at that moment.
    double      entry_bbo = 0.0, exit_bbo = 0.0, pnl_bbo_usd = 0.0;
    double      spread_entry_ticks = -1.0, spread_exit_ticks = -1.0;
    std::string fill_model = "last_slip";
};

// One entry signal as the broker saw it — taken or blocked by a book gate.
struct PaperSignalRow {
    std::string strategy_id, account_label, direction, decision, reason;
    int64_t     ts_us = 0;
    double      price = 0.0, spread_ticks = -1.0, imbalance = -1.0, microprice_dev_ticks = 0.0, spread_rel = -1.0;
};

struct PaperDailyRow {
    std::string strategy_id;
    std::string trade_date;          // "YYYY-MM-DD"
    int         trades = 0;
    int         wins = 0;
    double      pnl_usd = 0.0;
    bool        halted = false;
    std::string halt_reason;
};

struct PaperControlRow {
    int64_t     id = 0;
    std::string strategy_id;
    std::string action;              // "disable" / "enable" / "flatten"
};

struct PaperAccountRow {
    std::string account_label;
    double starting_balance = 0.0;
    double equity           = 0.0;
    double peak_equity      = 0.0;
    double day_pnl          = 0.0;
    double day_start_equity = 0.0;
    std::string trade_date;          // "YYYY-MM-DD" or ""
    bool        halted = false;
    std::string halt_reason;
};

// ── PaperStore — sink interface used by PaperBroker (fakeable in tests) ──────
class PaperStore {
public:
    virtual ~PaperStore() = default;
    virtual void save_position(const PaperPositionRow& p) = 0;  // upsert
    virtual void record_trade(const PaperTradeRow& t) = 0;      // insert + NOTIFY
    virtual void record_signal(const PaperSignalRow&) {}        // paper_signals (optional)
};

// ── PaperDb ──────────────────────────────────────────────────────────────────
class PaperDb : public PaperStore {
public:
    explicit PaperDb(const std::string& connstr);
    ~PaperDb() override;
    PaperDb(const PaperDb&) = delete;
    PaperDb& operator=(const PaperDb&) = delete;

    bool ok() const { return conn_ && PQstatus(conn_) == CONNECTION_OK; }

    void ensure_schema();

    // PaperStore
    void save_position(const PaperPositionRow& p) override;
    void record_trade(const PaperTradeRow& t) override;  // insert + NOTIFY paper_update

    // Registry / resume
    void upsert_strategy(const std::string& id, const std::string& account_label,
                         const std::string& engine, const std::string& params_json,
                         bool enabled);
    std::optional<PaperPositionRow> load_position(const std::string& strategy_id);
    std::optional<PaperDailyRow>    load_daily(const std::string& strategy_id,
                                               const std::string& trade_date);
    std::optional<PaperAccountRow>  load_account(const std::string& account_label);

    void upsert_daily(const PaperDailyRow& d);
    void upsert_account(const PaperAccountRow& a);

    // Manual control channel (dashboard inserts rows, engine consumes them)
    // Audit/replay isolation: when set, the store records paper_trades ONLY —
    // no strategy/position/daily/account upserts, no control consumption, and
    // every load_* returns empty so a replay never inherits the live fleet's
    // day counts or positions (paper_strategies/positions are keyed by
    // strategy_id alone, so a replay would otherwise overwrite the live rows).
    void set_trades_only(bool v) { trades_only_ = v; }
    // Label every seeding query is scoped to. paper_trades is keyed by strategy_id
    // across labels (live + replay 'audit' rows share ids), so an unscoped SUM
    // would seed live P&L / trade counts / halts from audit replays.
    void set_account_label(const std::string& l) { account_label_ = l; }
    bool trades_only() const { return trades_only_; }

    std::vector<PaperControlRow> poll_control();
    void consume_control(int64_t id);
    std::optional<bool> load_enabled(const std::string& strategy_id);

    // Seeding helpers (history for RiskManager / strategy warmup)
    double sum_pnl(const std::string& strategy_id);                          // all time
    double sum_pnl_since(const std::string& strategy_id, int64_t since_us);  // today
    int    count_trades_since(const std::string& strategy_id, int64_t since_us);

    // Tick polling (shared live feed written by the collector)
    void record_signal(const PaperSignalRow& s) override;
    struct BboRow { int64_t ts_us; double bid, ask; int bid_sz, ask_sz; };
    // Quotes after `after_us` (one-sided rows forward-filled from the last seen side).
    std::vector<BboRow> poll_bbo(const std::string& symbol, int64_t after_us, int limit = 5000);
    struct TickRow { int64_t ts_us; double price; int64_t size; bool is_buy; };
    std::vector<TickRow> poll_ticks(const std::string& symbol, int64_t after_us,
                                    int limit = 5000);

    static std::string format_ts(int64_t ts_micros);  // → "YYYY-MM-DD HH:MM:SS.ffffff+00"

    void exec_silent(const std::string& sql);

private:
    PGconn* conn_ = nullptr;
    // Every query goes through live(): a dead connection (Postgres restarted — 2026-09-26 the
    // engine sat on one for 6 min, 9.6k failed writes) is PQreset() in place, at most once
    // per 5 s. Same idea as TickDB::reconnect() / OrbDB::reconnect() in the other processes.
    PGconn* live();
    std::chrono::steady_clock::time_point last_reset_{};
    bool    trades_only_       = false;  // replay/audit isolation (see set_trades_only)
    std::string account_label_;          // scope for the seeding queries (set_account_label)
    bool    poll_error_logged_ = false;  // rate-limit poll_ticks WARN spam
    bool    bbo_error_logged_  = false;
    double  last_bid_ = 0.0, last_ask_ = 0.0; int last_bid_sz_ = 0, last_ask_sz_ = 0;   // poll_bbo forward-fill
    bool    ctl_error_logged_  = false;  // rate-limit poll_control WARN spam

    std::string exec_scalar(const std::string& sql, const char* const* params,
                            int nparams, const char* ctx);
};

} // namespace paper
