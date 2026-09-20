/*  ═══════════════════════════════════════════════════════════════════════════
    paper_db.cpp — libpq implementation for the paper fleet tables
    ═══════════════════════════════════════════════════════════════════════════ */
#include "paper_db.hpp"
#include "log.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <stdexcept>

namespace paper {

static const char* kSchemaSQL[] = {
    R"sql(CREATE TABLE IF NOT EXISTS paper_strategies (
  strategy_id TEXT PRIMARY KEY,
  account_label TEXT NOT NULL,
  engine TEXT NOT NULL,
  params_json JSONB NOT NULL DEFAULT '{}',
  mode TEXT NOT NULL DEFAULT 'paper',
  enabled BOOLEAN NOT NULL DEFAULT TRUE,
  created_at TIMESTAMPTZ NOT NULL DEFAULT now()))sql",
    R"sql(CREATE TABLE IF NOT EXISTS paper_positions (
  strategy_id TEXT PRIMARY KEY REFERENCES paper_strategies(strategy_id),
  direction TEXT,
  qty INT NOT NULL DEFAULT 0,
  entry_price DOUBLE PRECISION,
  entry_time TIMESTAMPTZ,
  stop_price DOUBLE PRECISION,
  target_price DOUBLE PRECISION,
  unrealized_pnl DOUBLE PRECISION NOT NULL DEFAULT 0,
  updated_at TIMESTAMPTZ NOT NULL DEFAULT now()))sql",
    R"sql(CREATE TABLE IF NOT EXISTS paper_trades (
  id BIGSERIAL PRIMARY KEY,
  strategy_id TEXT NOT NULL REFERENCES paper_strategies(strategy_id),
  account_label TEXT NOT NULL,
  symbol TEXT NOT NULL,
  direction TEXT NOT NULL,
  qty INT NOT NULL,
  entry_time TIMESTAMPTZ NOT NULL,
  entry_price DOUBLE PRECISION NOT NULL,
  exit_time TIMESTAMPTZ,
  exit_price DOUBLE PRECISION,
  pnl_pts DOUBLE PRECISION,
  pnl_usd DOUBLE PRECISION,
  commission DOUBLE PRECISION,
  exit_reason TEXT,
  created_at TIMESTAMPTZ NOT NULL DEFAULT now()))sql",
    R"sql(CREATE INDEX IF NOT EXISTS idx_paper_trades_strat_time
  ON paper_trades(strategy_id, entry_time DESC))sql",
    // MAE/MFE columns (added 2026-09-20) — idempotent for existing tables
    R"sql(ALTER TABLE paper_trades ADD COLUMN IF NOT EXISTS mae_pts DOUBLE PRECISION)sql",
    R"sql(ALTER TABLE paper_trades ADD COLUMN IF NOT EXISTS mfe_pts DOUBLE PRECISION)sql",
    R"sql(CREATE TABLE IF NOT EXISTS paper_daily (
  strategy_id TEXT NOT NULL REFERENCES paper_strategies(strategy_id),
  trade_date DATE NOT NULL,
  trades INT NOT NULL DEFAULT 0,
  wins INT NOT NULL DEFAULT 0,
  pnl_usd DOUBLE PRECISION NOT NULL DEFAULT 0,
  halted BOOLEAN NOT NULL DEFAULT FALSE,
  halt_reason TEXT,
  PRIMARY KEY (strategy_id, trade_date)))sql",
    R"sql(CREATE TABLE IF NOT EXISTS paper_account (
  account_label TEXT PRIMARY KEY,
  starting_balance DOUBLE PRECISION NOT NULL,
  equity DOUBLE PRECISION NOT NULL,
  peak_equity DOUBLE PRECISION NOT NULL,
  day_pnl DOUBLE PRECISION NOT NULL DEFAULT 0,
  day_start_equity DOUBLE PRECISION NOT NULL,
  trade_date DATE,
  halted BOOLEAN NOT NULL DEFAULT FALSE,
  halt_reason TEXT,
  updated_at TIMESTAMPTZ NOT NULL DEFAULT now()))sql",
    R"sql(CREATE TABLE IF NOT EXISTS paper_control (
  id BIGSERIAL PRIMARY KEY,
  strategy_id TEXT NOT NULL,
  action TEXT NOT NULL,
  created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
  consumed_at TIMESTAMPTZ))sql",
    R"sql(CREATE INDEX IF NOT EXISTS idx_paper_control_pending
  ON paper_control(consumed_at))sql",
};

