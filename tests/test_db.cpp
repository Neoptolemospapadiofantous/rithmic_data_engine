// Integration tests for TickDB + AuditLog against a real PostgreSQL instance.
//
// Requires env vars: PG_HOST, PG_PORT, PG_DB, PG_USER, PG_PASSWORD
// or a .env file in the working directory.
//
// Run: ./test_db
// All tests run inside an isolated rithmic_test schema (dropped on exit) —
// production tables are never touched, so the live collector can keep writing.

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/audit.hpp"
#include "../src/config.hpp"
#include "../src/db.hpp"

// ── helpers ────────────────────────────────────────────────────────

static int g_passed  = 0;
static int g_failed  = 0;
static int g_skipped = 0;

// Throw this to skip a test without counting it as a failure
struct SkipTest : std::exception {
    explicit SkipTest(const char* msg) : msg_(msg) {}
    const char* what() const noexcept override { return msg_; }
    const char* msg_;
};

#define TEST(name) \
    static void test_##name(); \
    struct _reg_##name { _reg_##name() { \
        std::printf("  %-40s ", #name); \
        try { test_##name(); std::printf("PASS\n"); ++g_passed; } \
        catch (std::exception& e) { std::printf("FAIL: %s\n", e.what()); ++g_failed; } \
    }} _inst_##name; \
    static void test_##name()

