// mm_paper — the 24/7 paper market maker on Lighter (founder 2026-10-25, Phase 1 of the market-making
// system). Reads Lighter's live public books and trades over its own connection, runs the frozen strategy
// through the queue model with the risk controls in mm_paper.hpp, and records everything. PAPER ONLY: this
// program has no keys and no order path.
//
//   mm_paper --symbols A,B,C [--kill-file data/mm_paper/KILL] [--status data/mm_paper/status.json]
//            [--daily-loss 150] [--latency-ms 300] [--tick-rule hl|book] [--label paper] [--reorder-ms 250] [--no-db]
//            [--duration SECONDS]
//
// --label names this instance in every table (column `label`, default 'paper'): the official engine runs as 'paper'
// at 300 ms; a shadow at the delay a server in Lighter's region would have runs as 'paper_220' beside it, so the
// value of the server is measured on the same live days instead of estimated from history (2026-10-25).
//
// Ticks: the strategy's distances are in HYPERLIQUID ticks (--tick-rule hl, the unit the setting was validated
// in; HL's size precision per coin is fetched from api.hyperliquid.xyz at start), while orders are rounded to
// Lighter's real grid (supported_price_decimals from /api/v1/orderBooks). A coin Hyperliquid does not list
// falls back to --tick-rule book (distances in Lighter's own ticks) and says so in the log.
//
// Postgres (.env, migrations/20261025_mm_paper.sql + 20261026_mm_paper_uid.sql — created IF NOT EXISTS): fills,
// minute equity, daily rows, events. One connection on a writer thread; the feed never waits for it. Statements run
// one by one and are retried after a lost connection (idempotent: fills/events carry a unique uid); every row that
// never reaches Postgres is counted per UTC day in data/mm_paper/db_drops_<label>.json (survives restarts; the G2
// gate requires zero drops on a day it counts).
// Event order: books and trades pass a 250 ms re-order buffer (--reorder-ms) and are applied in the backtest
// loader's order (exchange time, trades before books) — see mm_paper.hpp.
// Status file every 10 s: day, equity, gross inventory, halted, per-market state, DB drops (total + today). Touch the kill file to
// flatten and halt everything; delete it to resume. A restart starts flat (paper positions are not carried).
//
// Observability (2026-10-26; contract for the dashboard and scripts/mm_debug.sh):
//   * mm_paper_quotes (migrations/20261026_mm_paper_quotes.sql, created IF NOT EXISTS): what each market quotes and why —
//     ts, label, market, mid, best_bid, best_ask, bid_px/ask_px (NULL = no order), bid_reason/ask_reason (hl::reason_name:
//     quoting, no_book, halted, spread_narrow, jump_pause, imbalance_pull, inv_cap, kept), inv (coins), inv_usd, equity.
//     Written on a change of (bid_px, ask_px, bid_reason, ask_reason), ≤ 1 row/s per market, keepalive every 60 s
//     (mmpaper::QuoteTracker). Quote rows have their OWN bounded queue (50k, oldest shed first) and their OWN drop ledger
//     (db_quote_drops_<label>.json; status.json db.quote_*), so they can never cost a fill/event row nor fail the G2 gate; never pruned.
//   * status.json: per market bid_px, ask_px, bid_reason, ask_reason, reasons_bid/reasons_ask {reason: count of requote
//     decisions since the simulator started — 00:00 UTC or the engine start, whichever is later}, plus mid/best_bid/best_ask,
//     stale, inv_usd, last_book_ms and the tick settings (venue_tick, tick_rule, hl_sz_decimals) a replay needs; top-level
//     `components` {feed, reorder, sim, db, risk}, each with an `ok` verdict (mmpaper::components_json), and quote_rows.
//   * --env FILE: the .env holding PG_* (default ./.env). PGOPTIONS (libpq) is honoured, e.g. "-c search_path=mm_selftest"
//     for a test schema.

#include "config.hpp"
#include "dex/lighter_feed.hpp"
#include "dex/lighter_ws.hpp"
#include "dex/mm_paper.hpp"
#include "dex/mm_paper_db.hpp"

#include <libpq-fe.h>

#include <cmath>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace asio = boost::asio;
namespace fs = std::filesystem;
using json = nlohmann::json;

static void logf(const char* fmt, ...) {
    char msg[2048];
    va_list ap; va_start(ap, fmt); std::vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    std::time_t t = std::time(nullptr);
    char ts[32]; std::strftime(ts, sizeof ts, "[%Y-%m-%d %H:%M:%S]", std::localtime(&t));
    std::printf("%s [mm_paper] %s\n", ts, msg);
    std::fflush(stdout);
}

static std::string iso_ms(int64_t ms) {
    std::time_t s = static_cast<std::time_t>(ms / 1000);
    std::tm t{}; gmtime_r(&s, &t);
    char b[40]; std::strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", &t);
    char o[48]; std::snprintf(o, sizeof o, "%s.%03d+00", b, static_cast<int>(ms % 1000));
    return o;
}
static std::string num(double v) { char b[40]; std::snprintf(b, sizeof b, "%.12g", v); return b; }