PaperDb::PaperDb(const std::string& connstr) {
    conn_ = PQconnectdb(connstr.c_str());
    if (PQstatus(conn_) != CONNECTION_OK) {
        std::string msg = PQerrorMessage(conn_);
        PQfinish(conn_);
        conn_ = nullptr;
        throw std::runtime_error("PostgreSQL connection failed: " + msg);
    }
    ensure_schema();
}

PaperDb::~PaperDb() {
    if (conn_) PQfinish(conn_);
}

std::string PaperDb::format_ts(int64_t ts_micros) {
    time_t secs   = static_cast<time_t>(ts_micros / 1'000'000);
    int    micros = static_cast<int>(ts_micros % 1'000'000);
    if (micros < 0) { micros += 1'000'000; --secs; }
    struct tm t;
    gmtime_r(&secs, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
    char result[48];
    std::snprintf(result, sizeof(result), "%s.%06d+00", buf, micros);
    return result;
}

void PaperDb::ensure_schema() {
    for (const char* sql : kSchemaSQL) {
        PGresult* res = PQexec(conn_, sql);
        if (!res || PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::string msg = res ? PQresultErrorMessage(res) : "null result";
            if (res) PQclear(res);
            throw std::runtime_error(std::string("paper schema ensure failed: ") + msg);
        }
        PQclear(res);
    }
}

void PaperDb::exec_silent(const std::string& sql) {
    PGresult* r = PQexec(conn_, sql.c_str());
    if (r) {
        auto s = PQresultStatus(r);
        if (s != PGRES_COMMAND_OK && s != PGRES_TUPLES_OK)
            LOG("[PAPER-DB] WARN exec_silent: %.120s", PQresultErrorMessage(r));
        PQclear(r);
    }
}

std::string PaperDb::exec_scalar(const std::string& sql, const char* const* params,
                                 int nparams, const char* ctx) {
    PGresult* res = PQexecParams(conn_, sql.c_str(), nparams, nullptr, params,
                                 nullptr, nullptr, 0);
    if (!res || PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::string msg = res ? PQresultErrorMessage(res) : "null result";
        if (res) PQclear(res);
        throw std::runtime_error(std::string("DB error [") + ctx + "]: " + msg);
    }
    std::string out;
    if (PQntuples(res) > 0 && !PQgetisnull(res, 0, 0))
        out = PQgetvalue(res, 0, 0);
    PQclear(res);
    return out;
}

// ── PaperStore ───────────────────────────────────────────────────────────────

void PaperDb::save_position(const PaperPositionRow& p) {
    std::string ts = p.entry_time_us > 0 ? format_ts(p.entry_time_us) : "";
    const char* dir = p.direction > 0 ? "LONG" : (p.direction < 0 ? "SHORT" : nullptr);
    std::string qty  = std::to_string(p.qty);
    std::string ep   = std::to_string(p.entry_price);
    std::string sp   = std::to_string(p.stop_price);
    std::string tp   = std::to_string(p.target_price);
    std::string upnl = std::to_string(p.unrealized_pnl);
    const char* params[8] = {
        p.strategy_id.c_str(), dir,
        qty.c_str(),
        p.direction != 0 ? ep.c_str() : nullptr,
        p.direction != 0 ? ts.c_str() : nullptr,
        p.direction != 0 ? sp.c_str() : nullptr,
        p.direction != 0 ? tp.c_str() : nullptr,
        upnl.c_str(),
    };
    PGresult* res = PQexecParams(conn_,
        R"sql(INSERT INTO paper_positions
              (strategy_id, direction, qty, entry_price, entry_time,
               stop_price, target_price, unrealized_pnl, updated_at)
              VALUES ($1,$2,$3::int,$4::float8,$5::timestamptz,$6::float8,$7::float8,$8::float8,now())
              ON CONFLICT (strategy_id) DO UPDATE SET
                direction=EXCLUDED.direction, qty=EXCLUDED.qty,
                entry_price=EXCLUDED.entry_price, entry_time=EXCLUDED.entry_time,
                stop_price=EXCLUDED.stop_price, target_price=EXCLUDED.target_price,
                unrealized_pnl=EXCLUDED.unrealized_pnl, updated_at=now())sql",
        8, nullptr, params, nullptr, nullptr, 0);
    if (!res || PQresultStatus(res) != PGRES_COMMAND_OK) {
        LOG("[PAPER-DB] WARN save_position(%s): %s", p.strategy_id.c_str(),
            res ? PQresultErrorMessage(res) : "null result");
    }
    if (res) PQclear(res);
}

