// test_connection — step-by-step Rithmic + PostgreSQL pipeline test.
//
// Runs every stage of the data flow, times each one, and prints a report:
//   PostgreSQL connect → schema
//   TCP + SSL + WebSocket connect
//   RequestRithmicSystemInfo (validate system name)
//   Login
//   Subscribe
//   Receive N live ticks (wire latency)
//   DB write (UNNEST batch)
//   DB read-back (COUNT)
//
// Usage:  ./build/test_connection  [path/to/.env]
//         ./build/test_connection  --unit     (offline unit tests, no network/DB)

#include <chrono>
#include <cstdio>
#include <exception>
#include <optional>
#include <string>

#include <boost/asio.hpp>

#include "client.hpp"
#include "config.hpp"
#include "db.hpp"
#include "rithmic.pb.h"

namespace asio = boost::asio;

static void banner(const char* text) {
    const int W = 58;
    std::string line(W, '=');
    std::printf("\n%s\n  %s\n%s\n", line.c_str(), text, line.c_str());
}

// ── Offline unit tests (--unit) ────────────────────────────────────

static int unit_failures = 0;
#define CHECK(cond) do { \
    if (cond) std::printf("  ✓  %s\n", #cond); \
    else { std::printf("  ✗  %s  (%s:%d)\n", #cond, __FILE__, __LINE__); \
           ++unit_failures; } \
} while (0)

// Build a Rithmic wire frame: 4-byte big-endian length prefix + payload
static std::string make_wire(const std::string& payload, uint32_t declared_len) {
    uint32_t be = __builtin_bswap32(declared_len);
    std::string wire(reinterpret_cast<char*>(&be), 4);
    return wire + payload;
}

static int run_unit_tests() {
    banner("Unit tests (offline)");

    // ── strip_header validation ──────────────────────────────────
    CHECK(RithmicClient::strip_header(make_wire("abc", 3)) == "abc");
    CHECK(RithmicClient::strip_header(make_wire("", 0)).empty());

    bool threw_short = false;
    try { RithmicClient::strip_header("ab"); }
    catch (const std::runtime_error&) { threw_short = true; }
    CHECK(threw_short);

    bool threw_mismatch = false;
    try { RithmicClient::strip_header(make_wire("abc", 999)); }
    catch (const std::runtime_error&) { threw_mismatch = true; }
    CHECK(threw_mismatch);

    // ── BBO presence-bit mapping (dispatch_message) ──────────────
    asio::io_context ioc;
    Config cfg;   // default cert_path resolves from repo root
    RithmicClient client(ioc, cfg);

    std::optional<BBORow>   got_bbo;
    std::optional<DepthRow> got_depth;
    int tick_calls = 0;
    client.set_on_bbo([&](BBORow r)     { got_bbo   = std::move(r); });
    client.set_on_depth([&](DepthRow r) { got_depth = std::move(r); });
    client.set_on_tick([&](TickRow)     { ++tick_calls; });

    constexpr int32_t PRESENCE_BID = 0x1;
    constexpr int32_t PRESENCE_ASK = 0x2;

    auto make_bbo = [](int32_t presence, double bid, double ask) {
        rti::BestBidOffer bbo;
        bbo.set_template_id(151);
        bbo.set_presence_bits(presence);
        if (bid > 0) bbo.set_bid_price(bid);
        if (ask > 0) bbo.set_ask_price(ask);
        bbo.set_ssboe(1'700'000'000);
        bbo.set_usecs(123'456);
        return bbo.SerializeAsString();
    };

    // Both sides present
    got_bbo.reset();
    client.dispatch_message(make_bbo(PRESENCE_BID | PRESENCE_ASK, 21000.5, 21001.0));
    CHECK(got_bbo.has_value());
    CHECK(got_bbo && got_bbo->bid_price && *got_bbo->bid_price == 21000.5);
    CHECK(got_bbo && got_bbo->ask_price && *got_bbo->ask_price == 21001.0);
    CHECK(got_bbo && got_bbo->symbol == "NQ" && got_bbo->exchange == "CME");

    // Bid side only — ask must be nullopt (written as NULL by db layer)
    got_bbo.reset();
    client.dispatch_message(make_bbo(PRESENCE_BID, 21000.5, 0));
    CHECK(got_bbo.has_value());
    CHECK(got_bbo && got_bbo->bid_price.has_value());
    CHECK(got_bbo && !got_bbo->ask_price.has_value());

    // Ask side only
    got_bbo.reset();
    client.dispatch_message(make_bbo(PRESENCE_ASK, 0, 21001.0));
    CHECK(got_bbo.has_value());
    CHECK(got_bbo && !got_bbo->bid_price.has_value());
    CHECK(got_bbo && got_bbo->ask_price.has_value());

    // Neither side present — update dropped entirely
    got_bbo.reset();
    client.dispatch_message(make_bbo(0, 0, 0));
    CHECK(!got_bbo.has_value());

    // Presence bit set but price is 0 — treated as absent, dropped
    got_bbo.reset();
    client.dispatch_message(make_bbo(PRESENCE_BID, 0, 0));
    CHECK(!got_bbo.has_value());

    // ── DepthByOrder prev_depth_price_flag ───────────────────────
    auto make_depth = [](bool prev_flag, double prev_price) {
        rti::DepthByOrder dbo;
        dbo.set_template_id(160);
        dbo.set_depth_price(21000.5);
        dbo.set_depth_size(2);
        dbo.set_prev_depth_price_flag(prev_flag);
        if (prev_flag) dbo.set_prev_depth_price(prev_price);
        dbo.set_ssboe(1'700'000'000);
        return dbo.SerializeAsString();
    };

    got_depth.reset();
    client.dispatch_message(make_depth(false, 0));
    CHECK(got_depth.has_value());
    CHECK(got_depth && !got_depth->prev_depth_price.has_value());

    got_depth.reset();
    client.dispatch_message(make_depth(true, 21000.25));
    CHECK(got_depth && got_depth->prev_depth_price.has_value());
    CHECK(got_depth && *got_depth->prev_depth_price == 21000.25);

    // ── Malformed proto frames are dropped, never dispatched ─────
    got_bbo.reset();
    got_depth.reset();
    client.dispatch_message(std::string("\x0f\x0f", 2));  // invalid wire type
    CHECK(!got_bbo.has_value() && !got_depth.has_value() && tick_calls == 0);

    std::printf("\n  %s (%d failure%s)\n\n",
                unit_failures == 0 ? "UNIT PASS" : "UNIT FAIL",
                unit_failures, unit_failures == 1 ? "" : "s");
    return unit_failures == 0 ? 0 : 1;
}

int main(int argc, char* argv[]) {
    if (argc > 1 && std::string(argv[1]) == "--unit")
        return run_unit_tests();

    const char* env_path = argc > 1 ? argv[1] : ".env";

    banner("Rithmic Engine — Connection & Data-Flow Test");

    // ── Load config ────────────────────────────────────────────────
    Config cfg;
    try {
        cfg = Config::from_env(env_path);
    } catch (std::exception& e) {
        std::fprintf(stderr, "Config error: %s\n", e.what());
        return 1;
    }
    auto errs = cfg.validate();
    if (!errs.empty()) {
        for (auto& e : errs) std::fprintf(stderr, "  %s\n", e.c_str());
        std::fprintf(stderr, "  → Copy .env.example to .env and fill in credentials.\n");
        return 1;
    }

    // ── PostgreSQL ─────────────────────────────────────────────────
    std::printf("\n[1] PostgreSQL  (%s:%s/%s)\n",
                cfg.pg_host.c_str(), cfg.pg_port.c_str(), cfg.pg_db.c_str());

    std::unique_ptr<TickDB> db;
    {
        auto t0 = std::chrono::steady_clock::now();
        try {
            db = std::make_unique<TickDB>(cfg.pg_connstr());
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - t0)
                          .count();
            std::printf("  ✓  Connected + schema ready         %5lld ms\n",
                        (long long)ms);
        } catch (std::exception& e) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - t0)
                          .count();
            std::printf("  ✗  Failed (%lld ms): %s\n", (long long)ms, e.what());
            return 1;
        }
    }

    // ── Rithmic pipeline ───────────────────────────────────────────
    std::printf("\n[2] Rithmic AMP  (%s)\n", cfg.url.c_str());
    std::printf("    Waiting for %s/%s ticks (60 s timeout)...\n\n",
                cfg.symbol.c_str(), cfg.exchange.c_str());

    ConnectionTestResult result;
    asio::io_context ioc;
    RithmicClient    client(ioc, cfg);

    asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            result = co_await client.run_connection_test(*db, 5);
        }(),
        [&](std::exception_ptr ep) {
            if (ep) {
                try { std::rethrow_exception(ep); }
                catch (std::exception& e) {
                    std::fprintf(stderr, "  Fatal: %s\n", e.what());
                }
            }
            ioc.stop();
        });

    ioc.run();

    // ── Print results ──────────────────────────────────────────────
    result.print();

    // ── Summary ────────────────────────────────────────────────────
    std::printf("\n");
    if (result.all_ok()) {
        std::printf("  Result: PASS ✓  full data-flow verified\n");
    } else {
        std::printf("  Result: FAIL ✗  see ✗ steps above\n");
    }
    std::printf("%s\n\n", std::string(58, '=').c_str());

    return result.all_ok() ? 0 : 1;
}