// ── Postgres writer: statements with parameters, applied in order on its own thread ─────────────────
// Each statement runs on its own (mm_paper_db.hpp run_batch): a failing statement is logged with its error and
// counted as a drop, the rest still apply; a lost connection stops the batch, reconnects (backoff) and retries from
// the statement that was in flight — every insert is idempotent, so a retry cannot double count. Nothing is dropped
// silently: queue overflow, failed statements and rows abandoned at shutdown all land in the DropLedger, persisted
// next to the status file (a restart keeps the evidence) and shown in status.json.
class Writer {
public:
    using Stmt = mmpaper::Stmt;
    static constexpr size_t QUEUE_CAP = 200'000;
    // Quote rows (mm_paper_quotes, observability only) have their OWN bounded queue and their OWN drop ledger
    // (quote_ledger_file, db_quote_drops_<label>.json): during a long Postgres outage they shed oldest-first and can never
    // crowd fills / events / equity / daily rows out of QUEUE_CAP, nor count in the ledger the G2 gate reads.
    static constexpr size_t QUOTE_QUEUE_CAP = 50'000;
    Writer(std::string conn, fs::path ledger_file, fs::path quote_ledger_file)
        : conn_(std::move(conn)), ledger_file_(std::move(ledger_file)), qledger_file_(std::move(quote_ledger_file)) {
        for (auto [l, f] : {std::pair{&ledger_, &ledger_file_}, std::pair{&qledger_, &qledger_file_}}) {
            if (!l->load(*f) && fs::exists(*f)) logf("drop ledger %s unreadable — starting a new one today (earlier days unknown)", f->c_str());
            l->start(mmpaper::utc_day(now_ms()));
            if (!l->save(*f)) logf("drop ledger %s: cannot write", f->c_str());
        }
        th_ = std::thread([this] { run(); });
    }
    ~Writer() { { std::lock_guard lk(mu_); stop_ = true; } cv_.notify_one(); th_.join(); }
    void push(Stmt s) {
        std::lock_guard lk(mu_);
        if (q_.size() >= QUEUE_CAP) { drop_locked(s, "queue full"); return; }
        q_.push_back(std::move(s));
        cv_.notify_one();
    }
    void push_quote(Stmt s) {
        std::lock_guard lk(mu_);
        qshed_locked(qq_.push(std::move(s), qledger_, mmpaper::utc_day(now_ms())), "quote queue full");
        cv_.notify_one();
    }
    int64_t dropped_total() const { std::lock_guard lk(mu_); return ledger_.total(); }
    int64_t dropped_today() const { std::lock_guard lk(mu_); return ledger_.on(mmpaper::utc_day(now_ms())); }
    int64_t errors() const { return errors_; }
    int64_t retries() const { return retries_; }
    int64_t last_commit_ms() const { return last_commit_ms_; }   // wall clock of the last batch with ≥ 1 statement applied; 0 = none yet
    size_t queued() const { std::lock_guard lk(mu_); return q_.size(); }
    // quote rows: dropped (shed or failed), queued, inserted, and inserts that hit an existing (label, market, ts) row
    int64_t quote_dropped_total() const { std::lock_guard lk(mu_); return qledger_.total(); }
    int64_t quote_dropped_today() const { std::lock_guard lk(mu_); return qledger_.on(mmpaper::utc_day(now_ms())); }
    size_t quote_queued() const { std::lock_guard lk(mu_); return qq_.size(); }
    int64_t quote_written() const { return quote_written_; }
    int64_t quote_conflicts() const { return quote_conflicts_; }
    int64_t quote_errors() const { return quote_errors_; }

private:
    static int64_t now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }
    // mu_ held. Counted against the row's own UTC day (its timestamp), else the drop's wall-clock day; the file is
    // written by the writer thread (or at shutdown).
    void drop_locked(const Stmt& s, const char* why) {
        const std::string day = mmpaper::drop_day(s, mmpaper::utc_day(now_ms()));
        ledger_.add(day, 1);
        if (++drop_logs_ <= 20 || drop_logs_ % 1000 == 0)
            logf("postgres: row DROPPED (%s, day %s) — %lld dropped in total", why, day.c_str(), (long long)ledger_.total());
    }
    // mu_ held. `n` quote rows were shed / dropped (already counted in qledger_ by ShedQueue, or by the caller).
    void qshed_locked(size_t n, const char* why) {
        if (!n) return;
        qdrop_logs_ += static_cast<int64_t>(n);
        if (qdrop_logs_ <= 20 || qdrop_logs_ % 10'000 < static_cast<int64_t>(n))
            logf("postgres: %zu quote row(s) dropped (%s) — %lld quote rows dropped in total (fills/events unaffected)", n, why, (long long)qledger_.total());
    }
    void persist() {
        std::lock_guard lk(mu_);
        if (ledger_.dirty() && !ledger_.save(ledger_file_)) logf("drop ledger %s: cannot write", ledger_file_.c_str());
        if (qledger_.dirty() && !qledger_.save(qledger_file_)) logf("drop ledger %s: cannot write", qledger_file_.c_str());
    }
    void requeue_quotes(std::vector<Stmt>& v, size_t from) {
        std::lock_guard lk(mu_);
        qshed_locked(qq_.requeue(v, from, qledger_, mmpaper::utc_day(now_ms())), "quote queue full while Postgres was unavailable");
    }
    // Put unwritten statements back at the FRONT (they keep their order); anything beyond the cap is a counted drop.
    void requeue(std::vector<Stmt>& v, size_t from) {
        std::lock_guard lk(mu_);
        for (size_t i = v.size(); i-- > from;) q_.push_front(std::move(v[i]));
        while (q_.size() > QUEUE_CAP) { drop_locked(q_.back(), "queue full while Postgres was unavailable"); q_.pop_back(); }
    }
    static std::string brief(const Stmt& s) {
        std::string o = s.sql.substr(0, 60) + " [";
        for (size_t i = 0; i < s.params.size(); ++i) o += (i ? "," : "") + s.params[i].substr(0, 40);
        return o + "]";
    }

    static constexpr const char* DDL = R"(
CREATE TABLE IF NOT EXISTS mm_paper_fills (id BIGSERIAL PRIMARY KEY, label TEXT NOT NULL DEFAULT 'paper', ts TIMESTAMPTZ NOT NULL,
  market TEXT NOT NULL, side SMALLINT NOT NULL, price DOUBLE PRECISION NOT NULL, qty DOUBLE PRECISION NOT NULL,
  mid DOUBLE PRECISION NOT NULL, taker BOOLEAN NOT NULL, inv_after DOUBLE PRECISION NOT NULL, run_id TEXT NOT NULL);
CREATE INDEX IF NOT EXISTS mm_paper_fills_ts ON mm_paper_fills (ts);
CREATE TABLE IF NOT EXISTS mm_paper_equity (label TEXT NOT NULL DEFAULT 'paper', ts TIMESTAMPTZ NOT NULL, market TEXT NOT NULL,
  equity DOUBLE PRECISION NOT NULL, inv DOUBLE PRECISION NOT NULL, fills INTEGER NOT NULL, halted BOOLEAN NOT NULL,
  PRIMARY KEY (label, ts, market));
CREATE TABLE IF NOT EXISTS mm_paper_daily (label TEXT NOT NULL DEFAULT 'paper', day DATE NOT NULL, market TEXT NOT NULL,
  pnl DOUBLE PRECISION NOT NULL, volume DOUBLE PRECISION NOT NULL, fills INTEGER NOT NULL, flattens INTEGER NOT NULL,
  capture_bps DOUBLE PRECISION NOT NULL, mk10_bps DOUBLE PRECISION NOT NULL, max_dd DOUBLE PRECISION NOT NULL,
  PRIMARY KEY (label, day, market));
CREATE TABLE IF NOT EXISTS mm_paper_events (id BIGSERIAL PRIMARY KEY, label TEXT NOT NULL DEFAULT 'paper', ts TIMESTAMPTZ NOT NULL,
  kind TEXT NOT NULL, market TEXT NOT NULL, detail TEXT NOT NULL);
ALTER TABLE mm_paper_fills  ADD COLUMN IF NOT EXISTS label TEXT NOT NULL DEFAULT 'paper';
ALTER TABLE mm_paper_equity ADD COLUMN IF NOT EXISTS label TEXT NOT NULL DEFAULT 'paper';
ALTER TABLE mm_paper_daily  ADD COLUMN IF NOT EXISTS label TEXT NOT NULL DEFAULT 'paper';
ALTER TABLE mm_paper_events ADD COLUMN IF NOT EXISTS label TEXT NOT NULL DEFAULT 'paper';
ALTER TABLE mm_paper_fills  ADD COLUMN IF NOT EXISTS uid TEXT;
ALTER TABLE mm_paper_events ADD COLUMN IF NOT EXISTS uid TEXT;
CREATE UNIQUE INDEX IF NOT EXISTS mm_paper_fills_uid  ON mm_paper_fills (uid);
CREATE UNIQUE INDEX IF NOT EXISTS mm_paper_events_uid ON mm_paper_events (uid);
CREATE TABLE IF NOT EXISTS mm_paper_quotes (ts TIMESTAMPTZ NOT NULL, label TEXT NOT NULL, market TEXT NOT NULL,
  mid DOUBLE PRECISION NOT NULL, best_bid DOUBLE PRECISION NOT NULL, best_ask DOUBLE PRECISION NOT NULL,
  bid_px DOUBLE PRECISION NULL, ask_px DOUBLE PRECISION NULL, bid_reason TEXT NOT NULL, ask_reason TEXT NOT NULL,
  inv DOUBLE PRECISION NOT NULL, inv_usd DOUBLE PRECISION NOT NULL, equity DOUBLE PRECISION NOT NULL,
  PRIMARY KEY (label, market, ts));)";

    // Connect, session settings, then the DDL one statement at a time under a 2 s lock_timeout (an ALTER waiting
    // behind another session's open transaction — e.g. an older mm_paper's batch — gives up instead of queueing every
    // writer behind it). Returns "" on success, else why; the caller then drops the connection and backs off.
    std::string open_session(PGconn*& c) {
        c = PQconnectdb(conn_.c_str());
        if (PQstatus(c) != CONNECTION_OK) return PQerrorMessage(c);
        for (const char* set : {"SET synchronous_commit = off", "SET client_min_messages = warning", "SET lock_timeout = '2s'"}) {
            PGresult* r = PQexec(c, set);
            const bool ok = PQresultStatus(r) == PGRES_COMMAND_OK;
            PQclear(r);
            if (!ok) return std::string("session setup: ") + PQerrorMessage(c);
        }
        for (const auto& st : mmpaper::split_sql(DDL)) {
            PGresult* r = PQexec(c, st.c_str());
            const bool ok = PQresultStatus(r) == PGRES_COMMAND_OK;
            PQclear(r);
            if (!ok) return "DDL failed (" + st.substr(0, 60) + "): " + PQerrorMessage(c);
        }
        PGresult* r = PQexec(c, "SET lock_timeout = 0");   // the inserts themselves wait normally
        PQclear(r);
        return "";
    }

    void run() {
        PGconn* c = nullptr;
        int backoff = 1;
        auto exec = [&](const std::string& sql, const std::vector<std::string>& params) -> mmpaper::ExecResult {
            std::vector<const char*> p;
            for (const auto& x : params) p.push_back(x.c_str());
            PGresult* r = PQexecParams(c, sql.c_str(), static_cast<int>(p.size()), nullptr, p.data(), nullptr, nullptr, 0);
            const ExecStatusType st = r ? PQresultStatus(r) : PGRES_FATAL_ERROR;
            const char* ct = r ? PQcmdTuples(r) : "";
            const int64_t rows = ct && *ct ? std::atoll(ct) : -1;
            if (r) PQclear(r);
            if (st == PGRES_COMMAND_OK || st == PGRES_TUPLES_OK) return {mmpaper::ExecStatus::Ok, "", rows};
            const std::string err = PQerrorMessage(c);
            return {PQstatus(c) == CONNECTION_BAD ? mmpaper::ExecStatus::ConnLost : mmpaper::ExecStatus::Error, err};
        };
        while (true) {
            std::vector<Stmt> batch, qbatch;
            bool stopping;
            {
                std::unique_lock lk(mu_);
                cv_.wait_for(lk, std::chrono::seconds(2), [&] { return stop_ || !q_.empty() || !qq_.empty(); });
                stopping = stop_;
                if (stopping && q_.empty() && qq_.empty()) break;
                batch.assign(std::make_move_iterator(q_.begin()), std::make_move_iterator(q_.end()));
                q_.clear();
                qbatch = qq_.take_all();
            }
            if (batch.empty() && qbatch.empty()) { persist(); continue; }
            if (!c || PQstatus(c) != CONNECTION_OK) {
                if (c) PQfinish(c);
                c = nullptr;
                const std::string why = open_session(c);
                if (!why.empty()) {   // no connection, or its DDL failed: never write on it (the uid index may be missing)
                    logf("postgres: %s — %zu row(s) + %zu quote row(s) waiting, retrying in %d s", why.c_str(), batch.size(), qbatch.size(), backoff);
                    if (c) PQfinish(c);
                    c = nullptr;
                    if (stopping) {   // shutting down with Postgres away: these rows are lost, and counted
                        std::lock_guard lk(mu_);
                        for (const auto& s : batch) drop_locked(s, "Postgres unavailable at shutdown");
                        for (const auto& s : q_) drop_locked(s, "Postgres unavailable at shutdown");
                        q_.clear();
                        const std::string wd = mmpaper::utc_day(now_ms());
                        for (const auto& s : qbatch) qledger_.add(mmpaper::drop_day(s, wd), 1);
                        for (const auto& s : qq_.take_all()) qledger_.add(mmpaper::drop_day(s, wd), 1);
                        break;
                    }
                    requeue(batch, 0);
                    requeue_quotes(qbatch, 0);
                    persist();
                    std::this_thread::sleep_for(std::chrono::seconds(backoff));
                    backoff = std::min(backoff * 2, 60);
                    continue;
                }
                backoff = 1;
            }
            // The rows the gate counts first, quote rows after; a lost connection puts back whatever was not written.
            const auto o = mmpaper::run_batch(batch, exec);
            if (o.applied > 0) last_commit_ms_ = now_ms();
            if (!o.failed.empty()) {
                errors_ += static_cast<int64_t>(o.failed.size());
                for (const auto& [i, err] : o.failed) logf("postgres: statement failed, row dropped: %s — %s", err.c_str(), brief(batch[i]).c_str());
                std::lock_guard lk(mu_);
                for (const auto& [i, err] : o.failed) drop_locked(batch[i], "statement failed");
            }
            if (o.conn_lost) {
                ++retries_;
                logf("postgres: connection lost mid-batch (%s) — reconnecting, %zu row(s) will be retried", o.conn_error.c_str(), batch.size() - o.resume_at);
                PQfinish(c); c = nullptr;
                requeue(batch, o.resume_at);
                requeue_quotes(qbatch, 0);
                persist();
                continue;
            }
            const auto oq = mmpaper::run_batch(qbatch, exec);
            if (oq.applied > 0) last_commit_ms_ = now_ms();
            quote_written_ += static_cast<int64_t>(oq.applied - oq.no_rows.size());
            quote_conflicts_ += static_cast<int64_t>(oq.no_rows.size());
            if (!oq.failed.empty()) {
                quote_errors_ += static_cast<int64_t>(oq.failed.size());
                logf("postgres: %zu quote statement(s) failed, e.g. %s — %s", oq.failed.size(), oq.failed[0].second.c_str(), brief(qbatch[oq.failed[0].first]).c_str());
                std::lock_guard lk(mu_);
                const std::string wd = mmpaper::utc_day(now_ms());
                for (const auto& [i, err] : oq.failed) qledger_.add(mmpaper::drop_day(qbatch[i], wd), 1);
                qshed_locked(oq.failed.size(), "quote statement failed");
            }
            if (oq.conn_lost) {
                ++retries_;
                logf("postgres: connection lost during quote rows (%s) — reconnecting, %zu quote row(s) will be retried", oq.conn_error.c_str(), qbatch.size() - oq.resume_at);
                PQfinish(c); c = nullptr;
                requeue_quotes(qbatch, oq.resume_at);
            }
            persist();
        }
        persist();
        if (c) PQfinish(c);
    }

    std::string conn_;
    fs::path ledger_file_;
    fs::path qledger_file_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Stmt> q_;
    mmpaper::DropLedger ledger_;
    mmpaper::DropLedger qledger_;
    mmpaper::ShedQueue qq_{QUOTE_QUEUE_CAP};
    int64_t qdrop_logs_ = 0;
    std::atomic<int64_t> quote_written_{0}, quote_conflicts_{0}, quote_errors_{0};
    int64_t drop_logs_ = 0;
    bool stop_ = false;
    std::atomic<int64_t> errors_{0}, retries_{0}, last_commit_ms_{0};
    std::thread th_;
};