void PaperDb::record_trade(const PaperTradeRow& t) {
    std::string ets = format_ts(t.entry_time_us);
    std::string xts = format_ts(t.exit_time_us);
    std::string qty  = std::to_string(t.qty);
    std::string ep   = std::to_string(t.entry_price);
    std::string xp   = std::to_string(t.exit_price);
    std::string pts  = std::to_string(t.pnl_pts);
    std::string usd  = std::to_string(t.pnl_usd);
    std::string comm = std::to_string(t.commission);
    std::string mae  = std::to_string(t.mae_pts);
    std::string mfe  = std::to_string(t.mfe_pts);
    const char* params[15] = {
        t.strategy_id.c_str(), t.account_label.c_str(), t.symbol.c_str(),
        t.direction.c_str(), qty.c_str(), ets.c_str(), ep.c_str(),
        xts.c_str(), xp.c_str(), pts.c_str(), usd.c_str(), comm.c_str(),
        t.exit_reason.c_str(), mae.c_str(), mfe.c_str(),
    };
    PGresult* res = PQexecParams(conn_,
        R"sql(INSERT INTO paper_trades
              (strategy_id, account_label, symbol, direction, qty,
               entry_time, entry_price, exit_time, exit_price,
               pnl_pts, pnl_usd, commission, exit_reason, mae_pts, mfe_pts)
              VALUES ($1,$2,$3,$4,$5::int,$6::timestamptz,$7::float8,
                      $8::timestamptz,$9::float8,$10::float8,$11::float8,$12::float8,$13,
                      $14::float8,$15::float8))sql",
        15, nullptr, params, nullptr, nullptr, 0);
    if (!res || PQresultStatus(res) != PGRES_COMMAND_OK) {
        LOG("[PAPER-DB] WARN record_trade(%s): %s", t.strategy_id.c_str(),
            res ? PQresultErrorMessage(res) : "null result");
        if (res) PQclear(res);
        return;  // skip NOTIFY on failed insert
    }
    PQclear(res);

    // Wake any listener (backend dashboard); non-fatal by design.
    const char* np[1] = { t.strategy_id.c_str() };
    PGresult* nr = PQexecParams(conn_,
        "SELECT pg_notify('paper_update', $1)", 1, nullptr, np, nullptr, nullptr, 0);
    if (nr) PQclear(nr);
}

// ── registry / resume ────────────────────────────────────────────────────────

void PaperDb::upsert_strategy(const std::string& id, const std::string& account_label,
                              const std::string& engine, const std::string& params_json,
                              bool enabled) {
    const char* en = enabled ? "true" : "false";
    const char* params[5] = { id.c_str(), account_label.c_str(), engine.c_str(),
                              params_json.c_str(), en };
    PGresult* res = PQexecParams(conn_,
        R"sql(INSERT INTO paper_strategies (strategy_id, account_label, engine, params_json, enabled)
              VALUES ($1,$2,$3,$4::jsonb,$5::boolean)
              ON CONFLICT (strategy_id) DO UPDATE SET
                account_label=EXCLUDED.account_label, engine=EXCLUDED.engine,
                params_json=EXCLUDED.params_json)sql",
        5, nullptr, params, nullptr, nullptr, 0);
    if (!res || PQresultStatus(res) != PGRES_COMMAND_OK)
        LOG("[PAPER-DB] WARN upsert_strategy(%s): %s", id.c_str(),
            res ? PQresultErrorMessage(res) : "null result");
    if (res) PQclear(res);
}

