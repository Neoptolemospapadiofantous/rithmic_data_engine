#pragma once
/*  ═══════════════════════════════════════════════════════════════════════════
    paper_db.hpp — PostgreSQL access for the paper-trading fleet engine

    Owns the paper_* tables (migrations/007_paper_fleet.sql). ensure_schema()
    is idempotent and runs at engine startup, same style as src/db.cpp.

    Also implements the PaperStore interface consumed by PaperBroker so the
    broker is testable without a database.
    ═══════════════════════════════════════════════════════════════════════════ */
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
    std::string exit_reason;
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

    // Seeding helpers (history for RiskManager / strategy warmup)
    double sum_pnl(const std::string& strategy_id);                          // all time
    double sum_pnl_since(const std::string& strategy_id, int64_t since_us);  // today
    int    count_trades_since(const std::string& strategy_id, int64_t since_us);

    // Tick polling (shared live feed written by the collector)
    struct TickRow { int64_t ts_us; double price; int64_t size; bool is_buy; };
    std::vector<TickRow> poll_ticks(const std::string& symbol, int64_t after_us,
                                    int limit = 5000);

    static std::string format_ts(int64_t ts_micros);  // → "YYYY-MM-DD HH:MM:SS.ffffff+00"

    void exec_silent(const std::string& sql);

private:
    PGconn* conn_ = nullptr;
    bool    poll_error_logged_ = false;  // rate-limit poll_ticks WARN spam

    std::string exec_scalar(const std::string& sql, const char* const* params,
                            int nparams, const char* ctx);
};

} // namespace paper
