/*  ═══════════════════════════════════════════════════════════════════════════
    dbn_tool — Databento (DBN) history → Postgres / inspection

    The owned history lives (read-only) in /home/theone/Desktop/bot/data/raw:
      mbo/*.dbn.zst          GLBX.MDP3 MBO, NQ.c.0, 2023-03 … 2026-03
      ohlcv-1m/*.dbn.zst     NQ.c.0 1-minute bars, 2010-07 … 2026-03
      es-ohlcv-1m/*.dbn.zst  ES.c.0 1-minute bars, 2011 … 2026-03
    This tool never writes there.

    usage:
      dbn_tool info  <file>                         metadata + record counts by type
      dbn_tool dump  <file> [--n N] [--skip K]      print records
      dbn_tool bars  --symbol NQ <ohlcv-1m files…>  load into bars_1m (ON CONFLICT DO NOTHING:
                                                    the collector's own bars win); volume = contracts,
                                                    buy_volume 0 and ticks 0 = "aggressor split unknown"
      dbn_tool mbo-stats <mbo files…> [--from T] [--to T] [--daily]
                                                    replay the book; trade counts/volume (per UTC day
                                                    with --daily), quote counts, book sanity counters
      dbn_tool mbo-1m <mbo files…> [--from T] [--to T] [--symbol NQ] [--dry-run]
                                                    per-minute order flow → mbo_1m (venue 'GLBX'),
                                                    book_1m (top-of-book stats, ON CONFLICT DO NOTHING)
                                                    and buy_volume/ticks onto historical bars_1m rows
                                                    (only rows with ticks = 0, i.e. loaded by `bars`)
      dbn_tool ticks <mbo files…> --from T --to T   print the replay tick stream (ts_us,price,size,is_buy)
      dbn_tool agg-bars --symbol BTCUSDT <aggTrades .zip/.csv.zst/.csv files…> [--dry-run]
                                                    Binance aggTrades → bars_1m (volume/buy_volume in 0.001 coin,
                                                    ticks = aggTrades — the crypto_backfill.sh rollup), ON CONFLICT
                                                    DO NOTHING so collector/backfill bars win
    T = "YYYY-MM-DD[ HH:MM]" in UTC.

    Postgres: ≤ 1 connection, closed on every path (RAII); bulk loads via COPY into a temp table,
    synchronous_commit off.
    ═══════════════════════════════════════════════════════════════════════════ */
#include "config.hpp"
#include "history/dbn.hpp"
#include "history/mbo_feed.hpp"
#include "history/binance_feed.hpp"

#include <libpq-fe.h>

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <vector>