std::optional<PaperPositionRow> PaperDb::load_position(const std::string& strategy_id) {
    const char* params[1] = { strategy_id.c_str() };
    PGresult* res = PQexecParams(conn_,
        R"sql(SELECT direction, qty, entry_price,
                     (EXTRACT(EPOCH FROM entry_time)*1000000)::bigint,
                     stop_price, target_price, unrealized_pnl
              FROM paper_positions WHERE strategy_id=$1 AND qty > 0)sql",
        1, nullptr, params, nullptr, nullptr, 0);
    if (!res || PQresultStatus(res) != PGRES_TUPLES_OK) {
        LOG("[PAPER-DB] WARN load_position(%s): %s", strategy_id.c_str(),
            res ? PQresultErrorMessage(res) : "null result");
        if (res) PQclear(res);
        return std::nullopt;
    }
    std::optional<PaperPositionRow> out;
    if (PQntuples(res) > 0) {
        PaperPositionRow p;
        p.strategy_id    = strategy_id;
        std::string dir  = PQgetvalue(res, 0, 0);
        p.direction      = (dir == "LONG") ? 1 : -1;
        p.qty            = std::atoi(PQgetvalue(res, 0, 1));
        p.entry_price    = std::atof(PQgetvalue(res, 0, 2));
        p.entry_time_us  = std::atoll(PQgetvalue(res, 0, 3));
        p.stop_price     = std::atof(PQgetvalue(res, 0, 4));
        p.target_price   = PQgetisnull(res, 0, 5) ? 0.0 : std::atof(PQgetvalue(res, 0, 5));
        p.unrealized_pnl = std::atof(PQgetvalue(res, 0, 6));
        out = p;
    }
    PQclear(res);
    return out;
}

std::optional<PaperDailyRow> PaperDb::load_daily(const std::string& strategy_id,
                                                 const std::string& trade_date) {
    const char* params[2] = { strategy_id.c_str(), trade_date.c_str() };
    PGresult* res = PQexecParams(conn_,
        R"sql(SELECT trades, wins, pnl_usd, halted, COALESCE(halt_reason,'')
              FROM paper_daily WHERE strategy_id=$1 AND trade_date=$2::date)sql",
        2, nullptr, params, nullptr, nullptr, 0);
    if (!res || PQresultStatus(res) != PGRES_TUPLES_OK) {
        if (res) PQclear(res);
        return std::nullopt;
    }
    std::optional<PaperDailyRow> out;
    if (PQntuples(res) > 0) {
        PaperDailyRow d;
        d.strategy_id = strategy_id;
        d.trade_date  = trade_date;
        d.trades      = std::atoi(PQgetvalue(res, 0, 0));
        d.wins        = std::atoi(PQgetvalue(res, 0, 1));
        d.pnl_usd     = std::atof(PQgetvalue(res, 0, 2));
        d.halted      = std::strcmp(PQgetvalue(res, 0, 3), "t") == 0;
        d.halt_reason = PQgetvalue(res, 0, 4);
        out = d;
    }
    PQclear(res);
    return out;
}

std::optional<PaperAccountRow> PaperDb::load_account(const std::string& account_label) {
    const char* params[1] = { account_label.c_str() };
    PGresult* res = PQexecParams(conn_,
        R"sql(SELECT starting_balance, equity, peak_equity, day_pnl, day_start_equity,
                     COALESCE(to_char(trade_date,'YYYY-MM-DD'),''), halted,
                     COALESCE(halt_reason,'')
              FROM paper_account WHERE account_label=$1)sql",
        1, nullptr, params, nullptr, nullptr, 0);
    if (!res || PQresultStatus(res) != PGRES_TUPLES_OK) {
        if (res) PQclear(res);
        return std::nullopt;
    }
    std::optional<PaperAccountRow> out;
    if (PQntuples(res) > 0) {
        PaperAccountRow a;
        a.account_label    = account_label;
        a.starting_balance = std::atof(PQgetvalue(res, 0, 0));
        a.equity           = std::atof(PQgetvalue(res, 0, 1));
        a.peak_equity      = std::atof(PQgetvalue(res, 0, 2));
        a.day_pnl          = std::atof(PQgetvalue(res, 0, 3));
        a.day_start_equity = std::atof(PQgetvalue(res, 0, 4));
        a.trade_date       = PQgetvalue(res, 0, 5);
        a.halted           = std::strcmp(PQgetvalue(res, 0, 6), "t") == 0;
        a.halt_reason      = PQgetvalue(res, 0, 7);
        out = a;
    }
    PQclear(res);
    return out;
}