int main(int argc, char** argv) {
    std::vector<std::string> symbols;
    fs::path kill_file = "data/mm_paper/KILL", status_file = "data/mm_paper/status.json";
    double daily_loss = 150;
    int64_t latency_ms = 300;
    std::string label = "paper";
    bool tick_rule_hl = true;
    bool use_db = true;
    int duration_s = 0;
    int64_t reorder_ms = 250;
    fs::path env_file = ".env";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto val = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--symbols") { std::stringstream ss(val()); for (std::string s; std::getline(ss, s, ',');) if (!s.empty()) symbols.push_back(s); }
        else if (a == "--kill-file") kill_file = val();
        else if (a == "--status") status_file = val();
        else if (a == "--daily-loss") daily_loss = std::atof(val().c_str());
        else if (a == "--latency-ms") latency_ms = std::atoll(val().c_str());
        else if (a == "--label") label = val();
        else if (a == "--tick-rule") { const std::string v = val(); if (v != "hl" && v != "book") { std::fprintf(stderr, "--tick-rule hl|book\n"); return 2; } tick_rule_hl = v == "hl"; }
        else if (a == "--no-db") use_db = false;
        else if (a == "--reorder-ms") reorder_ms = std::atoll(val().c_str());
        else if (a == "--duration") duration_s = std::atoi(val().c_str());
        else if (a == "--env") env_file = val();
        else {
            std::fprintf(stderr, "usage: mm_paper --symbols A,B,C [--kill-file F] [--status F] [--daily-loss USD] [--latency-ms N] [--tick-rule hl|book] [--label NAME] [--reorder-ms N] [--no-db] [--duration S] [--env FILE]\n");
            return 2;
        }
    }
    if (symbols.empty()) { std::fprintf(stderr, "--symbols is required\n"); return 2; }
    std::error_code ec;
    fs::create_directories(status_file.parent_path(), ec);

    std::unique_ptr<Writer> db;
    if (use_db) {
        Config cfg = Config::from_env(env_file.string());
        if (cfg.pg_password.empty()) { logf("PG_PASSWORD not set (%s) — use --no-db to run without Postgres", env_file.c_str()); return 1; }
        db = std::make_unique<Writer>("host=" + Config::pg_value(cfg.pg_host) + " port=" + Config::pg_value(cfg.pg_port) +
                                      " dbname=" + Config::pg_value(cfg.pg_db) + " user=" + Config::pg_value(cfg.pg_user) +
                                      " password=" + Config::pg_value(cfg.pg_password) + " connect_timeout=10 application_name=mm_paper",
                                      status_file.parent_path() / ("db_drops_" + label + ".json"),
                                      status_file.parent_path() / ("db_quote_drops_" + label + ".json"));
    }
    const std::string run_id = std::to_string(lighter::now_us() / 1000);
    const int64_t started_ms = lighter::now_us() / 1000;

    mmpaper::Config pc;
    pc.p = mmpaper::frozen_params();
    pc.p.latency_ms = latency_ms;
    pc.daily_loss_usd = daily_loss;
    pc.reorder_ms = reorder_ms;
    mmpaper::Portfolio pf(pc);          // per_market is filled once the markets are resolved (below), before any book
    int64_t fills = 0, events = 0;
    auto fill_fn = [&](const std::string& m, const hl::Fill& f, double inv) {
        ++fills;
        if (db) db->push({"INSERT INTO mm_paper_fills (ts, market, side, price, qty, mid, taker, inv_after, run_id, label, uid) "
                          "VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11) ON CONFLICT (uid) DO NOTHING",
                          {iso_ms(f.t_ms), m, std::to_string(f.side), num(f.px), num(f.qty), num(f.mid), f.taker ? "true" : "false", num(inv), run_id, label,
                           label + ":" + run_id + ":f" + std::to_string(fills)}, mmpaper::utc_day(f.t_ms)});
    };
    auto event_fn = [&](int64_t t, const std::string& kind, const std::string& m, const std::string& d) {
        ++events;
        logf("event %s %s %s", kind.c_str(), m.c_str(), d.c_str());
        if (db) db->push({"INSERT INTO mm_paper_events (ts, kind, market, detail, label, uid) VALUES ($1,$2,$3,$4,$5,$6) ON CONFLICT (uid) DO NOTHING",
                          {iso_ms(t), kind, m, d, label, label + ":" + run_id + ":e" + std::to_string(events)}, mmpaper::utc_day(t)});
    };
    auto day_fn = [&](const mmpaper::DayRow& r) {
        logf("day %s %s: P&L %.2f, %lld fills, $%.0f traded", r.day.c_str(), r.market.c_str(), r.pnl, (long long)r.fills, r.volume);
        if (db) db->push({"INSERT INTO mm_paper_daily (day, market, pnl, volume, fills, flattens, capture_bps, mk10_bps, max_dd, label) "
                          "VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10) ON CONFLICT (label, day, market) DO UPDATE SET pnl = EXCLUDED.pnl, volume = EXCLUDED.volume, "
                          "fills = EXCLUDED.fills, flattens = EXCLUDED.flattens, capture_bps = EXCLUDED.capture_bps, mk10_bps = EXCLUDED.mk10_bps, max_dd = EXCLUDED.max_dd",
                          {r.day, r.market, num(r.pnl), num(r.volume), std::to_string(r.fills), std::to_string(r.flattens), num(r.capture_bps), num(r.mk10_bps), num(r.max_dd), label}, r.day});
    };
    // Quote rows (mm_paper_quotes): on a change of (bid_px, ask_px, bid_reason, ask_reason), ≤ 1 row/s per market, plus a
    // keepalive every 60 s (mmpaper::QuoteTracker). Time = the exchange time of the event that produced the decision;
    // on the 1 s tick (keepalives, halts on a quiet market) the wall clock minus the re-order hold.
    mmpaper::QuoteTracker quotes;
    auto quote_row = [&](const std::string& m, int64_t t) {
        const auto r = quotes.observe(m, t, pf.quote_state(m));
        if (!r || !db) return;
        const auto& q = r->q;
        auto opt = [](double v) { return v > 0 ? num(v) : std::string(); };
        db->push_quote({"INSERT INTO mm_paper_quotes (ts, label, market, mid, best_bid, best_ask, bid_px, ask_px, bid_reason, ask_reason, inv, inv_usd, equity) "
                  "VALUES ($1,$2,$3,$4,$5,$6,NULLIF($7,'')::float8,NULLIF($8,'')::float8,$9,$10,$11,$12,$13) ON CONFLICT (label, market, ts) DO NOTHING",
                  {iso_ms(r->ts_ms), label, m, num(q.mid), num(q.best_bid), num(q.best_ask), opt(q.bid_px), opt(q.ask_px),
                   hl::reason_name(q.bid_reason), hl::reason_name(q.ask_reason), num(q.inv), num(q.inv_usd), num(q.equity)},
                  mmpaper::utc_day(r->ts_ms)});
    };
    auto applied_fn = [&](const std::string& m, int64_t t) { quote_row(m, t); };
    pf.on_fill(fill_fn); pf.on_event(event_fn); pf.on_day(day_fn); pf.on_applied(applied_fn);

    asio::io_context ioc(1);
    lighter::ssl::context ctx(lighter::ssl::context::tls_client);
    ctx.set_default_verify_paths();
    ctx.set_verify_mode(lighter::ssl::verify_peer);

    std::map<int, std::pair<std::string, lighter::Book>> mk;   // market_id → (symbol, book)
    std::vector<int> id_list;
    int64_t books = 0, trades = 0, gaps = 0, reconnects = 0;
    bool connected = false, ever_connected = false;   // the feed cannot be "down" before its first connection
    int64_t last_msg_us = 0;
    auto handle = [&](const json& m, const std::string&, int64_t recv) {
        last_msg_us = recv;
        if (int id = lighter::market_of(m, "order_book"); id >= 0) {
            auto it = mk.find(id);
            if (it == mk.end()) return;
            const auto r = it->second.second.apply(m);
            if (r == lighter::Book::Apply::Gap) { ++gaps; throw std::runtime_error("sequence gap on " + it->second.first); }
            if (it->second.second.valid()) { ++books; pf.push_book(it->second.first, it->second.second.top(20), recv / 1000); }
        } else if (int tid = lighter::market_of(m, "trade"); tid >= 0) {
            auto it = mk.find(tid);
            if (it == mk.end()) return;
            for (const auto& t : lighter::parse_trades(m)) { ++trades; pf.push_trade(it->second.first, t, recv / 1000); }
        }
        pf.pump(recv / 1000);
    };

    asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {
        std::map<std::string, lighter::MarketInfo> ids;
        std::map<std::string, int> hl_sz;
        while (ids.empty()) {
            bool failed = false;
            try { ids = co_await lighter::resolve_markets(ctx, symbols); }
            catch (const std::exception& e) { logf("market lookup failed (%s) — retrying in 30 s", e.what()); failed = true; }
            if (failed || ids.empty()) co_await lighter::sleep_s(30);   // (no co_await inside a handler)
        }
        if (tick_rule_hl) {
            for (int attempt = 0; attempt < 5 && hl_sz.empty(); ++attempt) {
                bool failed = false;
                try { hl_sz = co_await lighter::hyperliquid_sz_decimals(ctx); }
                catch (const std::exception& e) { logf("Hyperliquid meta failed (%s) — retrying in 15 s", e.what()); failed = true; }
                if (failed || hl_sz.empty()) co_await lighter::sleep_s(15);
            }
            if (hl_sz.empty()) logf("Hyperliquid meta unavailable: every market falls back to Lighter's own ticks (--tick-rule book)");
        }
        for (const auto& s : symbols) if (!ids.count(s)) logf("not an active Lighter perp, skipped: %s", s.c_str());
        for (const auto& [s, info] : ids) {
            mk[info.id] = {s, {}};
            id_list.push_back(info.id);
            hl::Params q = pc.p;
            q.venue_tick = std::pow(10.0, -info.price_decimals);
            auto hz = hl_sz.find(s);
            if (tick_rule_hl && hz != hl_sz.end()) { q.strat_tick_rule = hl::TickRule::Hyperliquid; q.hl_sz_decimals = hz->second; }
            else if (tick_rule_hl) logf("%s: not on Hyperliquid — distances in Lighter ticks for this market", s.c_str());
            pc.per_market[s] = q;
            logf("%s: market %d, Lighter grid %.*f, strategy tick %s", s.c_str(), info.id, info.price_decimals, q.venue_tick,
                 q.strat_tick_rule == hl::TickRule::Hyperliquid ? ("Hyperliquid (szDecimals " + std::to_string(q.hl_sz_decimals) + ")").c_str() : "Lighter grid");
        }
        pf = mmpaper::Portfolio(pc);
        pf.on_fill(fill_fn); pf.on_event(event_fn); pf.on_day(day_fn); pf.on_applied(applied_fn);
        pf.tick(lighter::now_us() / 1000, false);
        if (db) db->push({"INSERT INTO mm_paper_events (ts, kind, market, detail, label, uid) VALUES ($1,$2,$3,$4,$5,$6) ON CONFLICT (uid) DO NOTHING",
                          {iso_ms(lighter::now_us() / 1000), "start", "", "paper engine started on " + std::to_string(ids.size()) + " Lighter markets (run " + run_id +
                           ", " + std::to_string(latency_ms) + " ms)", label, label + ":" + run_id + ":start"}, mmpaper::utc_day(lighter::now_us() / 1000)});
        logf("paper trading %zu Lighter markets (frozen setting, %s ticks, %lld ms delay, daily loss stop $%.0f)", ids.size(),
             tick_rule_hl ? "Hyperliquid" : "Lighter", (long long)latency_ms, daily_loss);
        int backoff = 1;
        while (true) {
            const auto t0 = std::chrono::steady_clock::now();
            std::string why;
            for (auto& [id, x] : mk) x.second = lighter::Book{};
            connected = ever_connected = true;
            last_msg_us = lighter::now_us();        // the 15 s feed clock starts at connect, not at the first message
            try { co_await lighter::session(ctx, id_list, handle); why = "closed"; }
            catch (const std::exception& e) { why = e.what(); }
            connected = false;
            if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(60)) backoff = 1;
            ++reconnects;
            logf("disconnected (%s) — reconnecting in %d s", why.c_str(), backoff);
            co_await lighter::sleep_s(backoff);
            backoff = std::min(backoff * 2, 60);
        }
    }, [](std::exception_ptr e) { if (e) try { std::rethrow_exception(e); } catch (const std::exception& x) { logf("task ended: %s", x.what()); } });

    // The re-order buffer also drains on a 50 ms clock, so a quiet connection never holds an event much past its time.
    asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {
        asio::steady_timer t(co_await asio::this_coro::executor);
        while (true) {
            t.expires_after(std::chrono::milliseconds(50));
            co_await t.async_wait(asio::use_awaitable);
            pf.pump(lighter::now_us() / 1000);
        }
    }, asio::detached);

    // Risk tick every second; minute equity rows; status file every 10 s.
    asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {
        asio::steady_timer t(co_await asio::this_coro::executor);
        int64_t last_minute = 0;
        for (int n = 1;; ++n) {
            t.expires_after(std::chrono::seconds(1));
            co_await t.async_wait(asio::use_awaitable);
            const int64_t now = lighter::now_us() / 1000;
            const bool kill = fs::exists(kill_file, ec);
            pf.tick(now, kill, !ever_connected || (connected && now * 1000 - last_msg_us < 15'000'000));
            for (const auto& m : pf.markets()) quote_row(m, now - reorder_ms);   // deferred changes, halts, keepalives
            const int64_t minute = now / 60'000 * 60'000;
            if (db && minute != last_minute) {
                last_minute = minute;
                double eq = 0, inv_usd = 0; int64_t f = 0;
                for (const auto& s : pf.snapshot()) {
                    db->push({"INSERT INTO mm_paper_equity (ts, market, equity, inv, fills, halted, label) VALUES ($1,$2,$3,$4,$5,$6,$7) ON CONFLICT DO NOTHING",
                              {iso_ms(minute), s.market, num(s.equity), num(s.inv), std::to_string(s.fills), s.halted ? "true" : "false", label}, mmpaper::utc_day(minute)});
                    eq += s.equity; f += s.fills;
                }
                inv_usd = pf.gross_usd();
                db->push({"INSERT INTO mm_paper_equity (ts, market, equity, inv, fills, halted, label) VALUES ($1,'_total',$2,$3,$4,$5,$6) ON CONFLICT DO NOTHING",
                          {iso_ms(minute), num(eq), num(inv_usd), std::to_string(f), pf.halted() ? "true" : "false", label}, mmpaper::utc_day(minute)});
            }
            if (n % 10 == 0) {
                json st = {{"updated", iso_ms(now)}, {"label", label}, {"latency_ms", latency_ms}, {"day", pf.day()}, {"connected", connected}, {"equity_usd", pf.equity()},
                           {"gross_inventory_usd", pf.gross_usd()}, {"halted", pf.halted()}, {"books", books}, {"trades", trades},
                           {"fills", fills}, {"events", events}, {"gaps", gaps}, {"reconnects", reconnects},
                           {"db_dropped", db ? db->dropped_total() : 0}, {"db_dropped_today", db ? db->dropped_today() : 0},
                           {"db_errors", db ? db->errors() : 0}, {"db_retries", db ? db->retries() : 0}, {"db_queue", db ? db->queued() : 0},
                           {"reorder_ms", reorder_ms}, {"reorder_pending", pf.sequencer().pending()}, {"late_trades", pf.sequencer().late_trades()},
                           {"quote_rows", quotes.rows()}, {"quote_keepalives", quotes.keepalives()}, {"markets", pf.market_status()}};
                mmpaper::Health h;
                h.now_ms = now; h.started_ms = started_ms;
                h.connected = connected; h.last_msg_ms = last_msg_us / 1000; h.books = books; h.trades = trades; h.gaps = gaps; h.reconnects = reconnects;
                h.reorder_pending = static_cast<int64_t>(pf.sequencer().pending()); h.late_trades = pf.sequencer().late_trades();
                double worst = 0; int64_t nh = 0, nm = 0;
                for (const auto& s : pf.snapshot()) { ++nm; if (s.halted) ++nh; if (nm == 1 || s.equity < worst) worst = s.equity; }
                h.markets = nm; h.halted_markets = nh;
                h.db_enabled = db != nullptr;
                if (db) { h.db_queue = static_cast<int64_t>(db->queued()); h.db_retries = db->retries(); h.db_errors = db->errors(); h.db_dropped = db->dropped_total();
                          h.db_dropped_today = db->dropped_today(); h.db_last_commit_ms = db->last_commit_ms();
                          h.quote_queue = static_cast<int64_t>(db->quote_queued()); h.quote_dropped = db->quote_dropped_total();
                          h.quote_dropped_today = db->quote_dropped_today(); h.quote_written = db->quote_written();
                          h.quote_conflicts = db->quote_conflicts(); h.quote_errors = db->quote_errors(); }
                h.daily_loss_limit = daily_loss; h.worst_market_equity = worst; h.equity = pf.equity(); h.kill = kill; h.loss_stop = pf.loss_stopped();
                st["components"] = mmpaper::components_json(h);
                const fs::path tmp = status_file.string() + ".tmp";
                { std::ofstream(tmp) << st.dump(1); }
                fs::rename(tmp, status_file, ec);
            }
            if (n % 300 == 0)
                logf("summary: equity %.2f, gross $%.0f, fills %lld, books %lld, trades %lld, gaps %lld, reconnects %lld%s",
                     pf.equity(), pf.gross_usd(), (long long)fills, (long long)books, (long long)trades, (long long)gaps, (long long)reconnects,
                     pf.halted() ? " — HALTED" : "");
        }
    }, asio::detached);

    asio::signal_set sigs(ioc, SIGINT, SIGTERM);
    sigs.async_wait([&](const boost::system::error_code&, int sig) { logf("signal %d — stopping", sig); ioc.stop(); });
    asio::steady_timer limit(ioc);
    if (duration_s > 0) {
        limit.expires_after(std::chrono::seconds(duration_s));
        limit.async_wait([&](const boost::system::error_code& e) { if (!e) { logf("--duration %d s reached — stopping", duration_s); ioc.stop(); } });
    }
    ioc.run();
    logf("final: equity %.2f, fills %lld, books %lld, trades %lld, gaps %lld, reconnects %lld",
         pf.equity(), (long long)fills, (long long)books, (long long)trades, (long long)gaps, (long long)reconnects);
    return 0;
}