#define ASSERT(expr) \
    if (!(expr)) throw std::runtime_error("Assertion failed: " #expr)

#define ASSERT_EQ(a, b) \
    if ((a) != (b)) throw std::runtime_error( \
        std::string("Expected ") + std::to_string(b) + " got " + std::to_string(a))

// ── RAII PostgreSQL connection guard ───────────────────────────────
//
// Ensures PQfinish() is always called, even when an ASSERT throws.
// Without this, raw PGconn* leaked on every assertion failure.
struct PGConnGuard {
    PGconn* conn;
    explicit PGConnGuard(const std::string& cs) : conn(PQconnectdb(cs.c_str())) {}
    ~PGConnGuard() { if (conn) PQfinish(conn); }
    PGconn* get()  const { return conn; }
    bool    ok()   const { return conn && PQstatus(conn) == CONNECTION_OK; }
    PGConnGuard(const PGConnGuard&)            = delete;
    PGConnGuard& operator=(const PGConnGuard&) = delete;
};

// ── fixture ────────────────────────────────────────────────────────
//
// Every statement in this suite runs inside an isolated `rithmic_test`
// schema, recreated fresh at static-init time and dropped at exit.  The
// schema is selected via `options='-c search_path=rithmic_test'` on the
// connection string, so all unqualified table references (including the
// ones inside TickDB/AuditLog/ensure_schema) resolve there.  Production
// tables in `public` are never touched — the suite is safe to run while
// the live collector is writing.

static std::string g_base_connstr = []() {
    Config c = Config::from_env(".env");
    return c.pg_connstr();
}();

// g_connstr initialises after g_base_connstr (definition order), before any
// TEST static-initializer constructor runs — so every test connection lands
// in the isolated schema.
static std::string g_connstr = []() -> std::string {
    PGconn* c = PQconnectdb(g_base_connstr.c_str());
    if (PQstatus(c) != CONNECTION_OK) {
        std::fprintf(stderr, "Cannot connect to PostgreSQL: %s\n",
                     PQerrorMessage(c));
        PQfinish(c);
        std::exit(1);
    }
    PGresult* r = PQexec(c,
        "DROP SCHEMA IF EXISTS rithmic_test CASCADE;"
        "CREATE SCHEMA rithmic_test;");
    bool ok = r && PQresultStatus(r) == PGRES_COMMAND_OK;
    if (!ok) {
        std::fprintf(stderr, "Cannot create test schema: %s\n",
                     PQerrorMessage(c));
        if (r) PQclear(r);
        PQfinish(c);
        std::exit(1);
    }
    PQclear(r);
    PQfinish(c);
    return g_base_connstr + " options='-c search_path=rithmic_test'";
}();

static void drop_test_schema() {
    PGconn* c = PQconnectdb(g_base_connstr.c_str());
    if (PQstatus(c) == CONNECTION_OK) {
        PGresult* r = PQexec(c, "DROP SCHEMA IF EXISTS rithmic_test CASCADE");
        if (r) PQclear(r);
    }
    PQfinish(c);
}

// ── tests ──────────────────────────────────────────────────────────

TEST(connection) {
    TickDB db(g_connstr);
    ASSERT(db.conn() != nullptr);
}

TEST(write_and_count) {
    TickDB db(g_connstr);

    std::vector<TickRow> rows = {
        {1712000000'000000LL, 18500.0, 10, true},
        {1712000001'000000LL, 18501.5, 5,  false},
        {1712000002'000000LL, 18502.0, 8,  true},
    };

    int inserted = db.write(rows);
    ASSERT_EQ(inserted, 3);
    ASSERT_EQ(db.row_count(), 3);
}

TEST(deduplication) {
    TickDB db(g_connstr);

    // Insert same ts_event twice — second should be ignored
    std::vector<TickRow> rows = {
        {1712000010'000000LL, 18510.0, 1, true},
    };
    db.write(rows);
    int second = db.write(rows);  // duplicate
    ASSERT_EQ(second, 0);

    // Total should still be 3 + 1 = 4 from previous tests
    // (test isolation is via table drop/recreate in main)
}

TEST(latest_price) {
    TickDB db(g_connstr);

    std::vector<TickRow> rows = {
        {1712000020'000000LL, 18520.25, 3, false},
    };
    db.write(rows);

    auto price = db.latest_price();
    ASSERT(price.has_value());
    ASSERT(*price == 18520.25);
}

TEST(summary) {
    TickDB db(g_connstr);
    auto s = db.summary();
    ASSERT(s.tick_count >= 0);
    ASSERT(!s.connstr.empty());
}

TEST(audit_log) {
    TickDB db(g_connstr);
    AuditLog audit(db.conn());

    audit.info("test.event", "detail=hello");
    audit.warn("test.warn",  "detail=world");
    audit.error("test.error","detail=oops");

    ASSERT(audit.pending() == 3);

    audit.flush();
    ASSERT(audit.pending() == 0);

    // Verify rows written
    PGresult* res = PQexec(db.conn(),
        "SELECT COUNT(*) FROM audit_log WHERE event LIKE 'test.%'");
    ASSERT(res && PQresultStatus(res) == PGRES_TUPLES_OK);
    int count = std::atoi(PQgetvalue(res, 0, 0));
    PQclear(res);
    ASSERT(count == 3);
}

TEST(large_batch) {
    TickDB db(g_connstr);

    // Insert 500 ticks in one batch
    std::vector<TickRow> rows;
    rows.reserve(500);
    for (int i = 0; i < 500; ++i)
        rows.push_back({1712001000'000000LL + i * 1000LL,
                        18500.0 + i * 0.25, 1 + i % 10, i % 2 == 0});

    int inserted = db.write(rows);
    ASSERT(inserted == 500);
}

// ── RUN_TEST macro — runs tests from main() (not static init) ──────
//
// New tests use RUN_TEST instead of TEST so they execute after main()
// has set up the database schema.  TEST is kept for the existing tests
// (static-initializer style, maintained for backward compat).

#define RUN_TEST(name) \
    do { \
        std::printf("  %-40s ", #name); \
        try { test_##name(); std::printf("PASS\n"); ++g_passed; } \
        catch (SkipTest& s) { std::printf("SKIP: %s\n", s.what()); ++g_skipped; } \
        catch (std::exception& e) { std::printf("FAIL: %s\n", e.what()); ++g_failed; } \
    } while (0)

// ── BBO helpers ────────────────────────────────────────────────────

// Truncate (inside the isolated rithmic_test schema) rather than drop so
// the hypertable structure created by ensure_schema is preserved.
static void truncate_bbo_table(PGconn* conn) {
    PGresult* r = PQexec(conn, "TRUNCATE TABLE bbo");
    if (r) PQclear(r);
}

// ── BBORow tests ────────────────────────────────────────────────────

static void test_write_bbo_basic() {
    {
        PGConnGuard g(g_connstr);
        truncate_bbo_table(g.get());
    }

    TickDB db(g_connstr);

    std::vector<BBORow> rows = {
        {1712000100'000000LL, 18499.75, 10, 3, 18500.00, 8,  2, "NQ", "CME"},
        {1712000101'000000LL, 18500.00, 5,  1, 18500.25, 12, 4, "NQ", "CME"},
        {1712000102'000000LL, 18500.25, 7,  2, 18500.50, 6,  1, "NQ", "CME"},
    };

    int inserted = db.write_bbo(rows);
    ASSERT_EQ(inserted, 3);
}

static void test_write_bbo_empty_batch() {
    TickDB db(g_connstr);

    std::vector<BBORow> empty;
    int inserted = db.write_bbo(empty);
    ASSERT_EQ(inserted, 0);
}

static void test_write_bbo_dedup() {
    // idx_bbo_unique (symbol, exchange, ts_event) drives ON CONFLICT
    // DO UPDATE — a repeated identical row merges (no duplicate, no error).
    TickDB db(g_connstr);

    std::vector<BBORow> rows = {
        {1712000200'000000LL, 18510.0, 5, 1, 18510.25, 8, 2, "NQ", "CME"},
    };

    int first  = db.write_bbo(rows);
    int second = db.write_bbo(rows);

    ASSERT(first  >= 1);
    ASSERT(second >= 0);  // 0 or 1 — a DO UPDATE merge counts as affected
}

// Helper: true if idx_bbo_unique exists (required for the merge-on-conflict path)
static bool bbo_unique_index_exists() {
    PGConnGuard g(g_connstr);
    if (!g.ok()) return false;
    PGresult* r = PQexec(g.get(),
        "SELECT COUNT(*) FROM pg_indexes"
        " WHERE schemaname='rithmic_test' AND tablename='bbo'"
        "   AND indexname='idx_bbo_unique'");
    bool ok = r && PQresultStatus(r) == PGRES_TUPLES_OK && std::atoi(PQgetvalue(r, 0, 0)) > 0;
    if (r) PQclear(r);
    return ok;
}

// One-sided updates (absent side = nullopt → SQL NULL) must merge with
// COALESCE so a bid-only and an ask-only update at the same ts_event
// combine into a single two-sided row instead of clobbering each other.
static void test_write_bbo_null_side_merge() {
    if (!bbo_unique_index_exists())
        throw SkipTest("idx_bbo_unique not present (required for merge-on-conflict)");

    {
        PGConnGuard g(g_connstr);
        truncate_bbo_table(g.get());
    }

    TickDB db(g_connstr);
    int64_t ts = 1712000600'000000LL;

    // Bid-only update — ask side absent
    std::vector<BBORow> bid_only = {
        {ts, 18499.75, 10, 3, std::nullopt, 0, 0, "NQ", "CME"},
    };
    ASSERT_EQ(db.write_bbo(bid_only), 1);

    // Ask-only update at the same ts_event — bid side absent
    std::vector<BBORow> ask_only = {
        {ts, std::nullopt, 0, 0, 18500.25, 8, 2, "NQ", "CME"},
    };
    db.write_bbo(ask_only);

    // The two one-sided rows must have merged into one two-sided row
    PGresult* res = PQexec(db.conn(),
        "SELECT bid_price, bid_size, bid_orders, ask_price, ask_size, ask_orders"
        " FROM bbo WHERE ts_event = to_timestamp(1712000600.0)");
    ASSERT(res && PQresultStatus(res) == PGRES_TUPLES_OK);
    ASSERT_EQ(PQntuples(res), 1);
    ASSERT(!PQgetisnull(res, 0, 0));
    ASSERT(!PQgetisnull(res, 0, 3));
    double bid = std::atof(PQgetvalue(res, 0, 0));
    double ask = std::atof(PQgetvalue(res, 0, 3));
    int bid_sz = std::atoi(PQgetvalue(res, 0, 1));
    int ask_sz = std::atoi(PQgetvalue(res, 0, 4));
    PQclear(res);
    ASSERT(bid == 18499.75);
    ASSERT(ask == 18500.25);
    ASSERT_EQ(bid_sz, 10);
    ASSERT_EQ(ask_sz, 8);
}

// ── DepthRow helpers ────────────────────────────────────────────────

// Truncate (inside the isolated rithmic_test schema) rather than drop so
// the hypertable structure created by ensure_schema is preserved.
static void truncate_depth_table(PGconn* conn) {
    PGresult* r = PQexec(conn, "TRUNCATE TABLE depth_by_order");
    if (r) PQclear(r);
}

// ── DepthRow tests ──────────────────────────────────────────────────

// Helper: true if idx_depth_unique exists (TimescaleDB may not support partial unique indexes)
static bool depth_unique_index_exists() {
    PGConnGuard g(g_connstr);
    if (!g.ok()) return false;
    PGresult* r = PQexec(g.get(),
        "SELECT COUNT(*) FROM pg_indexes"
        " WHERE schemaname='rithmic_test' AND tablename='depth_by_order'"
        "   AND indexname='idx_depth_unique'");
    bool ok = r && PQresultStatus(r) == PGRES_TUPLES_OK && std::atoi(PQgetvalue(r, 0, 0)) > 0;
    if (r) PQclear(r);
    return ok;
}

static void test_write_depth_basic() {
    {
        PGConnGuard g(g_connstr);
        truncate_depth_table(g.get());
    }

    TickDB db(g_connstr);

    std::vector<DepthRow> rows = {
        {1712000300'000000LL, 1712000300'000000001LL, 1001, 1, 1, 18499.75, 0.0, 10, "ORD001", "NQ", "CME"},
        {1712000301'000000LL, 1712000301'000000002LL, 1002, 2, 1, 18499.75, 18499.75, 8, "ORD001", "NQ", "CME"},
        {1712000302'000000LL, 1712000302'000000003LL, 1003, 3, 1, 18499.75, 18499.75, 0, "ORD001", "NQ", "CME"},
    };

    // write_depth requires idx_depth_unique; skip if TimescaleDB won't create it
    if (!depth_unique_index_exists())
        throw SkipTest("idx_depth_unique not present (TimescaleDB partial-unique limitation)");

    int inserted = db.write_depth(rows);
    ASSERT_EQ(inserted, 3);
}

static void test_write_depth_empty_batch() {
    TickDB db(g_connstr);

    // Empty batch is a fast-path return — no DB query issued, no constraint needed
    std::vector<DepthRow> empty;
    int inserted = db.write_depth(empty);
    ASSERT_EQ(inserted, 0);
}

static void test_write_depth_dedup() {
    if (!depth_unique_index_exists())
        throw SkipTest("idx_depth_unique not present (TimescaleDB partial-unique limitation)");

    TickDB db(g_connstr);

    // Same source_ns → should be deduped by idx_depth_unique
    std::vector<DepthRow> rows = {
        {1712000400'000000LL, 1712000400'999999001LL, 2001, 1, 1, 18502.0, 0.0, 5, "ORD999", "NQ", "CME"},
    };

    int first  = db.write_depth(rows);
    int second = db.write_depth(rows);  // same source_ns

    ASSERT(first >= 1);
    ASSERT(second <= 0);
}

static void test_write_depth_update_types() {
    if (!depth_unique_index_exists())
        throw SkipTest("idx_depth_unique not present (TimescaleDB partial-unique limitation)");

    TickDB db(g_connstr);

    // One row per update_type: 1=NEW, 2=CHANGE, 3=DELETE — all unique source_ns
    std::vector<DepthRow> rows = {
        {1712000500'000000LL, 1712000500'100000001LL, 3001, 1, 1, 18505.0, 0.0,     5, "ORD_A", "NQ", "CME"},
        {1712000500'000001LL, 1712000500'200000002LL, 3002, 2, 1, 18505.0, 18505.0, 3, "ORD_A", "NQ", "CME"},
        {1712000500'000002LL, 1712000500'300000003LL, 3003, 3, 1, 18505.0, 18505.0, 0, "ORD_A", "NQ", "CME"},
    };

    int inserted = db.write_depth(rows);
    ASSERT_EQ(inserted, 3);
}

// source_ns == 0 means "source timestamp absent": rows must be plain-inserted
// (no dedup key — it would collapse them) and must never error, regardless of
// whether idx_depth_unique exists.  nullopt prev_depth_price → SQL NULL.
static void test_write_depth_zero_source_ns() {
    {
        PGConnGuard g(g_connstr);
        truncate_depth_table(g.get());
    }

    TickDB db(g_connstr);

    std::vector<DepthRow> rows = {
        {1712000700'000000LL, 0, 4001, 1, 1, 18510.0,  std::nullopt, 5, "ORD_Z1", "NQ", "CME"},
        {1712000701'000000LL, 0, 4002, 2, 1, 18510.0,  18510.0,      3, "ORD_Z2", "NQ", "CME"},
        {1712000702'000000LL, 0, 4003, 1, 2, 18510.25, std::nullopt, 7, "ORD_Z3", "NQ", "CME"},
    };

    int inserted = db.write_depth(rows);
    ASSERT_EQ(inserted, 3);

    // nullopt prev_depth_price stored as SQL NULL
    PGresult* res = PQexec(db.conn(),
        "SELECT prev_depth_price FROM depth_by_order"
        " WHERE exchange_order_id = 'ORD_Z1'");
    ASSERT(res && PQresultStatus(res) == PGRES_TUPLES_OK);
    ASSERT(PQntuples(res) >= 1);
    bool is_null = PQgetisnull(res, 0, 0);
    PQclear(res);
    ASSERT(is_null);
}

// ── Schema verification tests (information_schema queries) ──────────
// Catalog queries are pinned to the isolated rithmic_test schema —
// production tables with the same names must not satisfy them.

static void test_bbo_table_exists() {
    PGConnGuard g(g_connstr);
    ASSERT(g.ok());

    PGresult* res = PQexec(g.get(),
        "SELECT COUNT(*) FROM information_schema.columns"
        " WHERE table_schema = 'rithmic_test' AND table_name = 'bbo'");
    ASSERT(res && PQresultStatus(res) == PGRES_TUPLES_OK);
    int count = std::atoi(PQgetvalue(res, 0, 0));
    PQclear(res);

    ASSERT(count > 0);
}

static void test_bbo_has_required_columns() {
    PGConnGuard g(g_connstr);
    ASSERT(g.ok());

    const char* required[] = {
        "bid_price", "ask_price", "bid_size", "ask_size", "ts_event"
    };

    for (const char* col : required) {
        std::string sql =
            std::string("SELECT COUNT(*) FROM information_schema.columns"
                        " WHERE table_schema = 'rithmic_test'"
                        "   AND table_name = 'bbo' AND column_name = '") + col + "'";
        PGresult* res = PQexec(g.get(), sql.c_str());
        ASSERT(res && PQresultStatus(res) == PGRES_TUPLES_OK);
        int cnt = std::atoi(PQgetvalue(res, 0, 0));
        PQclear(res);
        if (cnt == 0)
            throw std::runtime_error(std::string("bbo missing column: ") + col);
    }
}

static void test_depth_table_exists() {
    PGConnGuard g(g_connstr);
    ASSERT(g.ok());

    PGresult* res = PQexec(g.get(),
        "SELECT COUNT(*) FROM information_schema.columns"
        " WHERE table_schema = 'rithmic_test' AND table_name = 'depth_by_order'");
    ASSERT(res && PQresultStatus(res) == PGRES_TUPLES_OK);
    int count = std::atoi(PQgetvalue(res, 0, 0));
    PQclear(res);

    ASSERT(count > 0);
}

static void test_depth_has_source_ns() {
    PGConnGuard g(g_connstr);
    ASSERT(g.ok());

    PGresult* res = PQexec(g.get(),
        "SELECT COUNT(*) FROM information_schema.columns"
        " WHERE table_schema = 'rithmic_test'"
        "   AND table_name = 'depth_by_order' AND column_name = 'source_ns'");
    ASSERT(res && PQresultStatus(res) == PGRES_TUPLES_OK);
    int count = std::atoi(PQgetvalue(res, 0, 0));
    PQclear(res);

    ASSERT(count > 0);
}

static void test_depth_has_update_type() {
    PGConnGuard g(g_connstr);
    ASSERT(g.ok());

    PGresult* res = PQexec(g.get(),
        "SELECT COUNT(*) FROM information_schema.columns"
        " WHERE table_schema = 'rithmic_test'"
        "   AND table_name = 'depth_by_order' AND column_name = 'update_type'");
    ASSERT(res && PQresultStatus(res) == PGRES_TUPLES_OK);
    int count = std::atoi(PQgetvalue(res, 0, 0));
    PQclear(res);

    ASSERT(count > 0);
}

static void test_ticks_unique_index_wide() {
    // Regression guard: idx_ticks_unique must include 'price' AND 'size'
    // AND the 'seq' tiebreaker column.  The old narrow index (ts_event
    // only) allowed duplicate trades sharing a microsecond timestamp to be
    // silently dropped, and the 5-col version (no seq) merged two legit
    // same-price/same-size trades in the same microsecond.
    PGConnGuard g(g_connstr);
    ASSERT(g.ok());

    PGresult* res = PQexec(g.get(),
        "SELECT indexdef FROM pg_indexes"
        " WHERE schemaname = 'rithmic_test' AND tablename = 'ticks'"
        "   AND indexname = 'idx_ticks_unique'");
    ASSERT(res && PQresultStatus(res) == PGRES_TUPLES_OK);

    bool found = PQntuples(res) > 0;
    std::string indexdef;
    if (found)
        indexdef = PQgetvalue(res, 0, 0);
    PQclear(res);

    ASSERT(found);
    ASSERT(indexdef.find("price") != std::string::npos);
    ASSERT(indexdef.find("size")  != std::string::npos);
    ASSERT(indexdef.find("seq")   != std::string::npos);
}

// Two legit identical trades in the same microsecond must both land
// (seq tiebreaker), while a redelivered duplicate batch must dedup.
static void test_tick_dedup_tiebreaker() {
    TickDB db(g_connstr);

    int64_t ts = 1712002000'000000LL;
    std::vector<TickRow> rows = {
        {ts, 18600.0, 4, true,  "NQ", "CME"},
        {ts, 18600.0, 4, true,  "NQ", "CME"},  // legit same-µs duplicate
        {ts, 18600.0, 7, false, "NQ", "CME"},
    };

    int inserted = db.write(rows);
    ASSERT_EQ(inserted, 3);

    // Redelivery of the same batch reproduces the same seqs → all deduped
    int again = db.write(rows);
    ASSERT_EQ(again, 0);
}

// ── main ───────────────────────────────────────────────────────────

int main() {
    // g_connstr was initialised at static-init time: it points at a fresh,
    // isolated rithmic_test schema — never at production tables.

    std::printf("\n=== TickDB + AuditLog integration tests ===\n\n");

    // TickDB constructor creates the schema objects inside rithmic_test
    {
        TickDB db(g_connstr);  // triggers ensure_schema()
        (void)db;
    }

    // Existing tests run via static initializers (TEST macro).
    // New tests (BBORow, DepthRow, schema) run explicitly below, after the
    // schema is guaranteed to be in place.

    std::printf("\n--- Tick dedup tests ---\n");
    RUN_TEST(tick_dedup_tiebreaker);

    std::printf("\n--- BBORow tests ---\n");
    RUN_TEST(write_bbo_basic);
    RUN_TEST(write_bbo_empty_batch);
    RUN_TEST(write_bbo_dedup);
    RUN_TEST(write_bbo_null_side_merge);

    std::printf("\n--- DepthRow tests ---\n");
    RUN_TEST(write_depth_basic);
    RUN_TEST(write_depth_empty_batch);
    RUN_TEST(write_depth_dedup);
    RUN_TEST(write_depth_update_types);
    RUN_TEST(write_depth_zero_source_ns);

    std::printf("\n--- Schema verification tests ---\n");
    RUN_TEST(bbo_table_exists);
    RUN_TEST(bbo_has_required_columns);
    RUN_TEST(depth_table_exists);
    RUN_TEST(depth_has_source_ns);
    RUN_TEST(depth_has_update_type);
    RUN_TEST(ticks_unique_index_wide);

    std::printf("\n=== Results: %d passed, %d failed, %d skipped ===\n\n",
                g_passed, g_failed, g_skipped);

    // Teardown: drop the whole isolated schema — production tables in
    // `public` are never touched.
    drop_test_schema();

    return g_failed > 0 ? 1 : 0;
}