void PaperDb::upsert_daily(const PaperDailyRow& d) {
    std::string trades = std::to_string(d.trades);
    std::string wins   = std::to_string(d.wins);
    std::string pnl    = std::to_string(d.pnl_usd);
    const char* halted = d.halted ? "true" : "false";
    const char* params[6] = { d.strategy_id.c_str(), d.trade_date.c_str(),
                              trades.c_str(), wins.c_str(), pnl.c_str(), halted };
    // halt_reason only updated when present
    PGresult* res = PQexecParams(conn_,
        R"sql(INSERT INTO paper_daily (strategy_id, trade_date, trades, wins, pnl_usd, halted)
              VALUES ($1,$2::date,$3::int,$4::int,$5::float8,$6::boolean)
              ON CONFLICT (strategy_id, trade_date) DO UPDATE SET
                trades=EXCLUDED.trades, wins=EXCLUDED.wins,
                pnl_usd=EXCLUDED.pnl_usd, halted=EXCLUDED.halted)sql",
        6, nullptr, params, nullptr, nullptr, 0);
    if (!res || PQresultStatus(res) != PGRES_COMMAND_OK)
        LOG("[PAPER-DB] WARN upsert_daily(%s): %s", d.strategy_id.c_str(),
            res ? PQresultErrorMessage(res) : "null result");
    if (res) PQclear(res);
    if (!d.halt_reason.empty()) {
        const char* hp[3] = { d.halt_reason.c_str(), d.strategy_id.c_str(),
                              d.trade_date.c_str() };
        PGresult* hr = PQexecParams(conn_,
            "UPDATE paper_daily SET halt_reason=$1 WHERE strategy_id=$2 AND trade_date=$3::date",
            3, nullptr, hp, nullptr, nullptr, 0);
        if (hr) PQclear(hr);
    }
}

void PaperDb::upsert_account(const PaperAccountRow& a) {
    std::string sb  = std::to_string(a.starting_balance);
    std::string eq  = std::to_string(a.equity);
    std::string pk  = std::to_string(a.peak_equity);
    std::string dp  = std::to_string(a.day_pnl);
    std::string dse = std::to_string(a.day_start_equity);
    const char* halted = a.halted ? "true" : "false";
    const char* params[9] = {
        a.account_label.c_str(), sb.c_str(), eq.c_str(), pk.c_str(), dp.c_str(),
        dse.c_str(),
        a.trade_date.empty() ? nullptr : a.trade_date.c_str(),
        halted,
        a.halt_reason.empty() ? nullptr : a.halt_reason.c_str(),
    };
    PGresult* res = PQexecParams(conn_,
        R"sql(INSERT INTO paper_account
              (account_label, starting_balance, equity, peak_equity, day_pnl,
               day_start_equity, trade_date, halted, halt_reason, updated_at)
              VALUES ($1,$2::float8,$3::float8,$4::float8,$5::float8,$6::float8,
                      $7::date,$8::boolean,$9,now())
              ON CONFLICT (account_label) DO UPDATE SET
                starting_balance=EXCLUDED.starting_balance, equity=EXCLUDED.equity,
                peak_equity=EXCLUDED.peak_equity, day_pnl=EXCLUDED.day_pnl,
                day_start_equity=EXCLUDED.day_start_equity,
                trade_date=EXCLUDED.trade_date, halted=EXCLUDED.halted,
                halt_reason=EXCLUDED.halt_reason, updated_at=now())sql",
        9, nullptr, params, nullptr, nullptr, 0);
    if (!res || PQresultStatus(res) != PGRES_COMMAND_OK)
        LOG("[PAPER-DB] WARN upsert_account(%s): %s", a.account_label.c_str(),
            res ? PQresultErrorMessage(res) : "null result");
    if (res) PQclear(res);
}

// ── manual control channel ───────────────────────────────────────────────────

std::vector<PaperControlRow> PaperDb::poll_control() {
    PGresult* res = PQexec(conn_,
        "SELECT id, strategy_id, action FROM paper_control "
        "WHERE consumed_at IS NULL ORDER BY id");
    std::vector<PaperControlRow> out;
    if (!res || PQresultStatus(res) != PGRES_TUPLES_OK) {
        // Same rate-limit as poll_ticks: one WARN per error-state entry.
        if (!ctl_error_logged_) {
            LOG("[PAPER-DB] WARN poll_control: %s (further errors suppressed)",
                res ? PQresultErrorMessage(res) : "null result");
            ctl_error_logged_ = true;
        }
        if (res) PQclear(res);
        return out;
    }
    ctl_error_logged_ = false;
    int n = PQntuples(res);
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        PaperControlRow c;
        c.id          = std::atoll(PQgetvalue(res, i, 0));
        c.strategy_id = PQgetvalue(res, i, 1);
        c.action      = PQgetvalue(res, i, 2);
        out.push_back(c);
    }
    PQclear(res);
    return out;
}