namespace {

int64_t parse_utc_us(const std::string& s) {
    struct tm t{}; int h = 0, mi = 0;
    if (std::sscanf(s.c_str(), "%d-%d-%d %d:%d", &t.tm_year, &t.tm_mon, &t.tm_mday, &h, &mi) < 3)
        throw std::runtime_error("bad time (want \"YYYY-MM-DD[ HH:MM]\" UTC): " + s);
    t.tm_year -= 1900; t.tm_mon -= 1; t.tm_hour = h; t.tm_min = mi;
    return (int64_t)timegm(&t) * 1'000'000LL;
}
std::string fmt_ts(int64_t us) {
    time_t s = (time_t)(us / 1'000'000LL); struct tm t; gmtime_r(&s, &t);
    char b[40]; std::strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S+00", &t); return b;
}
std::string fmt_ns(uint64_t ns) {
    time_t s = (time_t)(ns / 1'000'000'000ULL); struct tm t; gmtime_r(&s, &t);
    char b[64]; std::strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%S", &t);
    char o[96]; std::snprintf(o, sizeof o, "%s.%09" PRIu64 "Z", b, (uint64_t)(ns % 1'000'000'000ULL)); return o;
}
std::string day_of(int64_t us) { return fmt_ts(us).substr(0, 10); }

// ── Postgres (RAII) ─────────────────────────────────────────────────────────
class Pg {
public:
    Pg() {
        Config env = Config::from_env(".env");
        if (env.pg_password.empty()) throw std::runtime_error("PG_PASSWORD not set (.env)");
        c_ = PQconnectdb(env.pg_connstr().c_str());
        if (!c_ || PQstatus(c_) != CONNECTION_OK) {
            std::string e = c_ ? PQerrorMessage(c_) : "null";
            if (c_) PQfinish(c_);
            c_ = nullptr;
            throw std::runtime_error("postgres connect: " + e);
        }
        exec("SET synchronous_commit = off");
        exec("SET application_name = 'dbn_tool'");
    }
    ~Pg() { if (c_) PQfinish(c_); }
    Pg(const Pg&) = delete; Pg& operator=(const Pg&) = delete;

    void exec(const std::string& sql) {
        PGresult* r = PQexec(c_, sql.c_str());
        const auto st = r ? PQresultStatus(r) : PGRES_FATAL_ERROR;
        std::string e = r ? PQresultErrorMessage(r) : "null result";
        if (r) PQclear(r);
        if (st != PGRES_COMMAND_OK && st != PGRES_TUPLES_OK) throw std::runtime_error("SQL failed: " + e + " — " + sql.substr(0, 200));
    }
    long long exec_count(const std::string& sql) {   // INSERT/UPDATE affected rows
        PGresult* r = PQexec(c_, sql.c_str());
        const auto st = r ? PQresultStatus(r) : PGRES_FATAL_ERROR;
        std::string e = r ? PQresultErrorMessage(r) : "null result";
        long long n = (r && st == PGRES_COMMAND_OK) ? std::atoll(PQcmdTuples(r)) : 0;
        if (r) PQclear(r);
        if (st != PGRES_COMMAND_OK && st != PGRES_TUPLES_OK) throw std::runtime_error("SQL failed: " + e);
        return n;
    }
    void copy(const std::string& copy_sql, const std::string& data) {
        PGresult* r = PQexec(c_, copy_sql.c_str());
        const bool ok = r && PQresultStatus(r) == PGRES_COPY_IN;
        std::string e = r ? PQresultErrorMessage(r) : "null";
        if (r) PQclear(r);
        if (!ok) throw std::runtime_error("COPY start failed: " + e);
        if (PQputCopyData(c_, data.data(), (int)data.size()) != 1 || PQputCopyEnd(c_, nullptr) != 1)
            throw std::runtime_error(std::string("COPY data failed: ") + PQerrorMessage(c_));
        bool fail = false; std::string fe;
        while ((r = PQgetResult(c_)) != nullptr) {
            if (PQresultStatus(r) != PGRES_COMMAND_OK) { fail = true; fe = PQresultErrorMessage(r); }
            PQclear(r);
        }
        if (fail) throw std::runtime_error("COPY failed: " + fe);
    }
private:
    PGconn* c_ = nullptr;
};

// ── info / dump ─────────────────────────────────────────────────────────────
void print_meta(const dbn::Metadata& m) {
    std::printf("version=%u dataset=%s schema=%u start=%s end=%s stype_in=%u stype_out=%u ts_out=%d\n",
                m.version, m.dataset.c_str(), m.schema, fmt_ns(m.start).c_str(),
                m.end == UINT64_MAX ? "-" : fmt_ns(m.end).c_str(), m.stype_in, m.stype_out, (int)m.ts_out);
    for (auto& s : m.symbols) std::printf("symbol %s\n", s.c_str());
    for (auto& sm : m.mappings)
        for (auto& iv : sm.intervals)
            std::printf("mapping %s %u..%u -> %s\n", sm.raw_symbol.c_str(), iv.start_date, iv.end_date, iv.symbol.c_str());
}
void print_record(const dbn::RecordHeader* h) {
    if (auto* m = dbn::Reader::as_mbo(h))
        std::printf("MBO ts=%s iid=%u act=%c side=%c px=%.2f sz=%u oid=%" PRIu64 " flags=0x%02x seq=%u\n",
                    fmt_ns(h->ts_event).c_str(), h->instrument_id, m->action, m->side, dbn::px(m->price), m->size,
                    m->order_id, m->flags, m->sequence);
    else if (auto* t = dbn::Reader::as_trade(h))
        std::printf("TRADE ts=%s iid=%u act=%c side=%c px=%.2f sz=%u flags=0x%02x\n", fmt_ns(h->ts_event).c_str(),
                    h->instrument_id, t->action, t->side, dbn::px(t->price), t->size, t->flags);
    else if (auto* o = dbn::Reader::as_ohlcv(h))
        std::printf("OHLCV rtype=0x%02x ts=%s iid=%u o=%.2f h=%.2f l=%.2f c=%.2f v=%" PRIu64 "\n", h->rtype,
                    fmt_ns(h->ts_event).c_str(), h->instrument_id, dbn::px(o->open), dbn::px(o->high), dbn::px(o->low),
                    dbn::px(o->close), o->volume);
    else std::printf("rtype=0x%02x len=%u ts=%s iid=%u\n", h->rtype, h->length * 4u, fmt_ns(h->ts_event).c_str(), h->instrument_id);
}

int cmd_info(const std::string& f) {
    dbn::Reader r(f);
    print_meta(r.metadata());
    std::map<int, uint64_t> by_type; uint64_t first = 0, last = 0;
    while (auto* h = r.next()) { ++by_type[h->rtype]; if (!first) first = h->ts_event; last = h->ts_event; }
    for (auto& [k, v] : by_type) std::printf("rtype 0x%02x: %" PRIu64 " records\n", k, v);
    std::printf("records=%" PRIu64 " first=%s last=%s\n", r.records_read(), first ? fmt_ns(first).c_str() : "-",
                last ? fmt_ns(last).c_str() : "-");
    return 0;
}
int cmd_dump(const std::string& f, uint64_t n, uint64_t skip) {
    dbn::Reader r(f);
    print_meta(r.metadata());
    uint64_t i = 0;
    while (auto* h = r.next()) {
        if (i++ < skip) continue;
        print_record(h);
        if (i - skip >= n) break;
    }
    return 0;
}

// ── bars ────────────────────────────────────────────────────────────────────
int cmd_bars(const std::string& symbol, const std::vector<std::string>& files, bool dry) {
    std::unique_ptr<Pg> pg;
    if (!dry) {
        pg = std::make_unique<Pg>();
        pg->exec("CREATE TEMP TABLE IF NOT EXISTS hist_bars (minute timestamptz, open float8, high float8, low float8, "
                 "close float8, volume bigint)");
    }
    long long total_in = 0, total_new = 0;
    for (const auto& f : files) {
        dbn::Reader r(f);
        std::string buf; buf.reserve(8 << 20);
        long long n = 0, skipped = 0;
        int64_t last_min = INT64_MIN;
        while (auto* h = r.next()) {
            auto* o = dbn::Reader::as_ohlcv(h);
            if (!o || h->rtype != dbn::kOhlcv1m) { ++skipped; continue; }
            if (o->open == dbn::kUndefPrice || o->close == dbn::kUndefPrice) { ++skipped; continue; }
            const int64_t us = (int64_t)(h->ts_event / 1000);
            if (us == last_min) { ++skipped; continue; }      // two instruments in one minute: keep the first
            last_min = us;
            char line[200];
            std::snprintf(line, sizeof line, "%s\t%.9g\t%.9g\t%.9g\t%.9g\t%" PRIu64 "\n", fmt_ts(us).c_str(),
                          dbn::px(o->open), dbn::px(o->high), dbn::px(o->low), dbn::px(o->close), o->volume);
            buf += line; ++n;
        }
        long long ins = 0;
        if (!dry && n) {
            pg->exec("TRUNCATE hist_bars");
            pg->copy("COPY hist_bars FROM STDIN", buf);
            ins = pg->exec_count(
                "INSERT INTO bars_1m (symbol, minute, open, high, low, close, volume, buy_volume, ticks) "
                "SELECT '" + symbol + "', minute, open, high, low, close, volume, 0, 0 FROM hist_bars "
                "ON CONFLICT (symbol, minute) DO NOTHING");
        }
        total_in += n; total_new += ins;
        std::printf("%s: %lld bars read, %lld inserted%s, %lld skipped records\n", f.c_str(), n, ins,
                    dry ? " (dry run)" : "", skipped);
        std::fflush(stdout);
    }
    std::printf("TOTAL %s: %lld bars read, %lld inserted\n", symbol.c_str(), total_in, total_new);
    return 0;
}

// ── mbo-stats / ticks ───────────────────────────────────────────────────────
int cmd_mbo_stats(const std::vector<std::string>& files, int64_t from_us, int64_t to_us, bool daily) {
    hist::MboFeed feed(files, from_us, to_us);
    feed.set_log([](const std::string& s) { std::fprintf(stderr, "%s\n", s.c_str()); });
    struct Day { uint64_t trades = 0, vol = 0, buy = 0, quotes = 0; double hi = 0, lo = 1e18; };
    std::map<std::string, Day> days;
    std::vector<hist::Tick> ticks; std::deque<hist::Quote> quotes;
    uint64_t tot_t = 0, tot_q = 0, back_ts = 0; int64_t prev = 0;
    while (feed.next_batch(ticks, quotes, 100000)) {
        for (auto& t : ticks) {
            if (t.ts_us < prev) ++back_ts;
            prev = t.ts_us;
            if (daily) {
                Day& d = days[day_of(t.ts_us)];
                ++d.trades; d.vol += t.size; if (t.is_buy) d.buy += t.size;
                if (t.price > d.hi) d.hi = t.price;
                if (t.price < d.lo) d.lo = t.price;
            }
        }
        if (daily) for (auto& q : quotes) ++days[day_of(q.ts_us)].quotes;
        tot_t += ticks.size(); tot_q += quotes.size();
        ticks.clear(); quotes.clear();
    }
    if (daily)
        for (auto& [d, v] : days)
            std::printf("%s trades=%" PRIu64 " volume=%" PRIu64 " buy_share=%.3f quotes=%" PRIu64 " hi=%.2f lo=%.2f\n",
                        d.c_str(), v.trades, v.vol, v.vol ? (double)v.buy / v.vol : 0.0, v.quotes, v.hi, v.lo);
    const auto& s = feed.stats();
    std::printf("records=%" PRIu64 " trades=%" PRIu64 " volume=%" PRIu64 " side_N_trades=%" PRIu64 " emitted_ticks=%" PRIu64
                " emitted_quotes=%" PRIu64 " clamped_ts=%" PRIu64 " backwards_ts=%" PRIu64 " crossed_books=%" PRIu64
                " instrument_switches=%" PRIu64 " snapshot_records=%" PRIu64 "\n",
                s.records, s.trades, s.trade_volume, s.side_n_trades, tot_t, tot_q, s.clamped_ts, back_ts,
                s.crossed_books, s.instrument_switches, s.snapshots);
    std::printf("book: adds=%" PRIu64 " modifies=%" PRIu64 " cancels=%" PRIu64 " executed_deletes=%" PRIu64
                " clears=%" PRIu64 " unknown_cancel=%" PRIu64 " unknown_modify=%" PRIu64 "\n",
                s.book.adds, s.book.changes, s.book.deletes, s.book.executed_deletes, s.book.clears,
                s.book.unknown_cancel, s.book.unknown_modify);
    return 0;
}
int cmd_ticks(const std::vector<std::string>& files, int64_t from_us, int64_t to_us, bool with_quotes) {
    hist::MboFeed feed(files, from_us, to_us);
    std::vector<hist::Tick> ticks; std::deque<hist::Quote> quotes;
    while (feed.next_batch(ticks, quotes, 100000)) {
        // merge for printing, quotes first at equal ts (the engine applies quotes ≤ tick ts first)
        size_t qi = 0;
        for (auto& t : ticks) {
            while (with_quotes && qi < quotes.size() && quotes[qi].ts_us <= t.ts_us) {
                auto& q = quotes[qi++];
                std::printf("Q,%" PRId64 ",%.2f,%d,%.2f,%d\n", q.ts_us, q.bid, q.bid_sz, q.ask, q.ask_sz);
            }
            std::printf("T,%" PRId64 ",%.2f,%" PRId64 ",%d\n", t.ts_us, t.price, t.size, (int)t.is_buy);
        }
        ticks.clear(); quotes.clear();
    }
    return 0;
}

// ── agg-bars ────────────────────────────────────────────────────────────────
int cmd_agg_bars(const std::string& symbol, const std::vector<std::string>& files, int64_t from_us, int64_t to_us, bool dry) {
    std::unique_ptr<Pg> pg;
    if (!dry) {
        pg = std::make_unique<Pg>();
        pg->exec("CREATE TEMP TABLE IF NOT EXISTS hist_ab (minute timestamptz, open float8, high float8, low float8, "
                 "close float8, volume bigint, buy_volume bigint, ticks int)");
    }
    long long bars = 0, ins = 0;
    for (const auto& f : files) {
        hist::AggTradesFeed feed({f}, from_us, to_us);
        std::vector<hist::Tick> ticks; std::deque<hist::Quote> q;
        std::string buf; buf.reserve(8 << 20);
        int64_t m = INT64_MIN; double o = 0, h = 0, l = 0, c = 0; int64_t v = 0, bv = 0, n = 0; long long nb = 0;
        auto emit = [&]() {
            if (m == INT64_MIN) return;
            char line[200];
            std::snprintf(line, sizeof line, "%s\t%.10g\t%.10g\t%.10g\t%.10g\t%" PRId64 "\t%" PRId64 "\t%" PRId64 "\n",
                          fmt_ts(m).c_str(), o, h, l, c, v, bv, n);
            buf += line; ++nb;
        };
        while (feed.next_batch(ticks, q, 200000)) {
            for (const auto& t : ticks) {
                const int64_t mm = t.ts_us - (t.ts_us % 60'000'000LL);
                if (mm != m) { emit(); m = mm; o = h = l = t.price; v = bv = n = 0; }
                if (t.price > h) h = t.price;
                if (t.price < l) l = t.price;
                c = t.price; v += t.size; if (t.is_buy) bv += t.size; ++n;
            }
            ticks.clear();
        }
        emit();
        long long k = 0;
        if (!dry && nb) {
            pg->exec("TRUNCATE hist_ab");
            pg->copy("COPY hist_ab FROM STDIN", buf);
            k = pg->exec_count("INSERT INTO bars_1m (symbol, minute, open, high, low, close, volume, buy_volume, ticks) "
                               "SELECT '" + symbol + "', minute, open, high, low, close, volume, buy_volume, ticks FROM hist_ab "
                               "ON CONFLICT (symbol, minute) DO NOTHING");
        }
        bars += nb; ins += k;
        std::printf("%s: %lld bars, %lld inserted%s — %s\n", f.c_str(), nb, k, dry ? " (dry run)" : "", feed.summary().c_str());
        std::fflush(stdout);
    }
    std::printf("TOTAL %s: %lld bars, %lld inserted\n", symbol.c_str(), bars, ins);
    return 0;
}

// ── mbo-1m ──────────────────────────────────────────────────────────────────
int cmd_mbo_1m(const std::vector<std::string>& files, int64_t from_us, int64_t to_us, const std::string& symbol, bool dry) {
    std::unique_ptr<Pg> pg;
    if (!dry) {
        pg = std::make_unique<Pg>();
        // Same DDL as migrations/20261010_mbo_1m.sql (shared with the crypto collector).
        pg->exec(R"sql(CREATE TABLE IF NOT EXISTS mbo_1m (
            venue VARCHAR(16) NOT NULL, symbol VARCHAR(16) NOT NULL, ts_minute TIMESTAMPTZ NOT NULL,
            adds INTEGER NOT NULL, changes INTEGER NOT NULL, deletes INTEGER NOT NULL, executed_deletes INTEGER NOT NULL,
            add_qty DOUBLE PRECISION NOT NULL, delete_qty DOUBLE PRECISION NOT NULL,
            bid_orders INTEGER NOT NULL, ask_orders INTEGER NOT NULL,
            bid_qty_top10bps DOUBLE PRECISION NOT NULL, ask_qty_top10bps DOUBLE PRECISION NOT NULL,
            best_bid DOUBLE PRECISION, best_ask DOUBLE PRECISION,
            order_count_imbalance DOUBLE PRECISION NOT NULL, qty_imbalance DOUBLE PRECISION NOT NULL,
            cancel_to_add_ratio DOUBLE PRECISION, avg_order_qty DOUBLE PRECISION,
            complete BOOLEAN NOT NULL DEFAULT TRUE, PRIMARY KEY (venue, symbol, ts_minute)))sql");
        pg->exec("CREATE TEMP TABLE IF NOT EXISTS hist_m (ts_minute timestamptz, adds int, changes int, deletes int, "
                 "executed_deletes int, add_qty float8, delete_qty float8, bid_orders int, ask_orders int, bq float8, aq float8, "
                 "best_bid float8, best_ask float8, oci float8, qi float8, c2a float8, aoq float8, complete bool, "
                 "trades bigint, volume bigint, buy_volume bigint, quotes int, avg_spread float8, max_spread float8, "
                 "avg_imb float8, bid_heavy float8, ask_heavy float8, avg_bsz float8, avg_asz float8, mid_open float8, mid_close float8)");
    }
    std::string buf; buf.reserve(16 << 20);
    long long rows = 0, flushed = 0, mbo_up = 0, book_ins = 0, bars_up = 0;
    auto opt = [](double v, bool have) { if (!have) return std::string("\\N"); char b[40]; std::snprintf(b, sizeof b, "%.10g", v); return std::string(b); };
    auto flush = [&]() {
        if (dry || buf.empty()) { buf.clear(); return; }
        pg->exec("TRUNCATE hist_m");
        pg->copy("COPY hist_m FROM STDIN", buf);
        buf.clear();
        mbo_up += pg->exec_count(
            "INSERT INTO mbo_1m (venue, symbol, ts_minute, adds, changes, deletes, executed_deletes, add_qty, delete_qty, "
            "bid_orders, ask_orders, bid_qty_top10bps, ask_qty_top10bps, best_bid, best_ask, order_count_imbalance, qty_imbalance, "
            "cancel_to_add_ratio, avg_order_qty, complete) "
            "SELECT 'GLBX', '" + symbol + "', ts_minute, adds, changes, deletes, executed_deletes, add_qty, delete_qty, bid_orders, "
            "ask_orders, bq, aq, best_bid, best_ask, oci, qi, c2a, aoq, complete FROM hist_m "
            "ON CONFLICT (venue, symbol, ts_minute) DO UPDATE SET adds=EXCLUDED.adds, changes=EXCLUDED.changes, "
            "deletes=EXCLUDED.deletes, executed_deletes=EXCLUDED.executed_deletes, add_qty=EXCLUDED.add_qty, "
            "delete_qty=EXCLUDED.delete_qty, bid_orders=EXCLUDED.bid_orders, ask_orders=EXCLUDED.ask_orders, "
            "bid_qty_top10bps=EXCLUDED.bid_qty_top10bps, ask_qty_top10bps=EXCLUDED.ask_qty_top10bps, best_bid=EXCLUDED.best_bid, "
            "best_ask=EXCLUDED.best_ask, order_count_imbalance=EXCLUDED.order_count_imbalance, qty_imbalance=EXCLUDED.qty_imbalance, "
            "cancel_to_add_ratio=EXCLUDED.cancel_to_add_ratio, avg_order_qty=EXCLUDED.avg_order_qty, complete=EXCLUDED.complete");
        book_ins += pg->exec_count(
            "INSERT INTO book_1m (symbol, minute, quotes, avg_spread_pts, max_spread_pts, avg_imbalance, bid_heavy_pct, "
            "ask_heavy_pct, avg_bid_size, avg_ask_size, mid_open, mid_close) "
            "SELECT '" + symbol + "', ts_minute, quotes, avg_spread, max_spread, avg_imb, bid_heavy, ask_heavy, avg_bsz, avg_asz, "
            "mid_open, mid_close FROM hist_m WHERE quotes > 0 ON CONFLICT (symbol, minute) DO NOTHING");
        bars_up += pg->exec_count(
            "UPDATE bars_1m b SET buy_volume = h.buy_volume, ticks = h.trades::int FROM hist_m h "
            "WHERE b.symbol = '" + symbol + "' AND b.minute = h.ts_minute AND b.ticks = 0 AND h.trades > 0");
        ++flushed;
    };
    hist::MboFeed feed(files, from_us, to_us);
    feed.set_log([](const std::string& s) { std::fprintf(stderr, "%s\n", s.c_str()); });
    feed.set_minute_sink([&](const hist::MinuteRow& m) {
        const double ob = (double)(m.bid_orders + m.ask_orders), qq = (double)(m.bid_qty_near + m.ask_qty_near);
        const double q = (double)m.quotes;
        char line[1024];
        std::snprintf(line, sizeof line,
            "%s\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
            "\t%" PRIu64 "\t%" PRIu64 "\t%s\t%s\t%.6f\t%.6f\t%s\t%s\t%s\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
            "\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n",
            fmt_ts(m.minute_us).c_str(), m.adds, m.changes, m.deletes, m.executed_deletes, m.add_qty, m.delete_qty,
            m.bid_orders, m.ask_orders, m.bid_qty_near, m.ask_qty_near,
            opt(m.best_bid, m.best_bid > 0).c_str(), opt(m.best_ask, m.best_ask > 0).c_str(),
            ob > 0 ? ((double)m.bid_orders - (double)m.ask_orders) / ob : 0.0,
            qq > 0 ? ((double)m.bid_qty_near - (double)m.ask_qty_near) / qq : 0.0,
            opt(m.adds ? (double)(m.deletes - m.executed_deletes) / m.adds : 0, m.adds > 0).c_str(),
            opt(m.adds ? (double)m.add_qty / m.adds : 0, m.adds > 0).c_str(),
            m.complete ? "t" : "f", m.trades, m.volume, m.buy_volume, m.quotes,
            opt(q ? m.spread_sum / q : 0, q > 0).c_str(), opt(m.spread_max, q > 0).c_str(),
            opt(q ? m.imb_sum / q : 0, q > 0).c_str(), opt(q ? m.bid_heavy / q : 0, q > 0).c_str(),
            opt(q ? m.ask_heavy / q : 0, q > 0).c_str(), opt(q ? m.bid_sz_sum / q : 0, q > 0).c_str(),
            opt(q ? m.ask_sz_sum / q : 0, q > 0).c_str(), opt(m.mid_open, q > 0).c_str(), opt(m.mid_close, q > 0).c_str());
        buf += line; ++rows;
        if (buf.size() > (8u << 20)) flush();
    });
    feed.run_all();
    flush();
    const auto& s = feed.stats();
    std::printf("mbo-1m %s: %lld minutes (%lld flushes) — mbo_1m upserted %lld, book_1m inserted %lld, "
                "bars_1m buy_volume filled %lld%s; records=%" PRIu64 " trades=%" PRIu64 "\n",
                symbol.c_str(), rows, flushed, mbo_up, book_ins, bars_up, dry ? " (dry run)" : "", s.records, s.trades);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: dbn_tool {info|dump|bars|mbo-stats|mbo-1m|ticks|agg-bars} … (see src/history/dbn_tool_main.cpp)\n");
        return 2;
    }
    const std::string cmd = argv[1];
    std::vector<std::string> files;
    std::string symbol = "NQ";
    int64_t from_us = INT64_MIN / 2, to_us = INT64_MAX / 2;
    uint64_t n = 20, skip = 0;
    bool dry = false, daily = false, quotes = false;
    try {
        for (int i = 2; i < argc; ++i) {
            const std::string a = argv[i];
            auto val = [&]() -> std::string { if (i + 1 >= argc) throw std::runtime_error("missing value for " + a); return argv[++i]; };
            if (a == "--symbol") symbol = val();
            else if (a == "--from") from_us = parse_utc_us(val());
            else if (a == "--to") to_us = parse_utc_us(val());
            else if (a == "--n") n = std::stoull(val());
            else if (a == "--skip") skip = std::stoull(val());
            else if (a == "--dry-run") dry = true;
            else if (a == "--daily") daily = true;
            else if (a == "--quotes") quotes = true;
            else if (a.rfind("--", 0) == 0) throw std::runtime_error("unknown option " + a);
            else files.push_back(a);
        }
        for (char c : symbol) if (!std::isalnum((unsigned char)c)) throw std::runtime_error("bad --symbol");
        if (files.empty()) throw std::runtime_error("no input files");
        if (cmd == "info") return cmd_info(files[0]);
        if (cmd == "dump") return cmd_dump(files[0], n, skip);
        if (cmd == "bars") return cmd_bars(symbol, files, dry);
        if (cmd == "mbo-stats") return cmd_mbo_stats(files, from_us, to_us, daily);
        if (cmd == "mbo-1m") return cmd_mbo_1m(files, from_us, to_us, symbol, dry);
        if (cmd == "ticks") return cmd_ticks(files, from_us, to_us, quotes);
        if (cmd == "agg-bars") return cmd_agg_bars(symbol, files, from_us, to_us, dry);
        std::fprintf(stderr, "unknown command %s\n", cmd.c_str());
        return 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "dbn_tool: %s\n", e.what());
        return 1;
    }
}
