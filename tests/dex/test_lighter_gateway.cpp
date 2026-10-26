/*  test_lighter_gateway.cpp — the OFFLINE core of the Lighter live order gateway (src/dex/live/): signer wrapper
    (pinned .so, allowlist, range guards), mainnet-only config guard, market sizing, nonce manager, rate limiter +
    batcher, order state machine, risk limits with persisted baselines, the probe plan, and a FakeExchange that
    consumes the signer's real tx_info JSON.

    NO NETWORK: the signer client is created with an empty URL and explicit nonces only; nothing is sent anywhere.
    Keys are throwaway (GenerateAPIKey, in memory).                                                        */
#include "dex/live/gateway.hpp"
#include "dex/live/gateway_config.hpp"
#include "dex/live/market_spec.hpp"
#include "dex/live/nonce.hpp"
#include "dex/live/order_state.hpp"
#include "dex/live/probe.hpp"
#include "dex/live/risk.hpp"
#include "dex/live/signer.hpp"
#include "dex/live/tx_scheduler.hpp"
#include "lighter_fake_exchange.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

static int tests_run = 0, tests_failed = 0;
#define TEST(name) void test_##name()
#define RUN(name) do { ++tests_run; \
    try { test_##name(); std::cout << "PASS " #name "\n"; } \
    catch (std::exception& e) { ++tests_failed; std::cout << "FAIL " #name ": " << e.what() << "\n"; } \
} while (0)
#define ASSERT(c) do { if (!(c)) throw std::runtime_error("Assert failed: " #c); } while (0)
#define ASSERT_EQ(a, b) do { if ((a) != (b)) throw std::runtime_error("ASSERT_EQ failed: " #a " != " #b); } while (0)
#define ASSERT_THROWS(expr, Ex) do { bool thrown_ = false; try { (void)(expr); } catch (const Ex&) { thrown_ = true; } \
    if (!thrown_) throw std::runtime_error("expected " #Ex " from: " #expr); } while (0)

using namespace lighter::live;
using json = nlohmann::json;
namespace fs = std::filesystem;

static constexpr int64_t kAcct = 281474;
static constexpr int kKey = 4;

static int64_t wall_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
static int64_t wall_s() { return wall_ms() / 1000; }

static fs::path scratch() {
    static fs::path p = [] {
        std::string t = (fs::temp_directory_path() / "lgw_test_XXXXXX").string();
        if (!::mkdtemp(t.data())) throw std::runtime_error("mkdtemp");
        return fs::path(t);
    }();
    return p;
}
static fs::path fresh_dir(const std::string& name) {
    fs::path d = scratch() / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}
static void write_file(const fs::path& p, const std::string& s, mode_t mode = 0600) {
    { std::ofstream f(p); f << s; }
    ::chmod(p.c_str(), mode);
}
static std::string today_str(int64_t offset_days = 0) {
    std::time_t t = std::time_t(wall_s() + offset_days * 86400);
    std::tm tm{};
    ::gmtime_r(&t, &tm);
    char b[16];
    std::strftime(b, sizeof b, "%Y-%m-%d", &tm);
    return b;
}

// Decimals from lighter_tx_spec.md §7 (mainnet, recorded 2026-10-26). No network fetch.
static MarketSpec btc() { return MarketSpec::from_strings("BTC", 1, 5, 1, "0.00007", "10"); }
static MarketSpec eth() { return MarketSpec::from_strings("ETH", 0, 4, 2, "0.0020", "10"); }
static MarketSpec sol() { return MarketSpec::from_strings("SOL", 2, 3, 3, "0.055", "10"); }
static MarketSpec bnb() { return MarketSpec::from_strings("BNB", 25, 2, 4, "0.01", "10"); }
static MarketSpec aave() { return MarketSpec::from_strings("AAVE", 27, 3, 3, "0.045", "10"); }

static Signer& signer() {
    static Signer s(Signer::Options{kSignerPath, kSignerSha256, /*allow_generate_key*/ true});
    return s;
}
static void ensure_client() {
    static bool done = false;
    if (done) return;
    ApiKeyPair k = signer().generate_api_key();   // throwaway, in memory only
    signer().create_client(k.private_key, 304, kKey, kAcct);
    std::fill(k.private_key.begin(), k.private_key.end(), '\0');
    done = true;
}

// ───────────────────────────── signer ─────────────────────────────

TEST(signer_wrong_hash_is_refused_before_load) {   // runs FIRST: the library is not loaded yet
    ASSERT_THROWS(Signer(Signer::Options{kSignerPath, std::string(64, '0'), false}), SignerError);
    ASSERT(Signer::resolved_symbols().empty());
    ASSERT_THROWS(Signer(Signer::Options{"/nonexistent/lighter.so", kSignerSha256, false}), SignerError);
}

TEST(signer_resolves_only_the_allowlist) {
    signer();
    auto got = Signer::resolved_symbols();
    std::vector<std::string> want = {"CreateClient", "CheckClient", "SignCreateOrder", "SignModifyOrder", "SignCancelOrder",
        "SignCancelAllOrders", "SignCreateGroupedOrders", "CreateAuthToken", "Free", "GenerateAPIKey"};
    ASSERT_EQ(got, want);
    for (const char* bad : {"SignWithdraw", "SignTransfer", "SignChangePubKey", "SignCreateSubAccount", "SignUpdateLeverage",
                            "SignUpdateMargin", "SignMintShares", "SignBurnShares", "SignStakeAssets", "SignUnstakeAssets",
                            "SignApproveIntegrator", "SignCreatePublicPool", "SignUpdatePublicPool",
                            "SignUpdateAccountConfig", "SignUpdateAccountAssetConfig"}) {
        ASSERT(!symbol_allowed(bad));
        for (auto& g : got) ASSERT(g != bad);
    }
    // the library was loaded with the async-preemption guard in the EXEC-time environment (what Go reads)
    const char* gd = std::getenv("GODEBUG");
    ASSERT(gd && std::string(gd).find("asyncpreemptoff=1") != std::string::npos);
    ASSERT(godebug_in_initial_environment());
    // a second load with the same path/hash reuses the one Go runtime; a different hash is refused
    Signer again;
    ASSERT_EQ(Signer::resolved_symbols().size(), want.size());
    ASSERT_THROWS(Signer(Signer::Options{kSignerPath, std::string(64, 'f'), false}), SignerError);
}

TEST(signer_signs_a_post_only_create_offline) {
    ensure_client();
    CreateOrderParams p;
    p.market_index = 0; p.client_order_index = 123; p.base_amount = 48; p.price = 232574; p.is_ask = false;
    p.tif = TimeInForce::PostOnly; p.order_expiry_ms = wall_ms() + 600'000; p.nonce = 7;
    SignedTx t = signer().sign_create_order(p, kKey, kAcct);
    ASSERT_EQ(int(t.tx_type), 14);
    json j = json::parse(t.tx_info);
    ASSERT_EQ(j.at("Nonce").get<int64_t>(), 7);
    ASSERT_EQ(j.at("MarketIndex").get<int>(), 0);
    ASSERT_EQ(j.at("ClientOrderIndex").get<int64_t>(), 123);
    ASSERT_EQ(j.at("BaseAmount").get<int64_t>(), 48);
    ASSERT_EQ(j.at("Price").get<int64_t>(), 232574);
    ASSERT_EQ(j.at("TimeInForce").get<int>(), 2);
    ASSERT_EQ(j.at("AccountIndex").get<int64_t>(), kAcct);
    ASSERT_EQ(j.at("ApiKeyIndex").get<int>(), kKey);
    ASSERT(!j.at("Sig").get<std::string>().empty());
    ASSERT_EQ(t.tx_hash.size(), size_t{80});
}

TEST(signer_price_above_int31_is_bit_cast_not_truncated) {
    ensure_client();
    CreateOrderParams p;
    p.market_index = 1; p.client_order_index = 124; p.base_amount = 10; p.price = 3'000'000'000LL;
    p.tif = TimeInForce::PostOnly; p.order_expiry_ms = wall_ms() + 600'000; p.nonce = 8;
    json j = json::parse(signer().sign_create_order(p, kKey, kAcct).tx_info);
    ASSERT_EQ(j.at("Price").get<int64_t>(), 3'000'000'000LL);
}

TEST(signer_truncation_guards_fire_before_the_library_is_called) {
    ensure_client();
    CreateOrderParams ok;
    ok.market_index = 0; ok.client_order_index = 1; ok.base_amount = 1; ok.price = 1;
    ok.tif = TimeInForce::PostOnly; ok.order_expiry_ms = wall_ms() + 600'000; ok.nonce = 0;
    uint64_t before = Signer::library_calls();
    auto bad = [&](auto mut) { CreateOrderParams p = ok; mut(p); ASSERT_THROWS(signer().sign_create_order(p, kKey, kAcct), RangeError); };
    bad([](CreateOrderParams& p) { p.market_index = 32768; });           // int16 truncation
    bad([](CreateOrderParams& p) { p.market_index = 255; });             // nil sentinel
    bad([](CreateOrderParams& p) { p.market_index = -1; });
    bad([](CreateOrderParams& p) { p.price = int64_t{1} << 32; });       // uint32 truncation
    bad([](CreateOrderParams& p) { p.price = 0; });
    bad([](CreateOrderParams& p) { p.client_order_index = 0; });
    bad([](CreateOrderParams& p) { p.client_order_index = int64_t{1} << 48; });
    bad([](CreateOrderParams& p) { p.base_amount = 0; });
    bad([](CreateOrderParams& p) { p.base_amount = int64_t{1} << 48; });
    bad([](CreateOrderParams& p) { p.nonce = -1; });                     // never the HTTP nonce lookup
    bad([](CreateOrderParams& p) { p.order_expiry_ms = -1; });           // no implicit 28-day expiry
    bad([](CreateOrderParams& p) { p.tif = TimeInForce::IOC; });         // IOC needs expiry 0
    bad([](CreateOrderParams& p) { p.self_trade_mode = 4; });
    ASSERT_THROWS(signer().sign_create_order(ok, 3, kAcct), RangeError);     // api key 0-3 reserved
    ASSERT_THROWS(signer().sign_create_order(ok, 255, kAcct), RangeError);   // sentinel
    ASSERT_THROWS(signer().sign_create_order(ok, 256, kAcct), RangeError);   // uint8 truncation
    ASSERT_THROWS(signer().sign_create_order(ok, kKey, 0), RangeError);
    ASSERT_EQ(Signer::library_calls(), before);
}

TEST(signer_modify_and_cancel_take_client_or_exchange_index) {
    ensure_client();
    uint64_t before = Signer::library_calls();
    ModifyOrderParams m;
    m.market_index = 0; m.base_amount = 48; m.price = 232000; m.nonce = 9;
    m.order_ref = 0;
    ASSERT_THROWS(signer().sign_modify_order(m, kKey, kAcct), RangeError);
    m.order_ref = int64_t{1} << 60;                 // above the exchange range
    ASSERT_THROWS(signer().sign_modify_order(m, kKey, kAcct), RangeError);
    m.order_ref = 123; m.base_amount = 0;           // size 0 on modify is unconfirmed → refused
    ASSERT_THROWS(signer().sign_modify_order(m, kKey, kAcct), RangeError);
    ASSERT_THROWS(signer().sign_cancel_order(0, 0, 10, kKey, kAcct), RangeError);
    ASSERT_EQ(Signer::library_calls(), before);
    m.base_amount = 48;
    SignedTx t = signer().sign_modify_order(m, kKey, kAcct);
    ASSERT_EQ(int(t.tx_type), 17);
    ASSERT_EQ(json::parse(t.tx_info).at("Index").get<int64_t>(), 123);
    SignedTx c = signer().sign_cancel_order(0, (int64_t{1} << 48) + 5, 10, kKey, kAcct);   // exchange index
    ASSERT_EQ(int(c.tx_type), 15);
    ASSERT_EQ(json::parse(c.tx_info).at("Index").get<int64_t>(), (int64_t{1} << 48) + 5);
}

TEST(signer_cancel_all_rules) {
    ensure_client();
    ASSERT_THROWS(signer().sign_cancel_all(CancelAllTif::Scheduled, wall_ms() + 60'000, 0, 11, kKey, kAcct), RangeError);
    ASSERT_THROWS(signer().sign_cancel_all(CancelAllTif::Immediate, 5, 255, 11, kKey, kAcct), RangeError);
    ASSERT_THROWS(signer().sign_cancel_all(CancelAllTif::Scheduled, 0, 255, 11, kKey, kAcct), RangeError);
    SignedTx t = signer().sign_cancel_all(CancelAllTif::Immediate, 0, 255, 11, kKey, kAcct);
    ASSERT_EQ(int(t.tx_type), 16);
    SignedTx s = signer().sign_cancel_all(CancelAllTif::Scheduled, wall_ms() + 600'000, 255, 12, kKey, kAcct);
    ASSERT_EQ(json::parse(s.tx_info).at("Nonce").get<int64_t>(), 12);
}

TEST(signer_client_is_mainnet_only_and_offline) {
    uint64_t before = Signer::library_calls();
    ASSERT_THROWS(signer().create_client("0x00", 300, kKey, kAcct), RangeError);   // testnet chain id
    ASSERT_THROWS(signer().create_client("0x00", 304, 3, kAcct), RangeError);
    ASSERT_THROWS(signer().create_client("", 304, kKey, kAcct), RangeError);
    ASSERT_EQ(Signer::library_calls(), before);
    ensure_client();
    // With url "" the signer has no HTTP client: CheckClient can only fail locally (no socket).
    ASSERT(!signer().check_client(kKey, kAcct).empty());
}

TEST(signer_auth_token_deadline_rules) {
    ensure_client();
    int64_t now = wall_s();
    ASSERT_THROWS(signer().create_auth_token(now + 8 * 3600 + 1, now, kKey, kAcct), RangeError);
    ASSERT_THROWS(signer().create_auth_token(now, now, kKey, kAcct), RangeError);
    std::string tok = signer().create_auth_token(now + 600, now, kKey, kAcct);
    std::string prefix = std::to_string(now + 600) + ":" + std::to_string(kAcct) + ":" + std::to_string(kKey) + ":";
    ASSERT(tok.rfind(prefix, 0) == 0);
}

// ───────────────────────────── config / network guard ─────────────────────────────

static json base_cfg(const std::string& mode, const fs::path& dir) {
    return json{{"mode", mode}, {"host", kMainnetHost}, {"chain_id", 304}, {"account_index", kAcct},
                {"api_key_index", kKey}, {"key_file", (dir / "mainnet.key").string()},
                {"arming_file", (dir / "ARM_MAINNET").string()}, {"no_deploy_file", (dir / "NO_DEPLOY_MM").string()},
                {"state_dir", (dir / "state").string()}, {"market", "ETH"}};
}

TEST(config_testnet_host_and_wrong_chain_are_refused) {
    fs::path d = fresh_dir("cfg_net");
    json c = base_cfg("offline", d);
    c["host"] = kTestnetHost;
    ASSERT_THROWS(parse_config(c, Mode::Offline, wall_s()), ConfigError);
    c["host"] = "https://testnet.zklighter.elliot.ai";
    ASSERT_THROWS(parse_config(c, Mode::Offline, wall_s()), ConfigError);
    c["host"] = "mainnet.zklighter.elliot.ai.evil.example";
    ASSERT_THROWS(parse_config(c, Mode::Offline, wall_s()), ConfigError);
    c["host"] = kMainnetHost; c["chain_id"] = 300;
    ASSERT_THROWS(parse_config(c, Mode::Offline, wall_s()), ConfigError);
    c["chain_id"] = 304;
    GatewayConfig g = parse_config(c, Mode::Offline, wall_s());
    ASSERT(g.mode == Mode::Offline);
    ASSERT_EQ(g.max_txs_per_batch, 15);
    ASSERT_EQ(g.rate_budget_per_min, 54);
    ASSERT_EQ(g.risk.daily_stop_micro, -1'500'000);
}

TEST(config_unknown_keys_and_mode_mismatch_are_refused) {
    fs::path d = fresh_dir("cfg_keys");
    json c = base_cfg("offline", d);
    c["typo_key"] = 1;
    ASSERT_THROWS(parse_config(c, Mode::Offline, wall_s()), ConfigError);
    c = base_cfg("offline", d);
    c["risk"] = {{"daily_stop_usd", -1.5}, {"leverege", 5}};
    ASSERT_THROWS(parse_config(c, Mode::Offline, wall_s()), ConfigError);
    c = base_cfg("offline", d);
    ASSERT_THROWS(parse_config(c, Mode::Live, wall_s()), ConfigError);    // CLI says live, config says offline
    c["mode"] = "turbo";
    ASSERT_THROWS(parse_config(c, Mode::Offline, wall_s()), ConfigError);
    c = base_cfg("offline", d);
    c["risk"] = {{"max_markets", 2}};
    ASSERT_THROWS(parse_config(c, Mode::Offline, wall_s()), ConfigError);
}

TEST(config_missing_arming_file_refuses_probe_and_live) {
    fs::path d = fresh_dir("cfg_arm");
    for (const char* m : {"probe", "live"}) {
        json c = base_cfg(m, d);
        ASSERT_THROWS(parse_config(c, parse_mode(m), wall_s()), ConfigError);
    }
    parse_config(base_cfg("offline", d), Mode::Offline, wall_s());   // offline needs no arming
    write_file(d / "ARM_MAINNET", std::to_string(kAcct) + " " + today_str() + "\n");
    GatewayConfig g = parse_config(base_cfg("live", d), Mode::Live, wall_s());
    ASSERT(g.mode == Mode::Live);
    write_file(d / "ARM_MAINNET", std::to_string(kAcct) + " " + today_str(-8) + "\n");   // stale
    ASSERT_THROWS(parse_config(base_cfg("live", d), Mode::Live, wall_s()), ConfigError);
    write_file(d / "ARM_MAINNET", std::to_string(kAcct) + " " + today_str(1) + "\n");    // future
    ASSERT_THROWS(parse_config(base_cfg("live", d), Mode::Live, wall_s()), ConfigError);
    write_file(d / "ARM_MAINNET", "999 " + today_str() + "\n");                          // other account
    ASSERT_THROWS(parse_config(base_cfg("probe", d), Mode::Probe, wall_s()), ConfigError);
    write_file(d / "ARM_MAINNET", std::to_string(kAcct) + " " + today_str() + "\n");
    write_file(d / "NO_DEPLOY_MM", "");
    ASSERT_THROWS(parse_config(base_cfg("live", d), Mode::Live, wall_s()), ConfigError);
}

static std::string key_json(int64_t acct = kAcct, int key = kKey, const std::string& net = "mainnet") {
    return json{{"network", net}, {"account_index", acct}, {"api_key_index", key}, {"private_key", "0xdeadbeef"},
                {"l1_address", "0x0"}}.dump();
}

TEST(key_file_permissions_and_location) {
    fs::path d = fresh_dir("keys");
    ASSERT(!inside_git_tree(d / "x"));
    GatewayConfig g = parse_config(base_cfg("offline", d), Mode::Offline, wall_s());
    fs::path repo = fs::path(__FILE__).parent_path().parent_path().parent_path();   // this repo
    write_file(d / "mainnet.key", key_json(), 0644);
    ASSERT_THROWS(load_key_file(g, repo.string()), ConfigError);                       // group/world readable
    ::chmod((d / "mainnet.key").c_str(), 0600);
    KeyMaterial km = load_key_file(g, repo.string());
    ASSERT_EQ(km.account_index, kAcct);
    ASSERT_EQ(km.private_key, std::string("0xdeadbeef"));
    // inside "the repo"
    ASSERT_THROWS(load_key_file(g, d.string()), ConfigError);
    // inside some git work tree
    fs::path gt = fresh_dir("gittree");
    fs::create_directories(gt / ".git");
    fs::create_directories(gt / "sub");
    write_file(gt / "sub" / "mainnet.key", key_json(), 0600);
    GatewayConfig g2 = g; g2.key_file = (gt / "sub" / "mainnet.key").string();
    ASSERT_THROWS(load_key_file(g2, repo.string()), ConfigError);
    // symlink
    fs::create_symlink(d / "mainnet.key", d / "link.key");
    GatewayConfig g3 = g; g3.key_file = (d / "link.key").string();
    ASSERT_THROWS(load_key_file(g3, repo.string()), ConfigError);
    // contents must match the config
    write_file(d / "mainnet.key", key_json(kAcct + 1), 0600);
    ASSERT_THROWS(load_key_file(g, repo.string()), ConfigError);
    write_file(d / "mainnet.key", key_json(kAcct, kKey, "testnet"), 0600);
    ASSERT_THROWS(load_key_file(g, repo.string()), ConfigError);
    write_file(d / "mainnet.key", json{{"network", "mainnet"}, {"account_index", kAcct}, {"api_key_index", kKey},
                                      {"private_key", "0x1"}, {"seed_phrase", "x"}}.dump(), 0600);
    ASSERT_THROWS(load_key_file(g, repo.string()), ConfigError);                       // unknown field
}

// ───────────────────────────── market spec / sizing ─────────────────────────────

TEST(decimal_parsing_is_exact) {
    ASSERT_EQ(parse_decimal("0.0020", 4), 20);
    ASSERT_EQ(parse_decimal("2325.74", 2), 232574);
    ASSERT_EQ(parse_decimal("10", 6), 10'000'000);
    ASSERT_EQ(parse_decimal("0.00007", 5), 7);
    ASSERT_EQ(parse_decimal("1.50000", 2), 150);          // trailing zeros beyond the grid are fine
    ASSERT_THROWS(parse_decimal("2325.745", 2), SpecError); // never silently rounded
    ASSERT_THROWS(parse_decimal("-1", 2), SpecError);
    ASSERT_THROWS(parse_decimal("1e3", 2), SpecError);
}

TEST(eleven_dollar_quotes_round_up_on_btc_eth_sol_and_the_coarse_alt_is_refused) {
    SizingRules r;   // $11 target, $12 cap, min_quote × 1.05
    // BTC 110000.0: 0.00010 BTC = $11.00
    QuoteSize b = size_quote(btc(), r, btc().price_int("110000.0"));
    ASSERT_EQ(b.base, 10);
    ASSERT_EQ(b.notional_micro, 11'000'000);
    // ETH 2325.74: ceil(11/2325.74, 0.0001) = 0.0048 → $11.16
    QuoteSize e = size_quote(eth(), r, eth().price_int("2325.74"));
    ASSERT_EQ(e.base, 48);
    ASSERT(e.notional_micro >= 11'000'000 && e.notional_micro <= 12'000'000);
    // SOL 180.000: 0.062 SOL = $11.16
    QuoteSize s = size_quote(sol(), r, sol().price_int("180.000"));
    ASSERT_EQ(s.base, 62);
    // AAVE 250.000: $11 → 0.044 but min_base 0.045 binds → $11.25
    QuoteSize a = size_quote(aave(), r, aave().price_int("250.000"));
    ASSERT_EQ(a.base, 45);
    // BNB 737.0000: step 0.01 ≈ $7.37 → smallest valid 0.02 = $14.74 > $12 cap → market refused
    ASSERT_THROWS(size_quote(bnb(), r, bnb().price_int("737.0000")), SpecError);
    QuoteSize bn = smallest_valid(bnb(), r, bnb().price_int("737.0000"));
    ASSERT_EQ(bn.base, 2);
    // rounding DOWN at a lower price would have dropped under $10: our rule re-checks at the signing price
    ASSERT(!validate_order(eth(), r, 47, eth().price_int("2200.00")).empty());   // 0.0047 × 2200 = $10.34 < $10.50
    ASSERT(validate_order(eth(), r, 48, eth().price_int("2325.74")).empty());
}

TEST(order_validation_rejects_min_and_cap) {
    SizingRules r;
    int64_t px = eth().price_int("2325.74");
    ASSERT_EQ(validate_order(eth(), r, 10, px), std::string("below min_base_amount"));
    ASSERT_EQ(validate_order(eth(), r, 44, px), std::string("below min_quote_amount x margin"));   // $10.23
    ASSERT_EQ(validate_order(eth(), r, 52, px), std::string("above notional cap"));               // $12.09
    ASSERT_EQ(validate_order(eth(), r, 48, 0), std::string("price <= 0"));
}

// ───────────────────────────── nonce manager ─────────────────────────────

TEST(nonce_requires_sync_and_is_monotonic) {
    NonceManager nm(kKey);
    ASSERT_THROWS(nm.reserve(), NonceError);
    nm.resync(100);
    ASSERT_EQ(nm.reserve(), 100);
    ASSERT_EQ(nm.reserve(), 101);
    auto v = nm.reserve_consecutive(3);
    ASSERT_EQ(v, (std::vector<int64_t>{102, 103, 104}));
    nm.on_outcome(100, TxOutcome::Executed);
    nm.on_outcome(101, TxOutcome::Executed);
    ASSERT_THROWS(nm.resync(50), NonceError);   // the server went backwards: wrong key/account
}

TEST(nonce_after_reject_follows_the_consumption_rules) {
    NonceManager nm(kKey);
    nm.resync(10);
    // post-only would cross: a successful tx → consumed
    int64_t a = nm.reserve();
    ASSERT(classify_sequencer(kStatusCanceledPostOnly, 0) == TxOutcome::MatchingCancel);
    nm.on_outcome(a, classify_sequencer(kStatusCanceledPostOnly, 0));
    ASSERT_EQ(nm.peek_next(), 11);
    // duplicate client index: applied as a no-op → consumed
    int64_t b = nm.reserve();
    ASSERT(classify_sequencer(-1, 21728) == TxOutcome::SequencerFailedApplying);
    nm.on_outcome(b, TxOutcome::SequencerFailedApplying);
    ASSERT_EQ(nm.peek_next(), 12);
    // API reject (e.g. 429): not consumed → same nonce again; later in-flight nonces must be re-signed
    int64_t c = nm.reserve(), d = nm.reserve(), e = nm.reserve();
    ASSERT_EQ(c, 12);
    auto resign = nm.on_outcome(c, classify_send(429, 23000));
    ASSERT_EQ(resign, (std::vector<int64_t>{d, e}));
    ASSERT_EQ(nm.reserve(), 12);
    ASSERT_EQ(nm.in_flight(), size_t{1});
}

TEST(nonce_unknown_outcome_freezes_until_resync) {
    NonceManager nm(kKey);
    nm.resync(20);
    int64_t a = nm.reserve(), b = nm.reserve(), c = nm.reserve();
    nm.on_outcome(a, TxOutcome::Executed);
    nm.on_outcome(b, classify_send(0, 0));   // timeout: outcome unknown
    ASSERT(nm.frozen());
    ASSERT_THROWS(nm.reserve(), NonceError);
    auto r = nm.resync(22);                   // b is applied; c may still be in transit → unconfirmed, still frozen
    ASSERT_EQ(r.applied, (std::vector<int64_t>{b}));
    ASSERT_EQ(r.unconfirmed, (std::vector<int64_t>{c}));
    ASSERT(r.not_applied.empty());
    ASSERT(nm.frozen());
    nm.confirm_applied(c);                    // a tx-hash lookup says c executed
    nm.resync(23);
    ASSERT(!nm.frozen());
    ASSERT_EQ(nm.reserve(), 23);
    ASSERT(classify_send(400, 21104) == TxOutcome::InvalidNonce);
    ASSERT(classify_send(200, 200) == TxOutcome::Executed);
    ASSERT(classify_send(503, 0) == TxOutcome::Unknown);
}

// ───────────────────────────── rate limiter + batcher ─────────────────────────────

TEST(rate_limiter_rolling_window_with_safety_reserve) {
    RateLimiter rl;   // 54/min, 6 reserved for cancels
    int64_t t = 1'000'000;
    for (int i = 0; i < 48; ++i) { ASSERT(rl.can_send(t, false)); rl.record(t); }
    ASSERT(!rl.can_send(t, false));           // quoting stops at 48
    for (int i = 0; i < 6; ++i) { ASSERT(rl.can_send(t, true)); rl.record(t); }
    ASSERT(!rl.can_send(t, true));            // hard budget 54
    ASSERT(!rl.can_send(t + 59'999, true));
    ASSERT(rl.can_send(t + 60'000, false));   // the window rolled
}

TEST(rate_limit_429_backs_off_sixty_seconds) {
    RateLimiter rl;
    Batcher b;
    int64_t t = 5'000'000;
    rl.on_rate_limited(t);
    Intent ca; ca.kind = IntentKind::CancelAll; ca.market = 255;
    b.submit(ca);
    auto never = [](uint64_t) { return true; };
    ASSERT(b.next_batch(t + 1, rl, never).empty());
    ASSERT(b.next_batch(t + 59'999, rl, never).empty());
    ASSERT_EQ(b.next_batch(t + 60'000, rl, never).size(), size_t{1});
    ASSERT_EQ(rl.rate_limited_count(), 1);
}

static Intent create_i(int64_t market, bool ask, int64_t coi, int64_t px) {
    Intent i; i.kind = IntentKind::Create; i.market = market; i.is_ask = ask; i.slot = slot_key(market, ask);
    i.order_ref = coi; i.base = 48; i.price = px; i.tif = TimeInForce::PostOnly; i.expiry_ms = wall_ms() + 600'000;
    return i;
}

TEST(batch_orders_cancels_then_modifies_then_creates_and_caps_size) {
    Batcher b(15);
    RateLimiter rl;
    for (int k = 0; k < 20; ++k) b.submit(create_i(k + 100, false, 1000 + k, 1000));
    Intent m; m.kind = IntentKind::Modify; m.market = 0; m.slot = slot_key(0, true); m.order_ref = 7; m.base = 48; m.price = 2;
    b.submit(m);
    Intent c; c.kind = IntentKind::Cancel; c.market = 0; c.slot = slot_key(0, false); c.order_ref = 5;
    b.submit(c);
    auto batch = b.next_batch(10'000'000, rl, [](uint64_t) { return true; });
    ASSERT_EQ(batch.size(), size_t{15});
    ASSERT(batch[0].kind == IntentKind::Cancel);
    ASSERT(batch[1].kind == IntentKind::Modify);
    for (size_t k = 2; k < batch.size(); ++k) ASSERT(batch[k].kind == IntentKind::Create);
    ASSERT_EQ(b.queued(), size_t{7});
    // the newest create for a slot replaces the stale queued one
    Batcher b2;
    b2.submit(create_i(0, false, 1, 100));
    b2.submit(create_i(0, false, 2, 101));
    auto one = b2.next_batch(10'000'000, rl, [](uint64_t) { return true; });
    ASSERT_EQ(one.size(), size_t{1});
    ASSERT_EQ(one[0].order_ref, 2);
    ASSERT_EQ(b2.merged(), uint64_t{1});
}

TEST(batch_holds_a_create_while_its_slot_is_busy) {
    Batcher b;
    RateLimiter rl;
    Intent c; c.kind = IntentKind::Cancel; c.market = 0; c.slot = slot_key(0, false); c.order_ref = 5;
    b.submit(c);
    b.submit(create_i(0, false, 6, 100));
    auto batch = b.next_batch(1, rl, [](uint64_t) { return true; });
    ASSERT_EQ(batch.size(), size_t{1});                  // the create waits: its slot's cancel is in this batch
    ASSERT(batch[0].kind == IntentKind::Cancel);
    ASSERT(b.next_batch(2, rl, [](uint64_t) { return false; }).empty());   // old order not terminal yet
    ASSERT_EQ(b.next_batch(3, rl, [](uint64_t) { return true; }).size(), size_t{1});
}

TEST(signed_batch_has_consecutive_nonces_and_the_exchange_accepts_it) {
    ensure_client();
    NonceManager nm(kKey);
    nm.resync(40);
    test::FakeExchange ex(kAcct, wall_ms());
    ex.set_next_nonce(kKey, 40);
    ex.set_book(0, 232500, 232600);
    std::vector<Intent> batch = {create_i(0, false, 501, 232000), create_i(0, true, 502, 233000)};
    Intent cn; cn.kind = IntentKind::Cancel; cn.market = 0; cn.order_ref = 501;
    batch.push_back(cn);
    auto signed_txs = sign_batch(signer(), nm, batch, {kKey, kAcct});
    ASSERT_EQ(signed_txs.size(), size_t{3});
    for (size_t k = 0; k < signed_txs.size(); ++k) ASSERT_EQ(json::parse(signed_txs[k].tx_info).at("Nonce").get<int64_t>(), 40 + int64_t(k));
    std::vector<std::pair<uint8_t, std::string>> wire;
    for (auto& t : signed_txs) wire.push_back({t.tx_type, t.tx_info});
    // out of order → 21105, nothing consumed
    auto swapped = wire; std::swap(swapped[0], swapped[1]);
    ASSERT_EQ(ex.send_batch(swapped).code, 21105);
    ASSERT_EQ(ex.next_nonce(kKey), 40);
    ASSERT_EQ(ex.send_batch(wire).code, 200);
    ASSERT_EQ(ex.next_nonce(kKey), 43);
    ASSERT_EQ(ex.open_orders(), 2);
    ex.advance(300);                                       // the cancel lands after the 300 ms bump
    ASSERT_EQ(ex.open_orders(), 1);
    // a signing failure inside a batch releases every reserved nonce
    Intent bad = create_i(0, false, 0, 232000);            // client index 0 → RangeError
    ASSERT_THROWS(sign_batch(signer(), nm, {create_i(0, false, 503, 232000), bad}, {kKey, kAcct}), RangeError);
    ASSERT_EQ(nm.peek_next(), 43);
}

// ───────────────────────────── order state machine ─────────────────────────────

TEST(order_lifecycle_with_idempotent_partial_fills) {
    OrderTracker ot;
    ot.add_pending(11, 0, false, 48, 232000, 0);
    ot.on_ack(11, (int64_t{1} << 48) + 1, 10);
    ASSERT(ot.get(11).state == OrderState::Open);
    ASSERT(ot.on_fill(900, 11, 20, 20));
    ASSERT(ot.get(11).state == OrderState::PartiallyFilled);
    ASSERT(!ot.on_fill(900, 11, 20, 21));               // same trade id via REST: skipped
    ASSERT_EQ(ot.duplicate_fills(), uint64_t{1});
    ASSERT_EQ(ot.position(0), 20);
    ASSERT(ot.on_fill(901, 11, 28, 30));
    ASSERT(ot.get(11).state == OrderState::Filled);
    ASSERT_EQ(ot.position(0), 48);
    // an overfill on a Filled order is still applied to the position (exchange truth) and flags reconciliation
    ASSERT(!ot.reconcile_needed());
    ASSERT(ot.on_fill(902, 11, 1, 31));
    ASSERT_EQ(ot.position(0), 49);
    ASSERT(ot.reconcile_needed());
}

TEST(order_illegal_transitions_throw) {
    OrderTracker ot;
    ot.add_pending(1, 0, false, 10, 100, 0);
    ASSERT_THROWS(ot.request_modify(1, 101, 10), OrderStateError);   // not acked yet
    ot.on_rejected(1, RejectReason::ApiReject, 1);
    ASSERT_THROWS(ot.request_cancel(1, 2), OrderStateError);
    ASSERT_THROWS(ot.on_ack(1, 5, 2), OrderStateError);
    ASSERT_THROWS(ot.add_pending(1, 0, false, 10, 100, 3), OrderStateError);   // client index reuse
    ot.add_pending(2, 0, false, 10, 100, 0);
    ot.on_ack(2, 7, 1);
    ot.on_cancelled(2, kStatusCanceled, 2);
    ASSERT_THROWS(ot.on_cancelled(2, kStatusCanceled, 3), OrderStateError);
    ASSERT_THROWS(ot.on_fill(1, 2, 0, 4), OrderStateError);   // qty 0
    ASSERT_THROWS(ot.get(99), OrderStateError);
}

TEST(unknown_state_counts_as_exposure_until_the_exchange_confirms) {
    OrderTracker ot;
    ot.add_pending(21, 0, false, 48, 232000, 1'000);
    auto u = ot.on_timeout(3'999, 3'000);
    ASSERT(u.empty());
    u = ot.on_timeout(4'000, 3'000);
    ASSERT_EQ(u, (std::vector<int64_t>{21}));
    ASSERT(ot.get(21).state == OrderState::Unknown);
    // never assumed gone, however long we wait
    ot.on_timeout(1'000'000, 3'000);
    ASSERT(ot.get(21).state == OrderState::Unknown);
    ASSERT_EQ(ot.exposure(0).bid_fillable, 48);
    ASSERT(!ot.slot_free(0, false));
    ASSERT(ot.plan_requote(21, 232100, 48, 0) == RequoteAction::Blocked);
    ot.request_cancel(21, 1'000'001);                     // stays Unknown, cancel noted
    ASSERT(ot.get(21).state == OrderState::Unknown);
    ASSERT(ot.on_fill(700, 21, 8, 1'000'002));            // a fill on an Unknown order applies
    ASSERT_EQ(ot.exposure(0).worst_long(), 48);           // 8 position + 40 remaining
    ot.reconcile(21, kStatusCanceled, 8, (int64_t{1} << 48) + 3, 1'000'003);
    ASSERT(ot.get(21).state == OrderState::Cancelled);
    ASSERT_EQ(ot.exposure(0).bid_fillable, 0);
    ASSERT_EQ(ot.exposure(0).position, 8);
    // reconcile also adopts fills we never saw
    ot.add_pending(22, 0, true, 48, 233000, 0);
    ot.on_timeout(10'000, 3'000);
    ot.reconcile(22, kStatusFilled, 48, 0, 10'001);
    ASSERT_EQ(ot.position(0), 8 - 48);
}

TEST(cancel_pending_order_can_still_fill_during_the_300ms_delay) {
    ensure_client();
    NonceManager nm(kKey);
    nm.resync(0);
    test::FakeExchange ex(kAcct, wall_ms());
    ex.set_book(0, 232500, 232600);
    OrderTracker ot;
    ot.add_pending(31, 0, false, 48, 232000, ex.now());
    auto s = sign_batch(signer(), nm, {create_i(0, false, 31, 232000)}, {kKey, kAcct});
    ASSERT_EQ(ex.send_tx(s[0].tx_type, s[0].tx_info).code, 200);
    for (auto& e : ex.drain()) if (e.type == test::FakeEvent::Ack) ot.on_ack(e.client_index, e.order_index, ex.now());
    Intent cn; cn.kind = IntentKind::Cancel; cn.market = 0; cn.order_ref = 31;
    auto c = sign_batch(signer(), nm, {cn}, {kKey, kAcct});
    ot.request_cancel(31, ex.now());
    ASSERT_EQ(ex.send_tx(c[0].tx_type, c[0].tx_info).code, 200);
    ASSERT_EQ(ot.exposure(0).bid_fillable, 48);            // CancelPending still counts
    ASSERT(ex.hit(31, 20));                                 // filled inside the speed bump
    ex.advance(300);
    for (auto& e : ex.drain()) {
        if (e.type == test::FakeEvent::Fill) ot.on_fill(e.trade_id, e.client_index, e.qty, ex.now());
        if (e.type == test::FakeEvent::Cancelled) ot.on_cancelled(e.client_index, e.status, ex.now());
    }
    ASSERT(ot.get(31).state == OrderState::Cancelled);
    ASSERT_EQ(ot.position(0), 20);
    ASSERT_EQ(ex.open_orders(), 0);
}

TEST(requote_prefers_modify_when_only_the_price_moves) {
    ensure_client();
    OrderTracker ot;
    ot.add_pending(41, 0, false, 48, 232000, 0);
    ot.on_ack(41, (int64_t{1} << 48) + 9, 1);
    ASSERT(ot.plan_requote(41, 232001, 48, 1) == RequoteAction::Keep);
    ASSERT(ot.plan_requote(41, 232100, 48, 1) == RequoteAction::Modify);
    ASSERT(ot.plan_requote(41, 232100, 60, 1) == RequoteAction::CancelThenCreate);
    ot.request_modify(41, 232100, 48);
    ASSERT(ot.plan_requote(41, 232200, 48, 1) == RequoteAction::Blocked);
    ASSERT_EQ(ot.fillable_notional_units(0), __int128(48) * 232100);   // worst of old/new price
    // the modify signs and lands on the exchange after 300 ms
    NonceManager nm(kKey);
    nm.resync(0);
    test::FakeExchange ex(kAcct, wall_ms());
    ex.set_book(0, 232500, 232600);
    auto s = sign_batch(signer(), nm, {create_i(0, false, 41, 232000)}, {kKey, kAcct});
    ex.send_tx(s[0].tx_type, s[0].tx_info);
    Intent m; m.kind = IntentKind::Modify; m.market = 0; m.order_ref = 41; m.base = 48; m.price = 232100;
    auto ms = sign_batch(signer(), nm, {m}, {kKey, kAcct});
    ASSERT_EQ(ex.send_tx(ms[0].tx_type, ms[0].tx_info).code, 200);
    ASSERT_EQ(ex.order_price(41), 232000);
    ex.advance(300);
    ASSERT_EQ(ex.order_price(41), 232100);
    ot.on_modified(41, 2);
    ASSERT_EQ(ot.get(41).price, 232100);
}

TEST(client_index_allocator_never_repeats_across_runs) {
    ClientIndexAllocator r1(1), r2(2);
    int64_t a = r1.next(), b = r1.next(), c = r2.next();
    ASSERT(a != b && b != c && a != c);
    ASSERT(r1.owns(a) && !r1.owns(c) && r2.owns(c));
    ASSERT(c >= 1 && c <= kMaxClientOrderIndex);
    ASSERT_THROWS(ClientIndexAllocator(int64_t{1} << 28), std::invalid_argument);
}

// ───────────────────────────── risk ─────────────────────────────

TEST(loss_stops_persist_across_a_simulated_restart) {
    fs::path d = fresh_dir("risk_restart");
    RiskConfig cfg;   // daily −1.50, total −2.50
    int64_t day1 = 20'000 * int64_t{86400} + 3600;
    ASSERT_THROWS(RiskGovernor(cfg, d.string()), RiskError);                   // no state yet and no init flag
    {
        RiskGovernor g(cfg, d.string(), RiskGovernor::Open::Init);
        ASSERT(g.on_equity(5'000'000, day1) == RiskAction::None);             // first start seeds $5.00
        ASSERT(g.on_equity(3'600'000, day1 + 60) == RiskAction::None);        // −1.40
        ASSERT(g.on_equity(3'500'000, day1 + 120) == RiskAction::CancelAllAndFlatten);   // −1.50: daily stop
        ASSERT(g.halted(day1 + 120));
    }
    {   // restart, same day: still halted, and the baselines were NOT re-seeded from the current $3.50
        RiskGovernor g(cfg, d.string());
        ASSERT(g.halted(day1 + 200));
        ASSERT_EQ(g.state().start_equity_micro, 5'000'000);
        ASSERT_EQ(g.state().sod_equity_micro, 5'000'000);
        ASSERT(g.on_equity(3'500'000, day1 + 300) == RiskAction::None);   // already halted today, no re-fire
    }
    {   // next UTC day: SOD rolls to current equity, the daily halt clears, the total baseline does not move
        RiskGovernor g(cfg, d.string());
        int64_t day2 = day1 + 86400;
        ASSERT(g.on_equity(3'400'000, day2) == RiskAction::None);
        ASSERT(!g.halted(day2));
        ASSERT_EQ(g.state().sod_equity_micro, 3'400'000);
        ASSERT_EQ(g.state().start_equity_micro, 5'000'000);
        ASSERT(g.on_equity(2'500'000, day2 + 10) == RiskAction::CancelAllAndFlatten);   // −2.50 total
    }
    {
        RiskGovernor g(cfg, d.string());
        ASSERT(g.state().halt_total);
        ASSERT(g.halted(day1 + 10 * 86400));                              // total stop never expires
    }
}

TEST(kill_file_halts_and_never_auto_resumes) {
    fs::path d = fresh_dir("risk_kill");
    RiskConfig cfg;
    int64_t now = 20'000 * int64_t{86400};
    {
        RiskGovernor g(cfg, d.string(), RiskGovernor::Open::Init);
        g.on_equity(5'000'000, now);
        ASSERT(g.check_kill() == RiskAction::None);
        write_file(g.kill_path(), "");
        ASSERT(g.check_kill() == RiskAction::CancelAllAndFlatten);
        ASSERT(g.halted(now));
        ASSERT_THROWS(g.human_clear_kill(), RiskError);      // file still there
        fs::remove(g.kill_path());
        ASSERT(g.check_kill() == RiskAction::CancelAllAndFlatten);   // removing the file does not resume
    }
    {
        RiskGovernor g(cfg, d.string());                     // restart: still halted
        ASSERT(g.halted(now + 5));
        g.human_clear_kill();                                // explicit operator action only
        ASSERT(!g.halted(now + 6));
    }
}

TEST(pre_trade_limits_use_worst_case_exposure) {
    fs::path d = fresh_dir("risk_limits");
    RiskConfig cfg;
    RiskGovernor g(cfg, d.string(), RiskGovernor::Open::Init);
    int64_t now = 20'000 * int64_t{86400};
    g.on_equity(5'000'000, now);
    g.register_market(0);
    ASSERT_THROWS(g.register_market(1), RiskError);          // 1 market
    RiskGovernor::OrderCheck c;
    c.market = 0; c.is_ask = false; c.qty = 48; c.notional_micro = 11'163'552; c.max_position_base = 48;
    c.equity_micro = 5'000'000; c.now_unix_s = now; c.gross_micro = 0;
    ASSERT_EQ(g.check_order(c), std::string{});
    RiskGovernor::OrderCheck p = c; p.exposure.bid_fillable = 48;   // a pending bid (even CancelPending/Unknown)
    ASSERT(!g.check_order(p).empty());
    RiskGovernor::OrderCheck q = c; q.gross_micro = 1'000'000;      // $1 + $11.16 > $12 gross
    ASSERT(!g.check_order(q).empty());
    RiskGovernor::OrderCheck l = c; l.equity_micro = 2'000'000;     // 5 × $2 = $10 < $11.16
    ASSERT(!g.check_order(l).empty());
    RiskGovernor::OrderCheck big = c; big.notional_micro = 12'500'000;
    ASSERT(!g.check_order(big).empty());
    // halted: new quotes refused, a reduce-only flatten still allowed
    write_file(g.kill_path(), "");
    g.check_kill();
    ASSERT(!g.check_order(c).empty());
    RiskGovernor::OrderCheck flat = c; flat.is_ask = true; flat.reduce_only = true; flat.exposure.position = 48;
    ASSERT_EQ(g.check_order(flat), std::string{});
    flat.qty = 49;
    ASSERT(!g.check_order(flat).empty());
}

// ───────────────────────────── probe ─────────────────────────────

TEST(probe_plan_is_far_from_market_and_valid) {
    SizingRules r;
    int64_t bid = eth().price_int("2325.74"), ask = eth().price_int("2325.75");
    auto plan = build_probe_plan(eth(), r, bid, ask, 777, 1'000'000);
    ASSERT_EQ(plan.size(), size_t{5});
    ASSERT(plan[0].kind == ProbeStepKind::SignCreate);
    ASSERT(plan[1].kind == ProbeStepKind::AwaitAck);
    ASSERT(plan[2].kind == ProbeStepKind::SignCancel);
    ASSERT(plan[3].kind == ProbeStepKind::AwaitCancelled);
    ASSERT(plan[4].kind == ProbeStepKind::VerifyNoOpenOrders);
    const auto& c = plan[0].create;
    ASSERT(!c.is_ask);
    ASSERT(c.tif == TimeInForce::PostOnly);
    ASSERT(c.price * 10 <= bid * 8);                       // ≥ 20 % below the best bid
    ASSERT_EQ(c.price, 186059);                            // floor(232574 × 0.8)
    ASSERT(validate_order(eth(), r, c.base_amount, c.price).empty());
    ASSERT_EQ(c.order_expiry_ms, 1'000'000 + 600'000);
    ASSERT_EQ(plan[2].order_ref, 777);
    ASSERT_THROWS(build_probe_plan(eth(), r, ask, bid, 777, 0), SpecError);         // crossed book
    ProbeOptions near; near.min_distance_bp = 1'000;
    ASSERT_THROWS(build_probe_plan(eth(), r, bid, ask, 777, 0, near), SpecError);   // closer than 20 %
    ASSERT_THROWS(build_probe_plan(bnb(), r, bnb().price_int("737.0000"), bnb().price_int("737.1000"), 777, 0), SpecError);
}

TEST(probe_plan_runs_against_the_fake_exchange) {
    ensure_client();
    SizingRules r;
    int64_t bid = eth().price_int("2325.74"), ask = eth().price_int("2325.75");
    test::FakeExchange ex(kAcct, wall_ms());
    ex.set_book(0, bid, ask);
    ex.set_next_nonce(kKey, 1000);
    NonceManager nm(kKey);
    nm.resync(1000);
    OrderTracker ot;
    auto plan = build_probe_plan(eth(), r, bid, ask, 778, wall_ms());
    for (const auto& st : plan) {
        switch (st.kind) {
            case ProbeStepKind::SignCreate: {
                CreateOrderParams p = st.create; p.nonce = nm.reserve();
                SignedTx t = signer().sign_create_order(p, kKey, kAcct);
                ot.add_pending(p.client_order_index, p.market_index, p.is_ask, p.base_amount, p.price, ex.now());
                ASSERT_EQ(ex.send_tx(t.tx_type, t.tx_info).code, 200);
                nm.on_outcome(p.nonce, TxOutcome::Executed);
                break;
            }
            case ProbeStepKind::AwaitAck:
                for (auto& e : ex.drain()) if (e.type == test::FakeEvent::Ack) ot.on_ack(e.client_index, e.order_index, ex.now());
                ASSERT(ot.get(st.order_ref).state == OrderState::Open);
                ASSERT(ot.get(st.order_ref).order_index >= kMinExchangeOrderIndex);
                break;
            case ProbeStepKind::SignCancel: {
                int64_t n = nm.reserve();
                SignedTx t = signer().sign_cancel_order(0, st.order_ref, n, kKey, kAcct);
                ot.request_cancel(st.order_ref, ex.now());
                ASSERT_EQ(ex.send_tx(t.tx_type, t.tx_info).code, 200);
                nm.on_outcome(n, TxOutcome::Executed);
                break;
            }
            case ProbeStepKind::AwaitCancelled:
                ex.advance(300);
                for (auto& e : ex.drain()) if (e.type == test::FakeEvent::Cancelled) ot.on_cancelled(e.client_index, e.status, ex.now());
                ASSERT(ot.get(st.order_ref).state == OrderState::Cancelled);
                break;
            case ProbeStepKind::VerifyNoOpenOrders:
                ASSERT_EQ(ex.open_orders(), 0);
                break;
        }
    }
    ASSERT_EQ(ot.position(0), 0);
    ASSERT_EQ(ex.next_nonce(kKey), 1002);
}

// ───────────────────────────── FakeExchange semantics ─────────────────────────────

TEST(post_only_cross_is_cancelled_and_consumes_the_nonce) {
    ensure_client();
    test::FakeExchange ex(kAcct, wall_ms());
    ex.set_book(0, 232500, 232600);
    NonceManager nm(kKey);
    nm.resync(0);
    OrderTracker ot;
    ot.add_pending(51, 0, false, 48, 232600, ex.now());   // bid AT the best ask: would cross
    auto s = sign_batch(signer(), nm, {create_i(0, false, 51, 232600)}, {kKey, kAcct});
    ASSERT_EQ(ex.send_tx(s[0].tx_type, s[0].tx_info).code, 200);   // HTTP 200: accepted, cancelled in the sequencer
    for (auto& e : ex.drain()) {
        ASSERT(e.type == test::FakeEvent::Cancelled && e.status == kStatusCanceledPostOnly);
        ot.on_cancelled(e.client_index, e.status, ex.now());
        nm.on_outcome(e.nonce, classify_sequencer(e.status, 0));
    }
    ASSERT(ot.get(51).state == OrderState::Rejected);
    ASSERT(ot.get(51).reject == RejectReason::PostOnlyCross);
    ASSERT_EQ(ot.post_only_rejects(), uint64_t{1});
    ASSERT_EQ(ex.next_nonce(kKey), 1);
    ASSERT_EQ(nm.peek_next(), 1);
    ASSERT_EQ(ot.exposure(0).bid_fillable, 0);
}

TEST(api_rejects_do_not_consume_the_nonce) {
    ensure_client();
    test::FakeExchange ex(kAcct, wall_ms());
    ex.set_book(0, 232500, 232600);
    NonceManager nm(kKey);
    ex.set_next_nonce(kKey, 5);                            // another writer moved the key: the exchange is at 5
    nm.resync(0);                                          // our stale view
    auto s = sign_batch(signer(), nm, {create_i(0, false, 61, 232000)}, {kKey, kAcct});
    auto r = ex.send_tx(s[0].tx_type, s[0].tx_info);
    ASSERT_EQ(r.code, 21104);
    ASSERT_EQ(ex.next_nonce(kKey), 5);                     // not consumed
    auto resign = nm.on_outcome(0, classify_send(r.http, r.code));
    ASSERT_EQ(resign, (std::vector<int64_t>{0}));          // known not applied: re-sign after the resync
    ASSERT(nm.frozen());
    ASSERT_THROWS(nm.reserve(), NonceError);
    auto rs = nm.resync(ex.next_nonce(kKey));              // refetch nextNonce
    ASSERT(rs.applied.empty() && rs.not_applied.empty());
    auto s2 = sign_batch(signer(), nm, {create_i(0, false, 61, 232000)}, {kKey, kAcct});
    ASSERT_EQ(s2[0].nonce, 5);
    ASSERT_EQ(ex.send_tx(s2[0].tx_type, s2[0].tx_info).code, 200);
    // a tx that would expire within 5 s is rejected at the API and consumes nothing
    test::FakeExchange late(kAcct, wall_ms() + 10 * 60'000);
    late.set_next_nonce(kKey, 1);
    auto s3 = sign_batch(signer(), nm, {create_i(0, false, 62, 232000)}, {kKey, kAcct});
    ASSERT_EQ(late.send_tx(s3[0].tx_type, s3[0].tx_info).code, 21116);
    ASSERT_EQ(late.next_nonce(kKey), 1);
}

TEST(duplicate_client_index_is_a_consumed_no_op) {
    ensure_client();
    test::FakeExchange ex(kAcct, wall_ms());
    ex.set_book(0, 232500, 232600);
    NonceManager nm(kKey);
    nm.resync(0);
    auto a = sign_batch(signer(), nm, {create_i(0, false, 71, 232000)}, {kKey, kAcct});
    auto b = sign_batch(signer(), nm, {create_i(0, false, 71, 231000)}, {kKey, kAcct});
    ASSERT_EQ(ex.send_tx(a[0].tx_type, a[0].tx_info).code, 200);
    ASSERT_EQ(ex.send_tx(b[0].tx_type, b[0].tx_info).code, 200);
    auto ev = ex.drain();
    ASSERT_EQ(ev.size(), size_t{2});
    ASSERT(ev[1].type == test::FakeEvent::TxFailed && ev[1].app_error == 21728);
    ASSERT(classify_sequencer(-1, ev[1].app_error) == TxOutcome::SequencerFailedApplying);
    ASSERT_EQ(ex.next_nonce(kKey), 2);
    ASSERT_EQ(ex.open_orders(), 1);
}

TEST(exchange_429_past_budget_and_the_limiter_backs_off) {
    ensure_client();
    test::FakeExchange ex(kAcct, wall_ms());
    ex.set_book(0, 232500, 232600);
    NonceManager nm(kKey);
    nm.resync(0);
    RateLimiter rl(60, 0);          // deliberately at the exchange's own limit to provoke a 429
    int sent = 0;
    test::SendResult last{};
    for (int k = 0; k < 61; ++k) {
        Intent ca; ca.kind = IntentKind::CancelAll; ca.market = 255;
        auto s = sign_batch(signer(), nm, {ca}, {kKey, kAcct});
        last = ex.send_tx(s[0].tx_type, s[0].tx_info);
        auto out = classify_send(last.http, last.code);
        nm.on_outcome(s[0].nonce, out);
        if (last.http == 429) { rl.on_rate_limited(ex.now()); break; }
        rl.record(ex.now());
        ++sent;
    }
    ASSERT_EQ(sent, 60);
    ASSERT_EQ(last.http, 429);
    ASSERT_EQ(ex.next_nonce(kKey), 60);                    // the 61st was not consumed
    ASSERT_EQ(nm.peek_next(), 60);                         // and our manager reuses it
    ASSERT(!rl.can_send(ex.now() + 59'999, true));
    ASSERT(rl.can_send(ex.now() + 60'000, true));
    // with the default 54 budget we never reach the exchange's 60
    RateLimiter def;
    int ok = 0;
    for (int k = 0; k < 100; ++k) if (def.can_send(0, true)) { def.record(0); ++ok; }
    ASSERT_EQ(ok, 54);
}

TEST(testnet_appears_only_as_a_refused_value) {
    ASSERT_THROWS(network_guard(kTestnetHost, 300), ConfigError);
    ASSERT_THROWS(network_guard(kTestnetHost, 304), ConfigError);
    ASSERT_THROWS(network_guard(kMainnetHost, 300), ConfigError);
    network_guard(kMainnetHost, 304);
    ASSERT(std::string(kSignerPath).find("testnet") == std::string::npos);
}

// ───────────────────────────── review regressions (ported from /tmp/claude-1000/gwreview/adv.cpp) ─────────────

TEST(adv1_fill_after_cancel_confirm_is_applied) {
    OrderTracker t;
    t.add_pending(1, 0, false, 10, 100, 0);
    t.on_ack(1, int64_t{1} << 48, 1);
    t.request_cancel(1, 2);
    t.on_cancelled(1, kStatusCanceled, 3);
    ASSERT(t.on_fill(77, 1, 4, 4));                 // a late fill on a Cancelled order is applied, not thrown away
    ASSERT_EQ(t.position(0), 4);
    ASSERT_EQ(t.get(1).filled, 4);
    ASSERT(t.get(1).state == OrderState::Cancelled);
    ASSERT_EQ(t.late_fills(), uint64_t{1});
    ASSERT(!t.on_fill(77, 1, 4, 5));                // redelivery: a duplicate, position unchanged
    ASSERT_EQ(t.position(0), 4);
}

TEST(adv2_fill_for_an_unknown_order_is_held_not_poisoned) {
    OrderTracker t;
    ASSERT(!t.on_fill(88, 999, 4, 1));               // order not known yet: held, trade id NOT marked seen
    ASSERT_EQ(t.held_fills(), size_t{1});
    ASSERT(t.reconcile_needed());
    t.add_pending(999, 0, false, 10, 100, 0);
    ASSERT(t.on_fill(88, 999, 4, 2));                // the redelivery applies it
    ASSERT_EQ(t.position(0), 4);
    ASSERT_EQ(t.held_fills(), size_t{0});
    // held fill with no redelivery: apply_held after the order becomes known
    OrderTracker u;
    u.on_fill(5, 50, 3, 1);
    u.add_pending(50, 0, true, 10, 100, 2);
    ASSERT_EQ(u.apply_held(50, 3), 1);
    ASSERT_EQ(u.position(0), -3);
    ASSERT(!u.on_fill(5, 50, 3, 4));                 // later redelivery is a duplicate
    // with market/side known, the position moves at once and the qty is attributed when the order appears
    OrderTracker w;
    ASSERT(w.on_fill(6, 60, 2, 1, /*market*/ 0, /*is_ask*/ false));
    ASSERT_EQ(w.position(0), 2);
    w.add_pending(60, 0, false, 10, 100, 2);
    ASSERT_EQ(w.get(60).filled, 2);
    ASSERT(w.get(60).state == OrderState::PartiallyFilled);
    ASSERT_EQ(w.position(0), 2);                     // not double-counted
}

TEST(adv3_reconcile_filled_without_amount_moves_the_position) {
    OrderTracker t;
    t.add_pending(2, 0, true, 10, 100, 0);
    t.on_timeout(5000, 3000);
    t.reconcile(2, kStatusFilled, 0, 0, 6000);
    ASSERT_EQ(t.position(0), -10);
    ASSERT(t.get(2).state == OrderState::Filled);
    OrderTracker p;                                  // partially seen already: only the rest is added
    p.add_pending(3, 0, false, 10, 100, 0);
    p.on_ack(3, (int64_t{1} << 48) + 1, 1);
    p.on_fill(1, 3, 4, 2);
    p.reconcile(3, kStatusFilled, 0, 0, 3);
    ASSERT_EQ(p.position(0), 10);
}

TEST(adv4_second_reduce_only_for_the_full_position_is_refused) {
    fs::path d = fresh_dir("adv4");
    RiskGovernor g(RiskConfig{}, d.string(), RiskGovernor::Open::Init);
    g.register_market(0);
    OrderTracker t;
    t.set_position(0, 10);
    t.add_pending(5, 0, true, 10, 100, 0);           // first flatten already in flight
    RiskGovernor::OrderCheck c;
    c.market = 0; c.is_ask = true; c.qty = 10; c.reduce_only = true; c.exposure = t.exposure(0);
    ASSERT(!g.check_order(c).empty());
    c.exposure = OrderTracker{}.exposure(0); c.exposure.position = 10;
    ASSERT(g.check_order(c).empty());                // alone it is fine
    c.is_ask = false;
    ASSERT(!g.check_order(c).empty());               // a reduce-only BUY on a long never reduces
}

TEST(adv5_total_stop_is_measured_from_the_persisted_high_water_mark) {
    fs::path d = fresh_dir("adv5");
    RiskConfig rc;
    {
        RiskGovernor g(rc, d.string(), RiskGovernor::Open::Init);
        ASSERT(g.on_equity(100'000'000, 0) == RiskAction::None);
        ASSERT(g.on_equity(110'000'000, 10) == RiskAction::None);
        ASSERT_EQ(g.state().hwm_equity_micro, 110'000'000);
    }
    {   // restart: the HWM survives; −3 from the peak trips the total stop although equity is +7 vs start
        RiskGovernor g(rc, d.string());
        ASSERT_EQ(g.state().hwm_equity_micro, 110'000'000);
        ASSERT(g.on_equity(107'000'000, 20) != RiskAction::None);
        ASSERT(g.state().halt_total);
    }
    {
        RiskGovernor g(rc, d.string());
        ASSERT(g.halted(1'000'000));
    }
}

TEST(adv6_api_reject_below_a_consumed_nonce_freezes) {
    NonceManager nm(4);
    nm.resync(10);
    auto a = nm.reserve(), b = nm.reserve();
    nm.on_outcome(b, TxOutcome::Executed);
    nm.on_outcome(a, TxOutcome::ApiRejected);        // inconsistent: a later nonce is already consumed
    ASSERT(nm.frozen());
    ASSERT_THROWS(nm.reserve(), NonceError);         // never hands out a consumed nonce
    nm.resync(12);
    int64_t c = nm.reserve(), d2 = nm.reserve();
    ASSERT(c != b && d2 != b);
    ASSERT(c >= 12);
}

TEST(adv7_unknown_tx_is_not_declared_not_applied_by_a_nonce_snapshot) {
    ensure_client();
    NonceManager nm(kKey);
    nm.resync(10);
    auto txs = sign_batch(signer(), nm, {create_i(0, false, 9001, 232000)}, {kKey, kAcct});
    int64_t a = txs[0].nonce;
    nm.mark_sent(a);
    nm.on_outcome(a, TxOutcome::Unknown);
    auto r = nm.resync(10);
    ASSERT(r.not_applied.empty());
    ASSERT_EQ(r.unconfirmed, (std::vector<int64_t>{a}));
    ASSERT(nm.frozen());
    ASSERT_THROWS(nm.reserve(), NonceError);
    // the only thing allowed on that nonce: the byte-identical bytes (same nonce, same client index)
    ASSERT(nm.resend_bytes(a).has_value());
    ASSERT_EQ(*nm.resend_bytes(a), txs[0].tx_info);
    ASSERT_THROWS(nm.attach(a, txs[0].tx_info + " "), NonceError);
    // "not applied" is final only after the tx's own ExpiredAt has passed
    ASSERT_THROWS(nm.confirm_not_applied(a, wall_ms()), NonceError);
    nm.confirm_not_applied(a, tx_expired_at_ms(txs[0].tx_info) + 1);
    ASSERT(nm.frozen());                             // one fresh resync decides the next nonce
    nm.resync(10);
    ASSERT(!nm.frozen());
    ASSERT_EQ(nm.reserve(), 10);
}

TEST(adv8_a_bad_create_never_sinks_a_cancel) {
    ensure_client();
    Batcher b;
    RateLimiter rl;
    Intent ca; ca.kind = IntentKind::Cancel; ca.market = 0; ca.slot = slot_key(0, true); ca.order_ref = 5;
    ASSERT(b.submit(ca).empty());
    Intent cr; cr.kind = IntentKind::Create; cr.market = 0; cr.slot = slot_key(0, false); cr.order_ref = 7;
    cr.base = 10; cr.price = 100; cr.expiry_ms = 0;   // post-only without expiry: invalid
    ASSERT(!b.submit(cr).empty());                    // refused at the door, never queued
    ASSERT_EQ(b.rejected(), uint64_t{1});
    auto batch = b.next_batch(wall_ms(), rl, [](uint64_t) { return true; });
    ASSERT_EQ(batch.size(), size_t{1});
    NonceManager nm(kKey);
    nm.resync(0);
    auto s = sign_batch(signer(), nm, batch, {kKey, kAcct});
    ASSERT_EQ(s.size(), size_t{1});
    ASSERT_EQ(int(s[0].tx_type), 15);
    // a failure INSIDE a batch (bypassing the batcher) keeps the signed cancel prefix and requeues the rest
    Intent cn2 = ca; cn2.order_ref = 6;
    Intent bad = create_i(0, false, 0, 232000);       // client index 0
    Intent good = create_i(0, true, 8, 233000);
    std::vector<std::string> errs;
    Batcher back;
    auto s2 = sign_batch(signer(), nm, {cn2, bad, good}, {kKey, kAcct}, SignOptions{&back, &errs, 0, {}});
    ASSERT_EQ(s2.size(), size_t{1});
    ASSERT_EQ(int(s2[0].tx_type), 15);
    ASSERT_EQ(errs.size(), size_t{1});
    ASSERT_EQ(back.queued(), size_t{1});              // the good create waits for the next batch
    ASSERT_EQ(nm.peek_next(), 2);                     // nonces after the cancel were released unsent
}

TEST(adv9_scheduled_cancel_all_fires_at_its_time_not_immediately) {
    ensure_client();
    int64_t t0 = wall_ms();
    test::FakeExchange ex(kAcct, t0);
    ex.set_book(0, 232500, 232600);
    ex.set_book(1, 1'099'000, 1'100'000);
    NonceManager nm(kKey);
    nm.resync(0);
    auto o = sign_batch(signer(), nm, {create_i(0, false, 81, 232000), create_i(1, false, 82, 1'000'000)}, {kKey, kAcct});
    std::vector<std::pair<uint8_t, std::string>> wire;
    for (auto& t : o) wire.push_back({t.tx_type, t.tx_info});
    ASSERT_EQ(ex.send_batch(wire).code, 200);
    Intent sc; sc.kind = IntentKind::CancelAll; sc.market = 255; sc.ca_tif = CancelAllTif::Scheduled; sc.ca_time_ms = t0 + 60'000;
    auto s = sign_batch(signer(), nm, {sc}, {kKey, kAcct});
    ASSERT_EQ(ex.send_tx(s[0].tx_type, s[0].tx_info).code, 200);
    ex.advance(1'000);
    ASSERT_EQ(ex.open_orders(), 2);                  // not immediate
    ASSERT_EQ(ex.scheduled_cancel_alls(), size_t{1});
    ex.set_now(t0 + 60'000);
    ASSERT_EQ(ex.open_orders(), 0);                  // the dead-man fired
    // abort removes a scheduled one; a per-market immediate cancel-all touches only its market
    auto o2 = sign_batch(signer(), nm, {create_i(0, false, 83, 232000), create_i(1, false, 84, 1'000'000)}, {kKey, kAcct});
    for (auto& t : o2) ASSERT_EQ(ex.send_tx(t.tx_type, t.tx_info).code, 200);
    Intent sc2 = sc; sc2.ca_time_ms = t0 + 120'000;
    Intent ab; ab.kind = IntentKind::CancelAll; ab.market = 255; ab.ca_tif = CancelAllTif::Abort;
    Intent pm; pm.kind = IntentKind::CancelAll; pm.market = 0; pm.ca_tif = CancelAllTif::Immediate;
    for (auto& t : sign_batch(signer(), nm, {sc2, ab, pm}, {kKey, kAcct})) ASSERT_EQ(ex.send_tx(t.tx_type, t.tx_info).code, 200);
    ASSERT_EQ(ex.scheduled_cancel_alls(), size_t{0});
    ex.advance(300);
    ASSERT_EQ(ex.order_status(83), kStatusCanceled);
    ASSERT_EQ(ex.order_status(84), kStatusOpen);
    ex.set_now(t0 + 130'000);
    ASSERT_EQ(ex.order_status(84), kStatusOpen);     // the aborted dead-man never fires
}

// ───────────────────────────── further review fixes ─────────────────────────────

TEST(cancel_requested_while_unknown_is_resent_on_ack) {
    OrderTracker t;
    t.add_pending(91, 0, false, 10, 100, 0);
    t.on_timeout(5'000, 3'000);
    t.request_cancel(91, 5'001);
    ASSERT(t.get(91).state == OrderState::Unknown);
    ASSERT(t.on_ack(91, (int64_t{1} << 48) + 7, 5'002));   // → re-send the cancel now
    ASSERT(t.get(91).state == OrderState::CancelPending);
    t.add_pending(92, 0, true, 10, 100, 0);
    t.on_timeout(5'000, 3'000);
    ASSERT(!t.on_ack(92, (int64_t{1} << 48) + 8, 5'002));  // no cancel was asked: plain Open
    ASSERT(t.get(92).state == OrderState::Open);
}

TEST(order_expiry_must_be_within_the_spec_window) {
    ensure_client();
    uint64_t before = Signer::library_calls();
    CreateOrderParams p;
    p.market_index = 0; p.client_order_index = 1; p.base_amount = 48; p.price = 232000; p.nonce = 0;
    p.now_ms = 1'800'000'000'000;
    p.order_expiry_ms = p.now_ms + 4 * 60'000;                       // < 5 min
    ASSERT_THROWS(signer().sign_create_order(p, kKey, kAcct), RangeError);
    p.order_expiry_ms = p.now_ms + 31LL * 86'400'000;               // > 30 days
    ASSERT_THROWS(signer().sign_create_order(p, kKey, kAcct), RangeError);
    ASSERT_EQ(Signer::library_calls(), before);
    Intent i = create_i(0, false, 5, 232000);
    i.expiry_ms = wall_ms() + 60'000;
    Batcher b;
    ASSERT(!b.submit(i).empty());
    i.expiry_ms = wall_ms() + 10 * 60'000;
    ASSERT(b.submit(i).empty());
}

TEST(risk_state_missing_is_refused_and_the_rename_is_durable) {
    fs::path d = fresh_dir("risk_missing");
    {
        RiskGovernor g(RiskConfig{}, d.string(), RiskGovernor::Open::Init);
        g.on_equity(5'000'000, 0);
    }
    ASSERT(fs::exists(d / "risk_state.json"));
    ASSERT(!fs::exists(d / "risk_state.json.tmp"));
    RiskGovernor again(RiskConfig{}, d.string());                     // normal start loads it
    ASSERT_EQ(again.state().start_equity_micro, 5'000'000);
    fs::remove(d / "risk_state.json");                               // deleting the file cannot reset the stops
    ASSERT_THROWS(RiskGovernor(RiskConfig{}, d.string()), RiskError);
    write_file(d / "risk_state.json", "{\"start_equity_micro\":1}");  // a truncated file is refused too
    ASSERT_THROWS(RiskGovernor(RiskConfig{}, d.string()), RiskError);
    // Init never overwrites an existing state
    write_file(d / "risk_state.json", json{{"start_equity_micro", 9}, {"hwm_equity_micro", 9}, {"day", 0},
        {"sod_equity_micro", 9}, {"halt_day", -1}, {"halt_total", true}, {"halt_kill", false}}.dump());
    RiskGovernor init(RiskConfig{}, d.string(), RiskGovernor::Open::Init);
    ASSERT(init.state().halt_total);
}

TEST(key_file_symlinked_parent_directory_is_refused) {
    fs::path d = fresh_dir("keys_parent");
    fs::create_directories(d / "real");
    write_file(d / "real" / "mainnet.key", key_json(), 0600);
    fs::create_directory_symlink(d / "real", d / "linkdir");
    GatewayConfig g = parse_config(base_cfg("offline", d), Mode::Offline, wall_s());
    fs::path repo = fs::path(__FILE__).parent_path().parent_path().parent_path();
    g.key_file = (d / "real" / "mainnet.key").string();
    ASSERT_EQ(load_key_file(g, repo.string()).account_index, kAcct);
    g.key_file = (d / "linkdir" / "mainnet.key").string();
    ASSERT_THROWS(load_key_file(g, repo.string()), ConfigError);
    g.key_file = "relative/mainnet.key";
    ASSERT_THROWS(load_key_file(g, repo.string()), ConfigError);
}

TEST(arming_is_rechecked_before_every_batch) {
    ensure_client();
    fs::path d = fresh_dir("arm_recheck");
    write_file(d / "ARM_MAINNET", std::to_string(kAcct) + " " + today_str() + "\n");
    GatewayConfig cfg = parse_config(base_cfg("live", d), Mode::Live, wall_s());
    ArmingGate gate(cfg);
    ASSERT(gate.armed(wall_s()));
    NonceManager nm(kKey);
    nm.resync(0);
    Intent cn; cn.kind = IntentKind::Cancel; cn.market = 0; cn.order_ref = 5;
    SignOptions so;
    std::vector<std::string> errs;
    so.errors = &errs;
    so.armed = [&] { return gate.armed(wall_s()); };
    ASSERT_EQ(sign_batch(signer(), nm, {cn, create_i(0, false, 6, 232000)}, {kKey, kAcct}, so).size(), size_t{2});
    fs::remove(d / "ARM_MAINNET");                       // disarmed mid-run
    auto s = sign_batch(signer(), nm, {cn, create_i(0, false, 7, 232000)}, {kKey, kAcct}, so);
    ASSERT_EQ(s.size(), size_t{1});                       // the cancel still goes; the create does not
    ASSERT_EQ(int(s[0].tx_type), 15);
    ASSERT_EQ(errs.size(), size_t{1});
    ASSERT(!gate.last_reason().empty());
    write_file(d / "ARM_MAINNET", std::to_string(kAcct) + " " + today_str() + "\n");
    write_file(d / "NO_DEPLOY_MM", "");
    ASSERT(!gate.armed(wall_s()));
}

TEST(fake_late_fill_after_cancel_confirm_keeps_the_tracker_right) {
    ensure_client();
    int late_seen = 0;
    for (uint32_t seed = 1; seed <= 8; ++seed) {
        test::FakeExchange ex(kAcct, wall_ms(), seed);
        ex.set_book(0, 232500, 232600);
        ex.set_late_fill_prob(0.5);
        NonceManager nm(kKey);
        nm.resync(0);
        OrderTracker ot;
        ot.add_pending(101, 0, false, 48, 232000, ex.now());
        auto c = sign_batch(signer(), nm, {create_i(0, false, 101, 232000)}, {kKey, kAcct});
        ex.send_tx(c[0].tx_type, c[0].tx_info);
        for (auto& e : ex.drain()) ot.on_ack(e.client_index, e.order_index, ex.now());
        Intent cn; cn.kind = IntentKind::Cancel; cn.market = 0; cn.order_ref = 101;
        auto x = sign_batch(signer(), nm, {cn}, {kKey, kAcct});
        ot.request_cancel(101, ex.now());
        ex.send_tx(x[0].tx_type, x[0].tx_info);
        ex.hit(101, 20);
        ex.advance(300);
        bool cancel_first = false, seen_cancel = false;
        for (auto& e : ex.drain()) {
            if (e.type == test::FakeEvent::Cancelled) { seen_cancel = true; ot.on_cancelled(e.client_index, e.status, ex.now()); }
            if (e.type == test::FakeEvent::Fill) { cancel_first = seen_cancel; ot.on_fill(e.trade_id, e.client_index, e.qty, ex.now()); }
        }
        late_seen += cancel_first;
        ASSERT_EQ(ot.position(0), 20);                    // same answer in either delivery order
        ASSERT_EQ(ex.position(0), 20);
        ASSERT(ot.get(101).state == OrderState::Cancelled);
    }
    ASSERT(late_seen > 0 && late_seen < 8);              // both orders actually happened (seeded)
}

TEST(fake_enforces_min_size_reduce_only_and_fills_ioc) {
    ensure_client();
    test::FakeExchange ex(kAcct, wall_ms());
    ex.set_book(0, 232500, 232600);
    ex.set_market(0, /*min_base 0.0020*/ 20, /*$10.50 at sd4+pd2*/ 10'500'000);
    NonceManager nm(kKey);
    nm.resync(0);
    auto small = sign_batch(signer(), nm, {[] { Intent i = create_i(0, false, 111, 232000); i.base = 44; return i; }()}, {kKey, kAcct});
    ASSERT_EQ(ex.send_tx(small[0].tx_type, small[0].tx_info).code, 200);
    auto ev = ex.drain();
    ASSERT(ev.size() == 1 && ev[0].type == test::FakeEvent::TxFailed && ev[0].app_error == 21706);
    ASSERT_EQ(ex.next_nonce(kKey), 1);                    // consumed
    // reduce-only sell with no position → canceled-reduce-only (6)
    Intent ro = create_i(0, true, 112, 232700); ro.reduce_only = true;
    auto r = sign_batch(signer(), nm, {ro}, {kKey, kAcct});
    ex.send_tx(r[0].tx_type, r[0].tx_info);
    ASSERT_EQ(ex.order_status(112), 6);
    // IOC buy at the ask fills as taker; IOC below the ask cancels
    Intent ioc = create_i(0, false, 113, 232600); ioc.tif = TimeInForce::IOC; ioc.expiry_ms = 0;
    Intent ioc2 = create_i(0, false, 114, 232000); ioc2.tif = TimeInForce::IOC; ioc2.expiry_ms = 0;
    for (auto& t : sign_batch(signer(), nm, {ioc, ioc2}, {kKey, kAcct})) ex.send_tx(t.tx_type, t.tx_info);
    ASSERT_EQ(ex.order_status(113), kStatusFilled);
    ASSERT_EQ(ex.position(0), 48);
    ASSERT_EQ(ex.order_status(114), kStatusCanceled);
    // now a reduce-only sell for the position is accepted and fills as IOC
    Intent flat = create_i(0, true, 115, 232500); flat.tif = TimeInForce::IOC; flat.expiry_ms = 0; flat.reduce_only = true;
    auto f = sign_batch(signer(), nm, {flat}, {kKey, kAcct});
    ex.send_tx(f[0].tx_type, f[0].tx_info);
    ASSERT_EQ(ex.position(0), 0);
}

TEST(nonce_manager_under_both_batch_reject_modes) {
    ensure_client();
    for (auto mode : {test::BatchMode::WholeBatch, test::BatchMode::PrefixAccepted}) {
        test::FakeExchange ex(kAcct, wall_ms());
        ex.set_book(0, 232500, 232600);
        ex.set_batch_mode(mode);
        NonceManager nm(kKey);
        nm.resync(0);
        auto txs = sign_batch(signer(), nm, {create_i(0, false, 121, 232000), create_i(0, true, 122, 233000),
                                             create_i(1, false, 123, 1'000'000)}, {kKey, kAcct});
        std::vector<std::pair<uint8_t, std::string>> wire;
        for (auto& t : txs) { nm.mark_sent(t.nonce); wire.push_back({t.tx_type, t.tx_info}); }
        // corrupt the 2nd tx's signature: a per-tx API error inside the batch
        json bad = json::parse(wire[1].second); bad["Sig"] = ""; wire[1].second = bad.dump();
        auto res = ex.send_batch(wire);
        ASSERT_EQ(res.http, 400);
        // the gateway reports exactly what the exchange says was accepted, then the API reject for the rest
        for (size_t k = 0; k < res.accepted; ++k) nm.on_outcome(txs[k].nonce, TxOutcome::Executed);
        auto resign = nm.on_outcome(txs[res.accepted].nonce, classify_send(res.http, res.code));
        ASSERT_EQ(resign.size(), size_t{2} - res.accepted);   // everything after the rejected one is re-signed
        ASSERT_EQ(nm.peek_next(), ex.next_nonce(kKey));        // the manager and the exchange agree
        ASSERT(!nm.frozen());
        if (mode == test::BatchMode::WholeBatch) ASSERT_EQ(res.accepted, size_t{0});
        else ASSERT_EQ(res.accepted, size_t{1});
        // next tx goes through on the nonce the manager hands out
        auto again = sign_batch(signer(), nm, {create_i(0, true, 124, 233000)}, {kKey, kAcct});
        ASSERT_EQ(ex.send_tx(again[0].tx_type, again[0].tx_info).code, 200);
    }
}

int main(int, char** argv) {
    reexec_with_godebug_if_needed(argv);   // Go reads the exec-time environment, not setenv()
    std::cout << "=== Lighter live gateway (offline core) ===\n";
    RUN(signer_wrong_hash_is_refused_before_load);
    RUN(signer_resolves_only_the_allowlist);
    RUN(signer_signs_a_post_only_create_offline);
    RUN(signer_price_above_int31_is_bit_cast_not_truncated);
    RUN(signer_truncation_guards_fire_before_the_library_is_called);
    RUN(signer_modify_and_cancel_take_client_or_exchange_index);
    RUN(signer_cancel_all_rules);
    RUN(signer_client_is_mainnet_only_and_offline);
    RUN(signer_auth_token_deadline_rules);
    RUN(config_testnet_host_and_wrong_chain_are_refused);
    RUN(config_unknown_keys_and_mode_mismatch_are_refused);
    RUN(config_missing_arming_file_refuses_probe_and_live);
    RUN(key_file_permissions_and_location);
    RUN(decimal_parsing_is_exact);
    RUN(eleven_dollar_quotes_round_up_on_btc_eth_sol_and_the_coarse_alt_is_refused);
    RUN(order_validation_rejects_min_and_cap);
    RUN(nonce_requires_sync_and_is_monotonic);
    RUN(nonce_after_reject_follows_the_consumption_rules);
    RUN(nonce_unknown_outcome_freezes_until_resync);
    RUN(rate_limiter_rolling_window_with_safety_reserve);
    RUN(rate_limit_429_backs_off_sixty_seconds);
    RUN(batch_orders_cancels_then_modifies_then_creates_and_caps_size);
    RUN(batch_holds_a_create_while_its_slot_is_busy);
    RUN(signed_batch_has_consecutive_nonces_and_the_exchange_accepts_it);
    RUN(order_lifecycle_with_idempotent_partial_fills);
    RUN(order_illegal_transitions_throw);
    RUN(unknown_state_counts_as_exposure_until_the_exchange_confirms);
    RUN(cancel_pending_order_can_still_fill_during_the_300ms_delay);
    RUN(requote_prefers_modify_when_only_the_price_moves);
    RUN(client_index_allocator_never_repeats_across_runs);
    RUN(loss_stops_persist_across_a_simulated_restart);
    RUN(kill_file_halts_and_never_auto_resumes);
    RUN(pre_trade_limits_use_worst_case_exposure);
    RUN(probe_plan_is_far_from_market_and_valid);
    RUN(probe_plan_runs_against_the_fake_exchange);
    RUN(post_only_cross_is_cancelled_and_consumes_the_nonce);
    RUN(api_rejects_do_not_consume_the_nonce);
    RUN(duplicate_client_index_is_a_consumed_no_op);
    RUN(exchange_429_past_budget_and_the_limiter_backs_off);
    RUN(testnet_appears_only_as_a_refused_value);
    RUN(adv1_fill_after_cancel_confirm_is_applied);
    RUN(adv2_fill_for_an_unknown_order_is_held_not_poisoned);
    RUN(adv3_reconcile_filled_without_amount_moves_the_position);
    RUN(adv4_second_reduce_only_for_the_full_position_is_refused);
    RUN(adv5_total_stop_is_measured_from_the_persisted_high_water_mark);
    RUN(adv6_api_reject_below_a_consumed_nonce_freezes);
    RUN(adv7_unknown_tx_is_not_declared_not_applied_by_a_nonce_snapshot);
    RUN(adv8_a_bad_create_never_sinks_a_cancel);
    RUN(adv9_scheduled_cancel_all_fires_at_its_time_not_immediately);
    RUN(cancel_requested_while_unknown_is_resent_on_ack);
    RUN(order_expiry_must_be_within_the_spec_window);
    RUN(risk_state_missing_is_refused_and_the_rename_is_durable);
    RUN(key_file_symlinked_parent_directory_is_refused);
    RUN(arming_is_rechecked_before_every_batch);
    RUN(fake_late_fill_after_cancel_confirm_keeps_the_tracker_right);
    RUN(fake_enforces_min_size_reduce_only_and_fills_ioc);
    RUN(nonce_manager_under_both_batch_reject_modes);
    fs::remove_all(scratch());
    std::cout << tests_run - tests_failed << "/" << tests_run << " passed\n";
    return tests_failed ? 1 : 0;
}