void PaperDb::consume_control(int64_t id) {
    std::string sid = std::to_string(id);
    const char* params[1] = { sid.c_str() };
    PGresult* res = PQexecParams(conn_,
        "UPDATE paper_control SET consumed_at=now() WHERE id=$1::bigint",
        1, nullptr, params, nullptr, nullptr, 0);
    if (!res || PQresultStatus(res) != PGRES_COMMAND_OK)
        LOG("[PAPER-DB] WARN consume_control(%lld): %s", (long long)id,
            res ? PQresultErrorMessage(res) : "null result");
    if (res) PQclear(res);
}

std::optional<bool> PaperDb::load_enabled(const std::string& strategy_id) {
    const char* params[1] = { strategy_id.c_str() };
    PGresult* res = PQexecParams(conn_,
        "SELECT enabled FROM paper_strategies WHERE strategy_id=$1",
        1, nullptr, params, nullptr, nullptr, 0);
    if (!res || PQresultStatus(res) != PGRES_TUPLES_OK) {
        LOG("[PAPER-DB] WARN load_enabled(%s): %s", strategy_id.c_str(),
            res ? PQresultErrorMessage(res) : "null result");
        if (res) PQclear(res);
        return std::nullopt;
    }
    std::optional<bool> out;
    if (PQntuples(res) > 0)
        out = std::strcmp(PQgetvalue(res, 0, 0), "t") == 0;
    PQclear(res);
    return out;
}

// ── seeding queries ──────────────────────────────────────────────────────────

double PaperDb::sum_pnl(const std::string& strategy_id) {
    const char* params[1] = { strategy_id.c_str() };
    std::string v = exec_scalar(
        "SELECT COALESCE(SUM(pnl_usd),0) FROM paper_trades WHERE strategy_id=$1",
        params, 1, "sum_pnl");
    return v.empty() ? 0.0 : std::atof(v.c_str());
}

double PaperDb::sum_pnl_since(const std::string& strategy_id, int64_t since_us) {
    std::string ts = format_ts(since_us);
    const char* params[2] = { strategy_id.c_str(), ts.c_str() };
    std::string v = exec_scalar(
        "SELECT COALESCE(SUM(pnl_usd),0) FROM paper_trades "
        "WHERE strategy_id=$1 AND exit_time >= $2::timestamptz",
        params, 2, "sum_pnl_since");
    return v.empty() ? 0.0 : std::atof(v.c_str());
}

int PaperDb::count_trades_since(const std::string& strategy_id, int64_t since_us) {
    std::string ts = format_ts(since_us);
    const char* params[2] = { strategy_id.c_str(), ts.c_str() };
    std::string v = exec_scalar(
        "SELECT COUNT(*) FROM paper_trades "
        "WHERE strategy_id=$1 AND entry_time >= $2::timestamptz",
        params, 2, "count_trades_since");
    return v.empty() ? 0 : std::atoi(v.c_str());
}

// ── tick polling ─────────────────────────────────────────────────────────────

std::vector<PaperDb::TickRow> PaperDb::poll_ticks(const std::string& symbol,
                                                  int64_t after_us, int limit) {
    std::string ts  = format_ts(after_us);
    std::string lim = std::to_string(limit);
    const char* params[3] = { ts.c_str(), symbol.c_str(), lim.c_str() };
    PGresult* res = PQexecParams(conn_,
        R"sql(SELECT (EXTRACT(EPOCH FROM ts_event)*1000000)::bigint, price, size, is_buy
              FROM ticks
              WHERE ts_event > $1::timestamptz AND symbol = $2
              ORDER BY ts_event, seq
              LIMIT $3::int)sql",
        3, nullptr, params, nullptr, nullptr, 0);
    std::vector<TickRow> out;
    if (!res || PQresultStatus(res) != PGRES_TUPLES_OK) {
        // Log on error-state entry only — a missing table would otherwise
        // spam one WARN per poll (100ms) until the collector recreates it.
        if (!poll_error_logged_) {
            LOG("[PAPER-DB] WARN poll_ticks: %s (further errors suppressed)",
                res ? PQresultErrorMessage(res) : "null result");
            poll_error_logged_ = true;
        }
        if (res) PQclear(res);
        return out;
    }
    poll_error_logged_ = false;
    int n = PQntuples(res);
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        TickRow t;
        t.ts_us  = std::atoll(PQgetvalue(res, i, 0));
        t.price  = std::atof(PQgetvalue(res, i, 1));
        t.size   = std::atoll(PQgetvalue(res, i, 2));
        t.is_buy = std::strcmp(PQgetvalue(res, i, 3), "t") == 0;
        out.push_back(t);
    }
    PQclear(res);
    return out;
}

} // namespace paper
