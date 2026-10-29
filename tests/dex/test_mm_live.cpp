/*  test_mm_live.cpp — the live Lighter market maker (src/dex/live/engine.hpp + account_feed / sender / rest_api /
    journal / alerts / net / offline), entirely OFFLINE.

    NO NETWORK to any Lighter host: the engine runs against the in-process FakeVenue (fake_venue.hpp); the real
    TLS clients (net.hpp) are exercised only against a local HTTPS/WSS server on 127.0.0.1 with a CA generated in this
    process. Keys are throwaway (GenerateAPIKey, in memory). Alerts go to a capture vector, never to grid-notify.
    Run under: env GODEBUG=asyncpreemptoff=1 (main() re-execs itself with it if missing).                       */
#include "dex/live/account_feed.hpp"
#include "dex/live/alerts.hpp"
#include "dex/live/driver.hpp"
#include "dex/live/engine.hpp"
#include "dex/live/fake_venue.hpp"
#include "dex/live/instance_lock.hpp"
#include "dex/live/journal.hpp"
#include "dex/live/mm_live_config.hpp"
#include "dex/live/net.hpp"
#include "dex/live/offline.hpp"
#include "dex/live/rest_api.hpp"
#include "dex/live/sender.hpp"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>

#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <csignal>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

static int tests_run = 0, tests_failed = 0;
#define TEST(name) void test_##name()
#define RUN(name) do { if (std::getenv("ONLY") && std::string(#name).find(std::getenv("ONLY")) == std::string::npos) break; ++tests_run; auto t0_ = std::chrono::steady_clock::now(); \
    try { test_##name(); std::cout << "PASS " #name; } \
    catch (std::exception& e) { ++tests_failed; std::cout << "FAIL " #name ": " << e.what(); } \
    std::cout << "  (" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0_).count() << " ms)\n"; \
} while (0)
#define ASSERT(c) do { if (!(c)) throw std::runtime_error("Assert failed: " #c " (line " + std::to_string(__LINE__) + ")"); } while (0)
template <class T> static std::string tstr(const T& v) {
    if constexpr (std::is_enum_v<T>) return std::to_string(int(v));
    else if constexpr (std::is_arithmetic_v<T>) return std::to_string(v);
    else if constexpr (std::is_convertible_v<T, std::string>) return std::string(v);
    else return "?";
}
#define ASSERT_EQ(a, b) do { auto a_ = (a); auto b_ = (b); if (!(a_ == b_)) throw std::runtime_error("ASSERT_EQ failed: " #a " != " #b " (" + tstr(a_) + " vs " + tstr(b_) + ", line " + std::to_string(__LINE__) + ")"); } while (0)
#define ASSERT_THROWS(expr, Ex) do { bool thrown_ = false; try { (void)(expr); } catch (const Ex&) { thrown_ = true; } \
    if (!thrown_) throw std::runtime_error("expected " #Ex " from: " #expr); } while (0)

using namespace lighter::live;
using json = nlohmann::json;
namespace fs = std::filesystem;

static constexpr int64_t kAcct = 281474;
static constexpr int kKey = 4;
static constexpr int64_t kMkt = 27;
static const char* kArchive = "/media/theone/381C48C51C48803C/rithmic_dex/lighter/AAVE/2026-10-25.jsonl.zst";

static fs::path scratch() {
    static fs::path p = [] {
        fs::path base = fs::exists("/dev/shm") ? fs::path("/dev/shm") : fs::temp_directory_path();   // fast fsync
        std::string t = (base / "mmlive_test_XXXXXX").string();
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
static int64_t t_start() { return 1'793'000'000'000; }   // 2026-10-26 ~ 10:13 UTC, a fixed instant

static std::string px3(int64_t v) { return format_decimal(v, 3); }
static json book_msg(int64_t t, int64_t bid, int64_t ask, int64_t nonce, const std::string& bsz = "5.000", const std::string& asz = "5.000",
                     bool snapshot = true, int64_t begin_nonce = -1) {
    json ob = {{"code", 0}, {"nonce", nonce}, {"bids", json::array({{{"price", px3(bid)}, {"size", bsz}}, {{"price", px3(bid - 50)}, {"size", "9.000"}}})},
               {"asks", json::array({{{"price", px3(ask)}, {"size", asz}}, {{"price", px3(ask + 50)}, {"size", "9.000"}}})}};
    if (!snapshot) ob["begin_nonce"] = begin_nonce;
    return {{"channel", "order_book:27"}, {"type", snapshot ? "subscribed/order_book" : "update/order_book"}, {"timestamp", t - 140}, {"order_book", ob}};
}

// ── the rig: engine + driver + in-process venue ─────────────────────────────────────────────────────────────
struct Rig {
    fs::path dir;
    ManualClock clock{t_start()};
    fake::FakeExchange ex{kAcct, t_start(), 7};
    fake::FakeVenue venue{ex, aave(), kKey, [this] { return clock.now_ms(); }};
    fake::FakeAccountWs aws{venue};
    fake::QueueWs pws;
    fake::QueueWs bws;          // Binance reference stream (bn_pull tests)
    bool with_ref = false;
    std::vector<std::string> alerts;
    Alerter alerter{[this](const std::string& s) { alerts.push_back(s); }, 300'000};
    std::vector<std::string> logs;
    EngineConfig cfg;
    std::unique_ptr<RiskGovernor> risk;
    std::unique_ptr<Journal> journal;
    std::unique_ptr<Engine> eng;
    std::unique_ptr<Driver> drv;
    int64_t bid = 135'000, ask = 135'100, book_nonce = 1;
    std::string bsz = "5.000", asz = "5.000";
    bool push_books = true;
    bool armed = true;
    int64_t run_id = 5;

    ~Rig() {
        if (!std::getenv("DUMP")) return;
        for (auto& l : logs) std::cout << "      | " << l << "\n";
        if (std::string(std::getenv("DUMP")) == "j") for (const auto& f : fs::directory_iterator(cfg.journal_dir)) std::cout << std::ifstream(f.path()).rdbuf();
    }
    explicit Rig(const std::string& name, Mode mode = Mode::Live) : dir(fresh_dir(name)) {
        ensure_client();
        cfg.mode = mode;
        cfg.label = "test";
        cfg.account_index = kAcct;
        cfg.api_key_index = kKey;
        cfg.spec = aave();
        cfg.hl_sz_decimals = 2;
        cfg.risk.max_gross_micro = 24 * kMicro;   // two-sided quoting ($11 + $11)
        cfg.state_dir = (dir / "state").string();
        cfg.journal_dir = (dir / "journal").string();
        cfg.status_file = (dir / "status.json").string();
        fs::create_directories(cfg.state_dir);
        ex.set_next_nonce(kKey, 1);
        ex.set_market(kMkt, 45, int64_t(10) * 1'000'000);   // $10 at sd3+pd3 = 10 × 10^6 units
        ex.set_min_cancel_all_ms(5 * 60'000);
        ex.set_schedule_mode(fake::FakeExchange::ScheduleMode::Replace);
        clock.on_sleep = [this](int64_t t) { ex.set_now(t); };
        venue.on_public(book_msg(clock.now_ms(), bid, ask, book_nonce));
    }
    bool start() {
        risk = std::make_unique<RiskGovernor>(cfg.risk, cfg.state_dir, RiskGovernor::Open::Init);
        journal = std::make_unique<Journal>(cfg.journal_dir, cfg.label);
        eng = std::make_unique<Engine>(cfg, Engine::Deps{signer(), venue, clock, *risk, *journal, alerter,
                                                        [this](int64_t) { return armed; },
                                                        [this](const std::string& s) { logs.push_back(s); }}, run_id);
        if (!eng->start()) return false;
        drv = std::make_unique<Driver>(*eng, aws, pws, clock, nullptr, with_ref ? &bws : nullptr);
        step(0);
        return true;
    }
    void push_book() {
        ++book_nonce;
        json m = book_msg(clock.now_ms(), bid, ask, book_nonce, bsz, asz);
        venue.on_public(m);
        pws.push(m.dump());
    }
    void step(int64_t ms) {
        clock.set(clock.now_ms() + ms);
        ex.set_now(clock.now_ms());
        if (push_books) push_book();
        drv->step();
    }
    void run(int64_t ms, int64_t every = 100) { for (int64_t t = 0; t < ms; t += every) step(every); }
    int64_t resting(bool ask_side) {
        for (const auto& o : ex.resting(kMkt)) if (o.is_ask == ask_side && o.client_index > 0) return o.client_index;
        return 0;
    }
    bool alerted(const std::string& needle) const {
        for (const auto& a : alerts) if (a.find(needle) != std::string::npos) return true;
        return false;
    }
    int journal_count(const std::string& ev) const {
        int n = 0;
        for (const auto& f : fs::directory_iterator(cfg.journal_dir)) {
            std::ifstream in(f.path());
            std::string line;
            while (std::getline(in, line)) if (line.find("\"ev\":\"" + ev + "\"") != std::string::npos) ++n;
        }
        return n;
    }
};

// ═════════════════════════════════ account feed protocol ═════════════════════════════════

struct CaptureSink : AccountFeed::Sink {
    std::vector<AcctOrder> orders; std::vector<AcctFill> fills; std::vector<AcctPosition> pos; std::vector<AcctTx> txs;
    AcctStats stats; std::vector<std::string> notes; int snapshots = 0;
    void on_orders(const std::vector<AcctOrder>& o, bool s, int64_t) override { orders.insert(orders.end(), o.begin(), o.end()); snapshots += s; }
    void on_fills(const std::vector<AcctFill>& f, bool s, int64_t) override { fills.insert(fills.end(), f.begin(), f.end()); snapshots += s; }
    void on_positions(const std::vector<AcctPosition>& p, bool s, int64_t) override { pos.insert(pos.end(), p.begin(), p.end()); snapshots += s; }
    void on_stats(const AcctStats& s, int64_t) override { stats = s; }
    void on_txs(const std::vector<AcctTx>& t, int64_t) override { txs.insert(txs.end(), t.begin(), t.end()); }
    void on_feed_note(const std::string& w, int64_t) override { notes.push_back(w); }
};
static AccountFeed make_feed(int* mints) {
    return AccountFeed(kAcct, Specs{{kMkt, aave()}}, [mints](int64_t deadline, int64_t) {
        ++*mints;
        return std::to_string(deadline) + ":" + std::to_string(kAcct) + ":4:sig" + std::to_string(*mints);
    });
}
static std::string snap(const std::string& name) {
    return json({{"channel", name + ":" + std::to_string(kAcct)}, {"type", "subscribed/" + name}}).dump();
}

TEST(feed_subscribes_after_connected_with_auth_only_where_required) {
    int mints = 0;
    AccountFeed f = make_feed(&mints);
    CaptureSink s;
    f.on_open(1'000);
    ASSERT(f.on_frame(R"({"type":"pong"})", 1'000, s).empty());
    auto subs = f.on_frame(R"({"session_id":"x","type":"connected"})", 1'010, s);
    ASSERT_EQ(subs.size(), size_t{5});
    int with_auth = 0;
    for (auto& x : subs) {
        auto j = json::parse(x);
        ASSERT(j["type"] == "subscribe");
        bool a = j.contains("auth");
        with_auth += a;
        std::string ch = j["channel"];
        ASSERT(a == (ch.rfind("account_all_orders/", 0) == 0 || ch.rfind("account_tx/", 0) == 0));
        ASSERT(ch.find("/" + std::to_string(kAcct)) != std::string::npos);
    }
    ASSERT_EQ(with_auth, 2);
    ASSERT(!f.live());
    for (auto n : AccountFeed::channel_names()) f.on_frame(snap(n), 1'100, s);
    ASSERT(f.live());
    // a frame for another account is ignored
    f.on_frame(json({{"channel", "account_all_positions:999"}, {"type", "update/account_all_positions"}, {"positions", {{"27", {{"market_id", 27}, {"position", "1.000"}, {"sign", 1}}}}}}).dump(), 1'200, s);
    ASSERT(s.pos.empty());
}

TEST(feed_auth_token_renewed_every_6h_deadline_within_8h) {
    int mints = 0;
    AccountFeed f = make_feed(&mints);
    CaptureSink s;
    int64_t t = 1'793'000'000'000;
    f.on_open(t);
    f.on_frame(R"({"type":"connected"})", t, s);
    for (auto n : AccountFeed::channel_names()) f.on_frame(snap(n), t, s);
    ASSERT_EQ(mints, 1);
    ASSERT(f.token_deadline_s() <= t / 1000 + 8 * 3600);
    ASSERT(f.token_deadline_s() > t / 1000 + 7 * 3600);
    // keep the connection talking; nothing renews before 6 h
    int64_t now = t;
    for (; now < t + 6 * 3600'000 - 60'000; now += 60'000) { f.on_frame(R"({"type":"pong"})", now, s); f.tick(now); }
    ASSERT_EQ(mints, 1);
    now = t + 6 * 3600'000;
    f.on_frame(R"({"type":"pong"})", now, s);
    auto out = f.tick(now);
    ASSERT_EQ(mints, 2);
    ASSERT_EQ(f.renewals(), 1);
    int unsub = 0, resub = 0;
    for (auto& x : out) {
        auto j = json::parse(x);
        if (j["type"] == "unsubscribe") ++unsub;
        if (j["type"] == "subscribe") { ++resub; ASSERT(j.contains("auth")); ASSERT(j["auth"].get<std::string>().find(std::to_string(f.token_deadline_s())) == 0); }
    }
    ASSERT_EQ(unsub, 2);
    ASSERT_EQ(resub, 2);
    ASSERT(f.token_deadline_s() <= now / 1000 + 8 * 3600);
    ASSERT(f.live());                                       // quoting continues across a renewal
    f.on_frame(snap("account_all_orders"), now + 100, s);
    f.on_frame(snap("account_tx"), now + 100, s);
    ASSERT(!f.renewing());
    ASSERT(!f.want_reconnect());
    // a renewal whose snapshots never come recycles the connection
    int64_t later = now + 6 * 3600'000;
    for (int64_t x = now; x <= later; x += 60'000) { f.on_frame(R"({"type":"pong"})", x, s); f.tick(x); }
    for (int64_t x = later; x <= later + 16'000; x += 1'000) { f.on_frame(R"({"type":"pong"})", x, s); f.tick(x); }
    ASSERT(f.want_reconnect());
}

TEST(feed_keepalive_ping_pong_shutdown_silence_and_error_frames) {
    int mints = 0;
    AccountFeed f = make_feed(&mints);
    CaptureSink s;
    f.on_open(0);
    f.on_frame(R"({"type":"connected"})", 0, s);
    for (auto n : AccountFeed::channel_names()) f.on_frame(snap(n), 0, s);
    auto out = f.tick(60'000);
    ASSERT(out.size() == 1 && out[0] == R"({"type":"ping"})");       // ≤ 2 min keepalive
    auto pong = f.on_frame(R"({"type":"ping"})", 60'100, s);
    ASSERT(pong.size() == 1 && pong[0] == R"({"type":"pong"})");
    f.on_frame(R"({"error":{"code":30003,"message":"already subscribed"}})", 60'200, s);
    ASSERT(!f.want_reconnect());
    f.on_frame(R"({"type":"shutdown","close_in_ms":5000})", 60'300, s);
    ASSERT(f.want_reconnect());
    ASSERT(f.reconnect_reason().find("shutdown") != std::string::npos);
    f.clear_reconnect();
    f.on_frame(R"({"error":{"code":20013,"message":"invalid auth: account_all_orders:281474"}})", 60'400, s);
    ASSERT(f.want_reconnect());
    ASSERT(f.token().empty());                             // re-minted on the next connect
    // silence
    AccountFeed g = make_feed(&mints);
    g.on_open(0);
    g.on_frame(R"({"type":"connected"})", 0, s);
    for (auto n : AccountFeed::channel_names()) g.on_frame(snap(n), 0, s);
    ASSERT(!g.want_reconnect());
    g.tick(149'000);
    ASSERT(!g.want_reconnect());
    g.tick(150'000);
    ASSERT(g.want_reconnect());
}

TEST(parsers_read_lighters_shapes) {
    // the spec's real mainnet trade (account X = 281474 here), ids past double precision
    json t = json::parse(R"({"trade_id":36524584584,"trade_id_str":"36524584584","type":"trade","market_id":27,"size":"0.082",
        "price":"135.120","ask_id":281478100207421,"ask_id_str":"281478100207421","bid_id":562946718494060,
        "ask_client_id":160715583723574,"ask_client_id_str":"160715583723574","bid_client_id":1792581973634,
        "ask_account_id":281474,"bid_account_id":746193,"is_maker_ask":true,"timestamp":1793003940155})");
    auto f = parse_fill(t, kAcct, Specs{{kMkt, aave()}});
    ASSERT(f.has_value());
    ASSERT(f->is_ask);
    ASSERT(f->maker);
    ASSERT_EQ(f->client_index, int64_t{160715583723574});
    ASSERT_EQ(f->order_index, int64_t{281478100207421});
    ASSERT_EQ(f->qty, int64_t{82});
    ASSERT_EQ(f->price, int64_t{135120});
    ASSERT(!parse_fill(t, 12345, Specs{{kMkt, aave()}}).has_value());
    auto p = parse_position(json::parse(R"({"market_id":27,"position":"0.082","sign":-1,"initial_margin_fraction":"20.00"})"), Specs{{kMkt, aave()}});
    ASSERT_EQ(p.base, int64_t{-82});
    ASSERT(std::fabs(leverage_from_imf(p.imf) - 5.0) < 1e-9);
    auto o = parse_order(json::parse(R"({"order_index":281474976710700,"client_order_index":5242881,"market_index":27,"is_ask":false,
        "status":"canceled-post-only","initial_base_amount":"0.082","remaining_base_amount":"0.082","filled_base_amount":"0.000","price":"135.000"})"), Specs{{kMkt, aave()}});
    ASSERT_EQ(o.status, 5);
    ASSERT_EQ(o.base, int64_t{82});
    ASSERT(o.market_known);
    auto tx = parse_tx(json::parse(R"({"hash":"ab","type":14,"status":0,"nonce":9,"api_key_index":4,"account_index":281474,"event_info":"{\"ae\":\"21706: below min\"}"})"));
    ASSERT_EQ(tx.app_error, 21706);
    ASSERT_EQ(parse_money_micro("-12.3456789"), int64_t{-12'345'678});
    ASSERT_EQ(format_decimal(82, 3), std::string("0.082"));
    ASSERT_EQ(format_decimal(-135000, 3), std::string("-135.000"));
}

// ═════════════════════════════════ sender ═════════════════════════════════

TEST(sender_forms_and_classification) {
    SignedTx a; a.tx_type = 14; a.tx_info = R"({"Nonce":1,"Sig":"a+b/c=="})";
    SignedTx b; b.tx_type = 15; b.tx_info = R"({"Nonce":2})";
    auto f1 = form_decode(Sender::form_send_tx(a));
    ASSERT(form_get(f1, "tx_type") == "14");
    ASSERT(form_get(f1, "tx_info") == a.tx_info);                       // '+' '/' '=' survive the encoding
    auto f2 = form_decode(Sender::form_send_batch({a, b}));
    ASSERT(json::parse(form_get(f2, "tx_types")) == json::array({14, 15}));
    auto infos = json::parse(form_get(f2, "tx_infos"));
    ASSERT(infos.is_array() && infos[0].is_string() && infos[0] == a.tx_info);   // array of STRINGS
    using K = SendTxResult::Kind;
    ASSERT(Sender::classify({200, true, R"({"code":200,"tx_hash":"x"})", ""}, 1).kind == K::Accepted);
    ASSERT(Sender::classify({200, true, R"({"code":21706})", ""}, 1).kind == K::Unknown);   // HTTP 200 alone proves nothing
    ASSERT(Sender::classify({429, true, "", ""}, 1).kind == K::RateLimited);
    ASSERT(Sender::classify({405, true, "", ""}, 2).kind == K::RateLimited);
    ASSERT(Sender::classify({400, true, R"({"code":23000})", ""}, 1).kind == K::RateLimited);
    ASSERT(Sender::classify({400, true, R"({"code":21104})", ""}, 1).kind == K::InvalidNonce);
    ASSERT(Sender::classify({400, true, R"({"code":21105})", ""}, 3).kind == K::InvalidNonce);
    ASSERT(Sender::classify({400, true, R"({"code":52002})", ""}, 1).kind == K::StandardLimit);
    ASSERT(Sender::classify({400, true, R"({"code":21751})", ""}, 1).kind == K::ApiRejected);
    ASSERT(Sender::classify({400, true, R"({"code":21728})", ""}, 2).kind == K::Unknown);   // a batch: prefix unknown
    ASSERT(Sender::classify({502, true, "bad gateway", ""}, 1).kind == K::Unknown);
    ASSERT(Sender::classify({0, true, "", "timeout"}, 1).kind == K::Unknown);              // timeout ≠ failed
    ASSERT(Sender::classify({0, false, "", "refused"}, 1).kind == K::NotSent);
}

// ═════════════════════════════════ nonce: Accepted-pending ═════════════════════════════════

TEST(nonce_accepted_pending_then_rejected_before_apply) {
    NonceManager nm(kKey);
    nm.resync(10);
    int64_t a = nm.reserve(), b = nm.reserve(), c = nm.reserve();
    for (int64_t n : {a, b, c}) { nm.mark_sent(n); nm.on_accepted(n); }
    ASSERT_EQ(nm.accepted_pending(), size_t{3});
    ASSERT(!nm.frozen());
    ASSERT_EQ(nm.reserve(), 13);                           // accepted ≠ executed, but the stream moves on
    nm.release_unsent(13);
    // the sequencer rejects b BEFORE applying: b and every later tx (c) fail, nothing consumed from b on
    auto resign = nm.on_outcome(b, classify_sequencer(-1, 21116));
    ASSERT(resign == (std::vector<int64_t>{b, c}));
    ASSERT(nm.frozen());
    ASSERT_THROWS(nm.reserve(), NonceError);
    ASSERT(nm.tracking(a));                                // a is still pending its own result
    ASSERT(!nm.tracking(c));
    nm.on_outcome(a, TxOutcome::Executed);
    auto r = nm.resync(11);                                // server: a consumed, b not
    ASSERT(r.unconfirmed.empty());
    ASSERT(!nm.frozen());
    ASSERT_EQ(nm.reserve(), 11);                           // b's nonce is handed out again
}

TEST(nonce_accepted_pending_survives_resync_and_settles_fifo) {
    NonceManager nm(kKey);
    nm.resync(100);
    int64_t a = nm.reserve(), b = nm.reserve();
    nm.mark_sent(a); nm.on_accepted(a);
    nm.mark_sent(b); nm.on_accepted(b);
    auto r = nm.resync(100);                               // neither executed yet: still queued, NOT unconfirmed
    ASSERT(r.unconfirmed.empty());
    ASSERT(r.accepted == (std::vector<int64_t>{a, b}));
    ASSERT(!nm.frozen());
    ASSERT_EQ(nm.peek_next(), 102);                        // their nonces are never handed out again
    nm.on_outcome(b, TxOutcome::Executed);                 // b executed ⇒ a was processed first (one FIFO per key)
    ASSERT(!nm.tracking(a));
    ASSERT_EQ(nm.accepted_pending(), size_t{0});
    ASSERT_EQ(nm.consumed_floor(), 102);
}

TEST(engine_accepted_then_rejected_before_apply_recovers) {
    Rig r("seq_reject");
    ASSERT(r.start());
    r.run(2'000);
    ASSERT_EQ(r.ex.open_orders(), 2);
    // the next tx the engine sends is accepted by the API, then rejected by the sequencer before applying
    r.ex.inject_reject_before_apply(r.ex.next_nonce(kKey));
    r.bid = 135'030; r.ask = 135'130;                         // the target moves → cancels go out on that nonce
    r.run(30'000);
    ASSERT(r.eng->sequencer_rejects() >= 1);
    ASSERT(r.alerted("rejected nonce"));
    ASSERT(!r.eng->nonces().frozen());
    ASSERT_EQ(r.eng->nonces().peek_next(), r.ex.next_nonce(kKey));
    ASSERT_EQ(r.ex.open_orders(), 2);                         // quoting at the new prices again
    for (const auto& [coi, o] : r.eng->tracker().all()) ASSERT(o.state != OrderState::Unknown);
    ASSERT_EQ(r.eng->position(), r.ex.position(kMkt));
}

// ═════════════════════════════════ engine: start, quoting, fills ═════════════════════════════════

TEST(startup_cancels_everything_and_adopts_the_exchange_position) {
    Rig r("startup");
    r.ex.set_position(kMkt, 82);                           // a position left by an earlier run
    r.ex.place_foreign(kMkt, false, 82, 134'000, (int64_t{2} << 20) | 9);   // an order left by run 2
    ASSERT_EQ(r.ex.open_orders(), 1);
    ASSERT(r.start());
    ASSERT_EQ(r.ex.open_orders(), 0);                      // cancel-all, waited for the exchange to show none
    ASSERT_EQ(r.eng->position(), int64_t{82});             // adopted, never treated as zero
    r.run(500);
    ASSERT_EQ(r.ex.scheduled_cancel_alls(), size_t{1});    // dead-man armed once the account stream is live
    ASSERT(r.alerted("adopted exchange position 0.082"));
    r.run(3'000);
    // long 1 quote = the cap: only the ask (reducing) side quotes
    ASSERT(r.resting(true) != 0);
    ASSERT(r.resting(false) == 0);
}

TEST(startup_refuses_capital_guard_and_metadata_mismatch) {
    {
        Rig r("capital");
        r.venue.collateral_micro = 80 * kMicro;            // above max_collateral ($50)
        ASSERT(!r.start());
        ASSERT(r.alerted("capital guard"));
        ASSERT_EQ(r.ex.accepted_requests(), 0);            // not one tx sent
    }
    {
        Rig r("meta");
        r.venue.symbol_override = "BNB";
        ASSERT(!r.start());
        ASSERT(r.alerted("metadata differs"));
        ASSERT_EQ(r.ex.accepted_requests(), 0);
    }
    {
        Rig r("unconfigured");
        r.ex.set_position(99, 5);                          // a position in a market we do not trade
        ASSERT(!r.start());
        ASSERT(r.alerted("unconfigured market"));
    }
}

TEST(quotes_post_only_both_sides_at_the_strategy_prices) {
    Rig r("quotes");
    ASSERT(r.start());
    r.run(2'000);
    auto rest = r.ex.resting(kMkt);
    ASSERT_EQ(rest.size(), size_t{2});
    for (const auto& o : rest) {
        ASSERT_EQ(o.tif, 2);                               // post-only
        ASSERT_EQ(o.base, int64_t{82});                    // $11 rounded UP to the 0.001 grid at 135
        ASSERT(!o.reduce_only);
        ASSERT_EQ(o.price, o.is_ask ? int64_t{135'100} : int64_t{135'000});
    }
    ASSERT(r.eng->quoting_allowed(r.clock.now_ms()));
    // the journal holds each signed tx and each send result
    ASSERT(r.journal_count("tx") >= 4);
    ASSERT(r.journal_count("send") >= 2);
    // order expiry ≈ now + 10 min (second dead-man layer)
    bool expiry_ok = false;
    for (const auto& f : fs::directory_iterator(r.cfg.journal_dir)) {
        std::ifstream in(f.path());
        std::string line;
        while (std::getline(in, line)) {
            auto j = json::parse(line);
            if (j.value("kind", "") == "create") expiry_ok = std::llabs(j["expiry"].get<int64_t>() - j["t"].get<int64_t>() - 600'000) < 1'000;
        }
    }
    ASSERT(expiry_ok);
}

TEST(fill_moves_the_position_at_once_and_caps_that_side) {
    Rig r("fill");
    ASSERT(r.start());
    r.run(2'000);
    int64_t bid_coi = r.resting(false);
    ASSERT(bid_coi);
    ASSERT(r.ex.hit(bid_coi, 82));
    r.step(50);
    ASSERT_EQ(r.eng->position(), int64_t{82});              // the stream's fill moved it
    ASSERT_EQ(r.eng->tracker().held_fills(), size_t{0});    // never the held path
    ASSERT_EQ(r.eng->tracker().get(bid_coi).state, OrderState::Filled);
    r.run(3'000);
    ASSERT_EQ(r.resting(false), int64_t{0});                // long one quote: no new bid
    ASSERT(r.resting(true) != 0);
    ASSERT_EQ(r.journal_count("fill"), 1);
    ASSERT(r.alerted("first fill of the day"));
    ASSERT_EQ(r.eng->mismatch_acts(), int64_t{0});
}

// 2026-10-28 live: a $11 sell at a higher price is smaller than the $11 buy it closes; the closing quote is sized to the
// position so no unsellable dust is left.
TEST(closing_quote_is_sized_to_the_position_not_left_as_dust) {
    Rig r("dust");
    ASSERT(r.start());
    r.run(2'000);
    const int64_t bid_coi = r.resting(false);
    ASSERT(r.ex.hit(bid_coi, 82));                          // long 0.082 AAVE bought at 135
    r.step(50);
    ASSERT_EQ(r.eng->position(), int64_t{82});
    r.bid = 140'000; r.ask = 140'100;                        // $11 at 140.1 is 0.079: 3 short of the position
    r.run(3'000);
    int64_t ask_base = 0;
    for (const auto& o : r.ex.resting(kMkt)) if (o.is_ask && o.client_index > 0) ask_base = o.base;
    ASSERT_EQ(ask_base, int64_t{82});                       // the whole position, not 79
    ASSERT_EQ(r.resting(false), int64_t{0});                // still capped on the bid side
}

TEST(day_summary_counts_fills_of_earlier_runs_today) {
    Rig r("dayseed");
    ASSERT(r.start());
    r.run(2'000);
    ASSERT(r.ex.hit(r.resting(false), 82));
    r.step(50);
    ASSERT_EQ(r.eng->status(r.clock.now_ms())["fills_today"].get<int64_t>(), int64_t{1});
    r.eng->shutdown("test");
    r.run_id = 6;                                            // a restart the same UTC day
    ASSERT(r.start());
    r.run(500);
    ASSERT_EQ(r.eng->status(r.clock.now_ms())["fills_today"].get<int64_t>(), int64_t{1});
}

TEST(fill_after_cancel_confirm_takes_the_live_path) {
    Rig r("late_fill");
    r.ex.set_late_fill_prob(1.0);                           // every fill inside the cancel delay arrives AFTER the confirm
    ASSERT(r.start());
    r.run(2'000);
    int64_t bid_coi = r.resting(false);
    ASSERT(bid_coi);
    r.bid = 135'050; r.ask = 135'150;                       // 5-tick jump: pause → both quotes cancelled
    r.step(10);
    r.step(10);
    ASSERT(r.eng->tracker().get(bid_coi).state == OrderState::CancelPending);
    ASSERT(r.ex.hit(bid_coi, 40));                          // a taker hits it inside the 300 ms bump
    r.run(1'000, 50);
    ASSERT(r.eng->tracker().late_fills() >= 1);             // applied to a terminal (Cancelled) order …
    ASSERT_EQ(r.eng->tracker().get(bid_coi).state, OrderState::Cancelled);
    ASSERT_EQ(r.eng->position(), int64_t{40});              // … and the position moved
    ASSERT_EQ(r.ex.position(kMkt), int64_t{40});
    ASSERT_EQ(r.eng->tracker().held_fills(), size_t{0});
    r.run(10'000);
    ASSERT_EQ(r.eng->mismatch_acts(), int64_t{0});
}

TEST(account_stream_drop_cancels_all_then_backfills_and_reconciles) {
    Rig r("reconnect");
    r.venue.snapshot_trades = 0;                            // the reconnect snapshot carries no fills: only REST can
    ASSERT(r.start());
    r.run(2'000);
    int64_t bid_coi = r.resting(false);
    r.venue.drop_account_ws();
    ASSERT(r.ex.hit(bid_coi, 82));                          // a fill while the stream is down — never streamed
    r.step(100);
    ASSERT(r.eng->halted("account_feed"));
    ASSERT(r.alerted("account stream down"));
    r.run(500);
    ASSERT_EQ(r.ex.open_orders(), 0);                       // cancel-all on the drop
    r.run(15'000);
    ASSERT(r.aws.opens >= 2);                               // reconnected
    ASSERT(r.eng->account_feed().live());
    ASSERT_EQ(r.eng->position(), int64_t{82});              // the fill came from the REST backfill
    ASSERT_EQ(r.eng->fills(), int64_t{1});
    ASSERT_EQ(r.eng->mismatch_acts(), int64_t{0});          // applied before any mismatch could act
    ASSERT(!r.eng->halted("account_feed"));
    ASSERT(r.resting(true) != 0);                           // quoting again (reducing side only: long one quote)
    ASSERT_EQ(r.eng->tracker().get(bid_coi).state, OrderState::Filled);
}

TEST(duplicate_fill_from_stream_and_rest_counts_once) {
    Rig r("dup");
    ASSERT(r.start());
    r.run(2'000);
    int64_t bid_coi = r.resting(false);
    r.ex.hit(bid_coi, 30);
    r.step(50);
    ASSERT_EQ(r.eng->position(), int64_t{30});
    r.venue.drop_account_ws();                              // reconnect: the snapshot's 50 trades + REST backfill
    r.run(15'000);                                          // repeat the same fill twice more
    ASSERT_EQ(r.eng->position(), int64_t{30});
    ASSERT_EQ(r.eng->fills(), int64_t{1});
    ASSERT(r.eng->tracker().duplicate_fills() >= 1);
    ASSERT_EQ(r.journal_count("fill"), 1);
}

TEST(unknown_exchange_order_from_an_earlier_run_is_cancelled) {
    Rig r("leftover");
    ASSERT(r.start());
    r.run(2'000);
    int64_t oi = r.ex.place_foreign(kMkt, true, 82, 140'000, (int64_t{3} << 20) | 4);   // run 3 < run 5
    r.run(13'000);
    ASSERT(r.ex.find_view(oi)->status != 2);                 // cancelled by the reconciler
    ASSERT(r.alerted("still on the book"));
    ASSERT(!r.eng->halted("unowned"));
    ASSERT_EQ(r.ex.open_orders(), 2);                       // our two quotes stay
}

TEST(foreign_order_halts_quoting_account_used_by_something_else) {
    Rig r("foreign");
    ASSERT(r.start());
    r.run(2'000);
    int64_t oi = r.ex.place_foreign(kMkt, false, 100, 130'000, 0);   // placed from the Lighter UI (no client index)
    r.run(13'000);
    ASSERT(r.ex.find_view(oi)->status != 2);                 // cancelled by exchange order index
    ASSERT(r.eng->halted("unowned"));
    ASSERT(r.alerted("account used by something else"));
    ASSERT_EQ(r.ex.open_orders(), 0);                       // our quotes pulled (cancel-all)
    r.run(20'000);
    ASSERT_EQ(r.ex.open_orders(), 0);                       // sticky: nothing new
}

TEST(position_mismatch_halts_alerts_and_adopts_the_exchange) {
    Rig r("mismatch");
    ASSERT(r.start());
    r.run(2'000);
    r.ex.set_position(kMkt, -82);                           // the exchange moved without telling the stream …
    r.venue.collateral_micro += 82 * 135'050;            // … at the mid (equity unchanged: no loss stop)
    r.run(25'000);
    ASSERT_EQ(r.eng->mismatch_acts(), int64_t{1});
    ASSERT_EQ(r.eng->position(), int64_t{-82});             // the exchange's, never the local one
    ASSERT(r.alerted("position mismatch"));
    ASSERT(r.journal_count("position_adopt") == 1);
    ASSERT(r.eng->halted("position_mismatch") || r.journal_count("resume") >= 1);
    r.run(30'000);                                          // two consistent reconciles → quoting resumes
    ASSERT(!r.eng->halted("position_mismatch"));
    ASSERT(r.resting(false) != 0);                          // short one quote: only the bid (reducing) quotes
    ASSERT_EQ(r.resting(true), int64_t{0});
}

TEST(leverage_mismatch_refuses_to_quote_until_fixed_in_the_app) {
    Rig r("leverage");
    r.venue.imf = "10.00";                                  // 10× on the exchange, config says 5×
    ASSERT(r.start());
    r.run(5'000);
    ASSERT_EQ(r.ex.open_orders(), 0);
    ASSERT(r.eng->halted("leverage"));
    ASSERT(r.alerted("leverage"));
    r.venue.imf = "20.00";                                  // the founder sets 5× in the app
    r.run(12'000);
    ASSERT(!r.eng->halted("leverage"));
    ASSERT_EQ(r.ex.open_orders(), 2);
    Rig u("leverage_unread");
    u.venue.imf_present = false;
    ASSERT(u.start());
    u.run(5'000);
    ASSERT_EQ(u.ex.open_orders(), 0);
    ASSERT(u.eng->halted("leverage"));
}

TEST(journal_failure_halts_and_cancels_all) {
    Rig r("journal");
    ASSERT(r.start());
    r.run(2'000);
    ASSERT_EQ(r.ex.open_orders(), 2);
    r.journal->open_path("/dev/full");                       // every write now fails with ENOSPC
    r.bid = 135'030; r.ask = 135'130;                         // the next tx must be journalled first → fails
    r.run(2'000);
    ASSERT(r.eng->halted("journal"));
    ASSERT(r.alerted("JOURNAL FAILURE"));
    ASSERT_EQ(r.ex.open_orders(), 0);                        // cancel-all went out anyway (unjournalled)
    int64_t sends = r.eng->sends();
    r.run(20'000);
    ASSERT_EQ(r.ex.open_orders(), 0);                        // never trades without a record again
    ASSERT(r.eng->sends() - sends <= 1);                     // at most the dead-man re-arm path tries
}

// ═════════════════════════════════ dead-man ═════════════════════════════════

TEST(deadman_is_rearmed_every_20s_with_the_5_minute_window) {
    Rig r("deadman");
    ASSERT(r.start());
    r.run(500);                                              // armed once the account stream is live
    int64_t armed_at = r.clock.now_ms() - 500;
    int64_t deadline = r.ex.scheduled_cancel_all_time();
    ASSERT(deadline >= armed_at + 5 * 60'000);               // Lighter's minimum
    ASSERT(deadline <= armed_at + 315'000 + 1'000);
    int64_t arms = r.eng->deadman_arms();
    r.run(61'000);
    ASSERT(r.eng->deadman_arms() - arms >= 3);
    ASSERT_EQ(r.ex.scheduled_cancel_alls(), size_t{1});       // abort + schedule: never piles up
    ASSERT(r.ex.scheduled_cancel_all_time() >= r.clock.now_ms() + 5 * 60'000 - 20'000);
    ASSERT_EQ(r.eng->deadman_failures(), 0);
    ASSERT(r.eng->deadman_deadline() > armed_at);             // confirmed by the sequencer result
}

TEST(deadman_cancels_everything_when_the_process_stops) {
    Rig r("deadman_fire");
    ASSERT(r.start());
    r.run(2'000);
    ASSERT_EQ(r.ex.open_orders(), 2);
    r.ex.set_now(r.clock.now_ms() + 4 * 60'000);             // the process is gone (no more ticks)
    ASSERT_EQ(r.ex.open_orders(), 2);
    r.ex.set_now(r.clock.now_ms() + 6 * 60'000);
    ASSERT_EQ(r.ex.open_orders(), 0);                        // the exchange cancelled them itself
    ASSERT_EQ(r.ex.dead_man_fired(), 1);
}

// ═════════════════════════════════ rate limit, timeouts ═════════════════════════════════

TEST(http_429_backs_off_and_alerts) {
    Rig r("rate");
    ASSERT(r.start());
    r.run(2'000);
    for (int k = 0; k < 60; ++k) r.ex.send_tx(14, "{}");     // someone else spent the minute on this address
    r.bid = 135'030; r.ask = 135'130;
    r.run(1'000);
    ASSERT(r.eng->rate_limited_events() >= 1);
    ASSERT(r.alerted("rate limited"));
    ASSERT(r.eng->limiter().backing_off(r.clock.now_ms()));
    int64_t sends = r.eng->sends();
    int rest = r.venue.rest_calls;
    r.run(55'000);
    ASSERT_EQ(r.eng->sends(), sends);                         // nothing at all for 60 s
    ASSERT_EQ(r.venue.rest_calls, rest);
    ASSERT_EQ(r.ex.scheduled_cancel_alls(), size_t{1});       // the armed dead-man still covers us
    r.run(15'000);
    ASSERT(r.eng->sends() > sends);
    ASSERT_EQ(r.ex.open_orders(), 2);
    for (const auto& o : r.ex.resting(kMkt)) ASSERT_EQ(o.price, o.is_ask ? int64_t{135'130} : int64_t{135'030});
}

TEST(timeout_is_unknown_never_failed_and_reconcile_resolves_it) {
    Rig r("timeout_applied");
    ASSERT(r.start());
    r.run(2'000);
    r.venue.drop_next_sends = 1;                              // the exchange applies it, the answer never comes
    r.venue.drop_applies = true;
    r.bid = 135'030; r.ask = 135'130;
    r.run(500);
    ASSERT(r.alerted("send outcome unknown"));
    r.run(30'000);
    ASSERT(!r.eng->nonces().frozen());
    ASSERT_EQ(r.eng->nonces().peek_next(), r.ex.next_nonce(kKey));
    ASSERT_EQ(r.ex.open_orders(), 2);                         // no duplicate order on any side
    for (const auto& [coi, o] : r.eng->tracker().all()) ASSERT(o.state != OrderState::Unknown);
    ASSERT_EQ(r.eng->position(), r.ex.position(kMkt));
}

TEST(timeout_before_the_exchange_resends_identical_bytes) {
    Rig r("timeout_lost");
    ASSERT(r.start());
    r.run(2'000);
    r.venue.drop_next_sends = 1;
    r.venue.drop_applies = false;                             // lost before reaching the exchange
    r.bid = 135'030; r.ask = 135'130;
    r.run(30'000);
    ASSERT(r.journal_count("resend") >= 1);                   // the same signed bytes, the same nonce
    ASSERT(!r.eng->nonces().frozen());
    ASSERT_EQ(r.eng->nonces().peek_next(), r.ex.next_nonce(kKey));
    ASSERT_EQ(r.ex.open_orders(), 2);
    for (const auto& o : r.ex.resting(kMkt)) ASSERT_EQ(o.price, o.is_ask ? int64_t{135'130} : int64_t{135'030});
}

// ═════════════════════════════════ operator paths ═════════════════════════════════

TEST(sigterm_path_cancels_all_and_aborts_the_deadman) {
    Rig r("sigterm");
    ASSERT(r.start());
    r.run(2'000);
    ASSERT_EQ(r.ex.open_orders(), 2);
    ASSERT_EQ(r.ex.scheduled_cancel_alls(), size_t{1});
    ASSERT(r.eng->shutdown("SIGTERM"));
    ASSERT_EQ(r.ex.open_orders(), 0);
    ASSERT_EQ(r.ex.scheduled_cancel_alls(), size_t{0});
    ASSERT(r.eng->deadman_aborted());
    ASSERT_EQ(r.journal_count("stop"), 1);
    ASSERT(r.alerted("orders cancelled, dead-man aborted"));
    auto st = json::parse(std::ifstream(r.cfg.status_file));
    ASSERT(st["quoting_block"] == "stopping");
}

// 2026-10-27 live incident: a quote filled just before the engine cancelled it; mainnet rejected that cancel BEFORE
// applying (21727), which fails every later tx of the request. The engine re-queued the dead cancel on every resync,
// so each batch — the KILL flatten included — failed behind it, every 2 s, until the process was restarted.
TEST(cancel_of_a_filled_order_rejected_before_apply_does_not_loop_or_block_the_flatten) {
    Rig r("dead_cancel");
    r.ex.set_cancel_missing_rejects_before(true);
    ASSERT(r.start());
    r.run(2'000);
    int64_t bid_coi = r.resting(false);
    ASSERT(bid_coi);
    r.venue.hold_account_events = true;                       // the fill is not streamed yet …
    ASSERT(r.ex.hit(bid_coi, 82));                            // … but it is done on the exchange
    r.bid = 135'050; r.ask = 135'150;                         // the engine pulls quotes on a jump → cancel rejected
    r.run(1'000, 50);
    r.venue.hold_account_events = false;
    r.venue.release_held();                                   // now the engine learns the order filled
    r.run(10'000);
    ASSERT(r.eng->sequencer_rejects() >= 1);                  // the race happened
    ASSERT(r.eng->sequencer_rejects() <= 3);                  // … and did not loop
    ASSERT(!r.eng->nonces().frozen());
    ASSERT_EQ(r.eng->position(), r.ex.position(kMkt));
    std::ofstream(r.cfg.state_dir + "/KILL") << "x";
    r.run(15'000);
    ASSERT_EQ(r.ex.position(kMkt), int64_t{0});               // the flatten got through
    ASSERT_EQ(r.eng->position(), int64_t{0});
    ASSERT(!r.alerted("FLATTEN FAILED"));
}

// 2026-10-28 live: all 31 sequencer rejects were a cancel that lost the race to a fill already streamed. Each one
// raised a nonce_frozen HALT + Telegram alert. Now: journalled as cancel_race, the resync still runs, no halt/alert.
TEST(cancel_lost_to_a_known_fill_resyncs_quietly) {
    Rig r("race_quiet");
    r.ex.set_cancel_missing_rejects_before(true);
    ASSERT(r.start());
    r.run(2'000);
    int64_t bid_coi = r.resting(false);
    ASSERT(bid_coi);
    r.venue.hold_account_events = true;                       // fill + cancel result arrive together, fill first
    ASSERT(r.ex.hit(bid_coi, 82));
    r.bid = 135'050; r.ask = 135'150;                         // the jump sends the (dead) cancel
    r.run(1'000, 50);
    r.venue.hold_account_events = false;
    r.venue.release_held();
    r.run(10'000);
    ASSERT(r.eng->sequencer_rejects() >= 1);
    ASSERT(r.eng->cancel_races() >= 1);
    ASSERT(r.journal_count("cancel_race") >= 1);
    ASSERT(!r.alerted("sequencer rejected nonce"));
    ASSERT(!r.alerted("HALT nonce_frozen"));
    ASSERT(!r.eng->nonces().frozen());
    ASSERT_EQ(r.eng->position(), r.ex.position(kMkt));
    ASSERT(r.resting(false) != 0 || r.resting(true) != 0);   // quoting again
}

// 2026-10-28 live: periodic reconciles blocked the engine thread 2.6–3.7 s on slow REST reads (stall → 10 s without
// quotes). Periodic reads now carry an 800 ms timeout; three failed periodic reconciles in a row fall back to the full one.
TEST(periodic_reconcile_uses_a_short_timeout_and_falls_back) {
    Rig r("rec_fast");
    ASSERT(r.start());
    r.run(25'000, 100);
    int fast = 0;
    for (int t : r.venue.rest_timeouts) if (t == 800) ++fast;
    ASSERT(fast >= 2);                                        // the periodic ones ran fast
    r.venue.rest_timeouts.clear();
    r.venue.fail_next_rest = 1000;                            // REST dead
    r.run(45'000, 100);
    ASSERT(r.eng->fast_reconcile_fails() >= 3);
    bool full_seen = false;
    for (int t : r.venue.rest_timeouts) if (t > 800) full_seen = true;
    ASSERT(full_seen);                                        // after 3 failures a full-timeout reconcile went out
    r.venue.fail_next_rest = 0;
    r.run(25'000, 100);
    ASSERT_EQ(r.eng->fast_reconcile_fails(), int64_t{0});    // recovered
}

// 2026-10-27 live: Lighter closed the public stream every 2 min — the engine only answered server pings and never
// sent one, and the server drops a client that sends nothing for 2 min. The account feed and the recorder ping.
TEST(public_stream_sends_its_own_ping_every_minute) {
    Rig r("pub_ping");
    ASSERT(r.start());
    r.run(185'000, 500);
    int pings = 0;
    for (const auto& m : r.pws.sent) if (m == R"({"type":"ping"})") ++pings;
    ASSERT(pings >= 3);
    ASSERT_EQ(r.pws.opens, 1);                                // never reconnected
}

TEST(kill_file_halts_flattens_and_never_auto_resumes) {
    Rig r("kill");
    ASSERT(r.start());
    r.run(2'000);
    r.ex.hit(r.resting(false), 82);
    r.run(500);
    ASSERT_EQ(r.eng->position(), int64_t{82});
    std::ofstream(r.cfg.state_dir + "/KILL") << "x";
    r.run(15'000);
    ASSERT(r.eng->halted("kill"));
    ASSERT_EQ(r.ex.open_orders(), 0);
    ASSERT_EQ(r.ex.position(kMkt), int64_t{0});               // reduce-only IOC took it out
    ASSERT_EQ(r.eng->position(), int64_t{0});
    ASSERT(r.alerted("KILL"));
    fs::remove(r.cfg.state_dir + "/KILL");
    r.run(15'000);
    ASSERT(r.eng->halted("kill"));                            // deleting the file does not resume
    ASSERT_EQ(r.ex.open_orders(), 0);
}

// The guardian (scripts/mm_guardian.sh, 2026-10-29): started with a KILL file already in a fresh state dir while the
// exchange holds a position the main engine left behind. It must flatten and must never rest a quote, not even once.
TEST(guardian_start_with_kill_file_flattens_and_never_quotes) {
    Rig r("guardian");
    r.ex.set_position(kMkt, 123);
    std::ofstream(r.cfg.state_dir + "/KILL") << "x";
    ASSERT(r.start());
    int max_open = 0;
    for (int i = 0; i < 200; ++i) { r.step(100); max_open = std::max(max_open, r.ex.open_orders()); }
    ASSERT_EQ(max_open, 0);                                   // IOC only: nothing ever rested
    ASSERT(r.eng->halted("kill"));
    ASSERT_EQ(r.ex.position(kMkt), int64_t{0});
    ASSERT_EQ(r.eng->position(), int64_t{0});
}

TEST(pause_file_pulls_quotes_and_keeps_the_position) {
    Rig r("pause");
    ASSERT(r.start());
    r.run(2'000);
    r.ex.hit(r.resting(true), 82);
    r.run(500);
    std::ofstream(r.cfg.state_dir + "/PAUSE") << "x";
    r.run(3'000);
    ASSERT_EQ(r.ex.open_orders(), 0);
    ASSERT_EQ(r.ex.position(kMkt), int64_t{-82});             // kept
    fs::remove(r.cfg.state_dir + "/PAUSE");
    r.run(3'000);
    ASSERT(r.resting(false) != 0);
}

TEST(disarming_stops_new_orders_but_not_cancels) {
    Rig r("disarm");
    ASSERT(r.start());
    r.run(2'000);
    r.armed = false;                                          // the arming file expired / NO_DEPLOY_MM appeared
    r.bid = 135'030; r.ask = 135'130;
    r.run(3'000);
    ASSERT_EQ(r.ex.open_orders(), 0);                         // the cancels still went
    ASSERT(r.eng->halted("not_armed"));
}

TEST(book_gap_pulls_the_markets_quotes_before_resubscribing) {
    Rig r("gap");
    ASSERT(r.start());
    r.run(2'000);
    ASSERT_EQ(r.ex.open_orders(), 2);
    r.push_books = false;
    json bad = book_msg(r.clock.now_ms(), r.bid, r.ask, r.book_nonce + 5, "5.000", "5.000", false, r.book_nonce + 3);
    r.pws.push(bad.dump());
    r.step(10);
    ASSERT(r.eng->halted("book_gap"));
    ASSERT(r.pws.opens >= 2 || !r.pws.is_open());             // resubscribe requested
    r.step(400);
    ASSERT_EQ(r.ex.open_orders(), 0);                         // the per-market cancel-all landed
    r.push_books = true;
    r.run(4'000);
    ASSERT(!r.eng->halted("book_gap"));
    ASSERT_EQ(r.ex.open_orders(), 2);
}

TEST(public_feed_silence_cancels_all) {
    Rig r("silence");
    ASSERT(r.start());
    r.run(2'000);
    r.push_books = false;
    r.run(16'000);
    ASSERT(r.eng->halted("public_feed"));
    ASSERT_EQ(r.ex.open_orders(), 0);
}

TEST(single_instance_lock) {
    fs::path d = fresh_dir("lock");
    auto a = InstanceLock::acquire((d / "mm_live.lock").string());
    ASSERT(a.has_value());
    auto b = InstanceLock::acquire((d / "mm_live.lock").string());
    ASSERT(!b.has_value());                                   // "instance lock REFUSED"
    a.reset();
    auto c = InstanceLock::acquire((d / "mm_live.lock").string());
    ASSERT(c.has_value());
}

TEST(probe_runs_end_to_end_against_the_fake) {
    Rig r("probe", Mode::Probe);
    ASSERT(r.start());
    for (int k = 0; k < 600 && !r.eng->probe_done(); ++k) r.step(100);
    ASSERT(r.eng->probe_done());
    const json& rep = r.eng->probe_report();
    ASSERT(r.eng->probe_ok());
    ASSERT(rep["result"] == "PASS");
    ASSERT(rep["deadman_rearm"] == "executed");
    ASSERT(parse_decimal(rep["order"]["price"].get<std::string>(), 3) * 10 <= int64_t{135'000} * 8);   // ≥ 20 % below the bid
    ASSERT_EQ(r.ex.position(kMkt), int64_t{0});
    ASSERT(r.eng->shutdown("probe finished"));
    ASSERT_EQ(r.ex.open_orders(), 0);
    ASSERT_EQ(r.ex.scheduled_cancel_alls(), size_t{0});
    ASSERT_EQ(r.ex.trades().size(), size_t{0});               // the probe never trades
}

// ═════════════════════════════════ config, alerts, journal ═════════════════════════════════

static json gw_json(const std::string& mode) {
    return {{"mode", mode}, {"host", "mainnet.zklighter.elliot.ai"}, {"chain_id", 304}, {"account_index", kAcct}, {"api_key_index", kKey},
            {"state_dir", (scratch() / "cfg").string()}, {"market", "AAVE"},
            {"engine", {{"market_spec", {{"market_index", 27}, {"size_decimals", 3}, {"price_decimals", 3}, {"min_base", "0.045"}, {"min_quote", "10"}}},
                        {"hl_sz_decimals", 2}}}};
}

TEST(mm_live_config_is_strict) {
    auto mc = build_mm_live_config(parse_config(gw_json("offline"), Mode::Offline, 1'793'000'000));
    ASSERT_EQ(mc.engine.spec.market_index, int64_t{27});
    ASSERT_EQ(mc.engine.deadman_window_ms, int64_t{315'000});
    ASSERT(mc.engine.label == "lighter_main_offline");
    ASSERT_EQ(mc.engine.risk.leverage, 5);
    auto j = gw_json("offline"); j["engine"]["surprise"] = 1;
    ASSERT_THROWS(build_mm_live_config(parse_config(j, Mode::Offline, 1'793'000'000)), ConfigError);
    j = gw_json("offline"); j["engine"]["deadman_window_s"] = 120;   // under Lighter's 5-minute minimum
    ASSERT_THROWS(build_mm_live_config(parse_config(j, Mode::Offline, 1'793'000'000)), ConfigError);
    j = gw_json("offline"); j["engine"].erase("hl_sz_decimals");
    ASSERT_THROWS(build_mm_live_config(parse_config(j, Mode::Offline, 1'793'000'000)), ConfigError);
    j = gw_json("offline"); j["host"] = "testnet.zklighter.elliot.ai";
    ASSERT_THROWS(parse_config(j, Mode::Offline, 1'793'000'000), ConfigError);
    ASSERT_THROWS(parse_config(gw_json("live"), Mode::Offline, 1'793'000'000), ConfigError);   // mode mismatch
    ASSERT_THROWS(parse_config(gw_json("live"), Mode::Live, 1'793'000'000), ConfigError);      // no key file, not armed
}

TEST(mm_live_strategy_overrides_are_bounded_and_reach_the_quoter) {
    // no "strategy" block = exactly the frozen setting
    auto mc = build_mm_live_config(parse_config(gw_json("offline"), Mode::Offline, 1'793'000'000));
    hl::Params f = mmpaper::frozen_params(), p = f;
    mc.engine.strategy.apply(p);
    ASSERT_EQ(p.pause_move_ticks, f.pause_move_ticks); ASSERT_EQ(p.pause_ms, f.pause_ms); ASSERT_EQ(p.min_spread_ticks, f.min_spread_ticks);
    ASSERT_EQ(p.half_width_ticks, f.half_width_ticks); ASSERT_EQ(p.imb_pull, f.imb_pull); ASSERT_EQ(p.requote_ticks, f.requote_ticks);
    // the optimised pilot setting (27 Oct): 1-tick pause, 4 s, spread >= 5
    auto j = gw_json("offline"); j["engine"]["strategy"] = {{"pause_move_ticks", 1}, {"pause_ms", 4000}, {"min_spread_ticks", 5}};
    mc = build_mm_live_config(parse_config(j, Mode::Offline, 1'793'000'000));
    p = f; mc.engine.strategy.apply(p);
    ASSERT_EQ(p.pause_move_ticks, 1.0); ASSERT_EQ(p.pause_ms, int64_t{4000}); ASSERT_EQ(p.min_spread_ticks, 5.0);
    ASSERT_EQ(p.half_width_ticks, f.half_width_ticks);              // untouched knobs stay frozen
    ASSERT_EQ(p.order_usd, f.order_usd);                            // size is not a strategy knob
    // unknown keys, wrong types and out-of-range values are refused — never silently ignored
    for (const auto& bad : {nlohmann::json{{"order_usd", 100}}, nlohmann::json{{"max_inv_units", 5}}, nlohmann::json{{"pause_ms", 120000}},
                            nlohmann::json{{"half_width_ticks", 0.1}}, nlohmann::json{{"imb_pull", 2}}, nlohmann::json{{"pause_ms", "4s"}},
                            nlohmann::json(5)}) {
        auto b = gw_json("offline"); b["engine"]["strategy"] = bad;
        ASSERT_THROWS(build_mm_live_config(parse_config(b, Mode::Offline, 1'793'000'000)), ConfigError);
    }
    // margin limit: default 30 %, a $5 account holding one $11 quote at 5x needs ~44 % (founder 27 Oct: 60)
    ASSERT_EQ(build_mm_live_config(parse_config(gw_json("offline"), Mode::Offline, 1'793'000'000)).engine.max_margin_pct, 30.0);
    auto m = gw_json("offline"); m["engine"]["max_margin_pct"] = 60;
    ASSERT_EQ(build_mm_live_config(parse_config(m, Mode::Offline, 1'793'000'000)).engine.max_margin_pct, 60.0);
    for (double bad : {5.0, 95.0}) {
        m["engine"]["max_margin_pct"] = bad;
        ASSERT_THROWS(build_mm_live_config(parse_config(m, Mode::Offline, 1'793'000'000)), ConfigError);
    }
}

// ═════════════════════════════════ Binance-lead pull (bn_pull) ═════════════════════════════════

TEST(bn_lead_matches_the_lab_rule_threshold_hold_warmup_and_staleness) {
    bnpull::Lead off;
    off.on_price(0, 100); ASSERT(!off.at(10'000).bid && !off.at(10'000).ask);          // default params = off
    bnpull::Params p; p.w_ms = 500; p.x_bps = 4; p.hold_ms = 1'000;
    bnpull::Lead L(p);
    L.on_price(1'000, 100.0);
    auto o = L.at(1'200);
    ASSERT(!o.bid && !o.ask && std::isnan(o.lead_bps));                                  // warm-up: no sample at ≤ now − w
    L.on_price(1'400, 99.95);                                                             // −5 bps
    o = L.at(1'600);
    ASSERT(o.bid && !o.ask);                                                              // falling → pull the bid
    ASSERT(std::abs(o.lead_bps - (-5.0)) < 1e-9);
    L.on_price(1'700, 99.98);                                                             // −2 bps vs the 1 000 sample
    ASSERT(L.at(1'800).bid);                                                              // held (last true 1 600)
    ASSERT(L.at(2'600).bid);                                                              // 1 000 ms after 1 600: still held
    ASSERT(!L.at(2'601).bid);                                                             // released
    ASSERT_EQ(L.pulls_bid(), int64_t{1});
    L.on_price(2'700, 100.05);                                                            // +7 bps vs 99.98 at 1 700
    o = L.at(2'800);
    ASSERT(o.ask && !o.bid);
    ASSERT(!L.at(10'000).ask && std::isnan(L.last().lead_bps));                           // stale (> 5 s): no new pull
    bnpull::Lead F(p);                                                                    // a sample stamped after now is not used yet
    F.on_price(0, 100.0); F.on_price(600, 99.0);
    ASSERT(!F.at(550).bid && std::abs(F.last().lead_bps) < 1e-12);
    ASSERT(F.at(650).bid);
    bnpull::Lead E(p);                                                                    // exactly x is not beyond x
    E.on_price(0, 100.0); E.on_price(400, 100.0 * (1 - 4e-4));
    ASSERT(!E.at(500).bid);
}

TEST(bn_pull_config_is_bounded_and_both_knobs_go_together) {
    auto j = gw_json("offline");
    j["engine"]["strategy"] = {{"bn_pull_w_ms", 500}, {"bn_pull_x_bps", 4}, {"bn_pull_hold_ms", 1000}};
    auto mc = build_mm_live_config(parse_config(j, Mode::Offline, 1'793'000'000));
    ASSERT(mc.engine.strategy.bn_pull().on());
    ASSERT_EQ(mc.engine.strategy.bn_pull().hold_ms, int64_t{1000});
    ASSERT(!build_mm_live_config(parse_config(gw_json("offline"), Mode::Offline, 1'793'000'000)).engine.strategy.bn_pull().on());
    for (const auto& bad : {nlohmann::json{{"bn_pull_w_ms", 500}}, nlohmann::json{{"bn_pull_x_bps", 4}}, nlohmann::json{{"bn_pull_w_ms", 50}, {"bn_pull_x_bps", 4}},
                            nlohmann::json{{"bn_pull_w_ms", 500}, {"bn_pull_x_bps", 0.1}}, nlohmann::json{{"bn_pull_w_ms", 500}, {"bn_pull_x_bps", 4}, {"bn_pull_hold_ms", 90000}}}) {
        auto b = gw_json("offline"); b["engine"]["strategy"] = bad;
        ASSERT_THROWS(build_mm_live_config(parse_config(b, Mode::Offline, 1'793'000'000)), ConfigError);
    }
}

static std::string bn_ticker(double bid, double ask) {
    return json{{"stream", "aaveusdt@bookTicker"}, {"data", {{"e", "bookTicker"}, {"s", "AAVEUSDT"}, {"b", std::to_string(bid)}, {"B", "1"},
                                                           {"a", std::to_string(ask)}, {"A", "1"}, {"T", 1}, {"E", 1}}}}.dump();
}

TEST(binance_drop_pulls_the_live_bid_keeps_the_ask_and_requotes_after_the_hold) {
    Rig r("bnpull");
    r.with_ref = true;
    r.cfg.strategy.bn_pull_w_ms = 500; r.cfg.strategy.bn_pull_x_bps = 4; r.cfg.strategy.bn_pull_hold_ms = 1'000;
    ASSERT(r.start());
    ASSERT(r.eng->ref_wanted());
    ASSERT_EQ(r.eng->ref_ws_path(), std::string("/stream?streams=aaveusdt@bookTicker"));
    for (int i = 0; i < 20; ++i) { r.bws.push(bn_ticker(135.00, 135.02)); r.step(100); }
    ASSERT(r.resting(false) && r.resting(true));                                         // calm Binance: both sides quote
    const int64_t ask_before = r.resting(true);
    r.bws.push(bn_ticker(134.90, 134.92));                                                // −7.4 bps within 500 ms
    r.run(1'000);                                                                         // the cancel lands
    ASSERT(!r.resting(false));                                                            // bid pulled
    ASSERT_EQ(r.resting(true), ask_before);                                               // ask untouched
    ASSERT(r.journal_count("bn_pull") >= 1);
    auto st = r.eng->status(r.clock.now_ms());
    ASSERT(st["bn_pull"]["on"].get<bool>() && st["bn_pull"]["pulls_bid"].get<int64_t>() == 1);
    for (int i = 0; i < 30; ++i) { r.bws.push(bn_ticker(134.90, 134.92)); r.step(100); }   // flat again, hold expires
    ASSERT(r.resting(false));                                                             // the bid is back
    // the stream dies: stale reference = no pulls, the strategy quotes as without it
    r.bws.close();
    r.run(8'000);
    ASSERT(r.resting(false) && r.resting(true));
    ASSERT(r.eng->quoting_allowed(r.clock.now_ms()));
}

// 2026-10-28: re-opening the BOOK (or account) stream is not a stall — its loss already cancelled every quote — but a
// slow Binance open, which happens while quotes rest on the exchange, still is.
TEST(slow_book_stream_reopen_is_not_a_stall_but_a_slow_reference_open_is) {
    Rig r("slowopen");
    r.with_ref = true;
    r.cfg.strategy.bn_pull_w_ms = 500; r.cfg.strategy.bn_pull_x_bps = 4; r.cfg.strategy.bn_pull_hold_ms = 1'000;
    ASSERT(r.start());
    r.run(2'000);
    auto stalls = [&] { int n = 0; for (const auto& l : r.logs) if (l.find("[HALT] stall") != std::string::npos) ++n; return n; };
    const int s0 = stalls();
    r.pws.on_open = [&] { r.clock.set(r.clock.now_ms() + 3'000); r.ex.set_now(r.clock.now_ms()); };   // a 3 s handshake
    r.pws.close();
    r.run(3'000);
    ASSERT(r.pws.opens >= 2);
    ASSERT_EQ(stalls(), s0);                                               // not counted
    r.pws.on_open = nullptr;
    r.run(2'000);
    ASSERT(r.resting(false) && r.resting(true));                           // quoting again at once
    r.bws.on_open = [&] { r.clock.set(r.clock.now_ms() + 3'000); r.ex.set_now(r.clock.now_ms()); };
    r.bws.close();
    r.run(3'000);
    ASSERT(stalls() > s0);                                                 // a slow Binance open still pulls quotes
}

TEST(no_bn_pull_config_means_no_reference_stream_and_unchanged_quotes) {
    Rig r("bnoff");
    r.with_ref = true;
    ASSERT(r.start());
    ASSERT(!r.eng->ref_wanted());
    r.run(2'000);
    ASSERT_EQ(r.bws.opens, 0);
    ASSERT(r.resting(false) && r.resting(true));
    ASSERT(!r.eng->status(r.clock.now_ms())["bn_pull"]["on"].get<bool>());
}

TEST(alerts_are_rate_limited_and_fail_open) {
    std::vector<std::string> got;
    Alerter a([&](const std::string& s) { got.push_back(s); }, 300'000);
    ASSERT(a.alert("halt", "one", 0));
    ASSERT(!a.alert("halt", "two", 1'000));
    ASSERT(a.alert("other", "x", 1'000));
    ASSERT(a.alert("halt", "three", 301'000));
    ASSERT_EQ(got.size(), size_t{3});
    ASSERT(got[2].find("+1 similar suppressed") != std::string::npos);
    Alerter b([](const std::string&) { throw std::runtime_error("sink down"); });
    ASSERT(b.alert("k", "v", 0));                            // never reaches the caller
    auto missing = Alerter::grid_notify("/nonexistent/grid-notify");
    missing("nothing happens");
}

TEST(journal_is_append_only_jsonl_and_fails_loudly) {
    fs::path d = fresh_dir("journal_unit");
    {
        Journal j(d.string(), "x");
        j.write({{"ev", "a"}}, 1'793'000'000'000, true);
        j.write({{"ev", "b"}}, 1'793'000'000'500, false);
        j.flush_due(1'793'000'002'000);
        ASSERT_EQ(j.records(), uint64_t{2});
    }
    {
        Journal j(d.string(), "x");                          // a restart appends, never truncates
        j.write({{"ev", "c"}}, 1'793'000'003'000, true);
    }
    std::ifstream in(d / "x-2026-10-26.jsonl");
    std::string l;
    int n = 0;
    while (std::getline(in, l)) { auto o = json::parse(l); ASSERT(o.contains("t")); ++n; }
    ASSERT_EQ(n, 3);
    Journal bad(d.string(), "y");
    bad.open_path("/dev/full");
    ASSERT_THROWS(bad.write({{"ev", "x"}}, 1, true), JournalError);
    ASSERT(bad.failed());
    ASSERT_THROWS(bad.write({{"ev", "x"}}, 2, true), JournalError);   // latched
}

TEST(rest_api_reads_through_the_venue_with_auth) {
    ensure_client();
    ManualClock clock(t_start());
    fake::FakeExchange ex(kAcct, t_start());
    ex.set_next_nonce(kKey, 77);
    fake::FakeVenue v(ex, aave(), kKey, [&] { return clock.now_ms(); });
    int calls = 0;
    std::string token = signer().create_auth_token(t_start() / 1000 + 3600, t_start() / 1000, kKey, kAcct);
    RestApi api(v, kAcct, Specs{{kMkt, aave()}}, [&] { return token; }, RestGate{[] { return true; }, [&] { ++calls; }, {}});
    ASSERT_EQ(*api.next_nonce(kKey), int64_t{77});
    auto spec = api.order_book_details(kMkt);
    ASSERT(spec && spec->size_decimals == 3 && spec->min_base_amount == 45 && spec->min_quote_micro == 10 * kMicro);
    ex.place_foreign(kMkt, true, 82, 140'000, 5);
    auto open = api.active_orders(kMkt);
    ASSERT(open && open->size() == 1 && (*open)[0].client_index == 5 && (*open)[0].base == 82);
    RestApi noauth(v, kAcct, Specs{{kMkt, aave()}}, [] { return std::string("bad"); }, RestGate{});
    ASSERT(!noauth.active_orders(kMkt).has_value());           // 401 without a valid token
    ASSERT_EQ(noauth.last_status(), 401);
    ASSERT_EQ(calls, 3);
}

// ═════════════════════════════════ real TLS clients vs a local server ═════════════════════════════════

struct TestCert { std::string cert_pem, key_pem; };
static TestCert make_cert() {
    EVP_PKEY* pkey = EVP_EC_gen("P-256");
    X509* x = X509_new();
    X509_set_version(x, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(x), 1);
    X509_gmtime_adj(X509_getm_notBefore(x), -3600);
    X509_gmtime_adj(X509_getm_notAfter(x), 3600);
    X509_set_pubkey(x, pkey);
    X509_NAME* n = X509_get_subject_name(x);
    X509_NAME_add_entry_by_txt(n, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("localhost"), -1, -1, 0);
    X509_set_issuer_name(x, n);
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, x, x, nullptr, nullptr, 0);
    for (auto [nid, val] : {std::pair{NID_subject_alt_name, "DNS:localhost"}, std::pair{NID_basic_constraints, "critical,CA:TRUE"}}) {
        X509_EXTENSION* e = X509V3_EXT_conf_nid(nullptr, &ctx, nid, val);
        X509_add_ext(x, e, -1);
        X509_EXTENSION_free(e);
    }
    X509_sign(x, pkey, EVP_sha256());
    auto to_s = [](BIO* b) { char* p = nullptr; long len = BIO_get_mem_data(b, &p); std::string s(p, size_t(len)); BIO_free(b); return s; };
    BIO* cb = BIO_new(BIO_s_mem()); PEM_write_bio_X509(cb, x);
    BIO* kb = BIO_new(BIO_s_mem()); PEM_write_bio_PrivateKey(kb, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    TestCert c{to_s(cb), to_s(kb)};
    X509_free(x);
    EVP_PKEY_free(pkey);
    return c;
}

// Serves `conns` connections on 127.0.0.1, synchronously, on its own thread. http: /slow sleeps 800 ms; anything
// else echoes the request as {"code":200,"method","target","body","auth"}. ws: sends "connected", echoes frames.
struct LocalTlsServer {
    net_asio::io_context ioc;
    net_ssl::context ctx{net_ssl::context::tls_server};
    net_asio::ip::tcp::acceptor acc{ioc, {net_asio::ip::make_address("127.0.0.1"), 0}};
    std::thread th;
    // reqs: HTTP requests served per connection before the server closes it (1 = a server without keep-alive)
    LocalTlsServer(const TestCert& c, bool ws, int conns, int reqs = 1) {
        ctx.use_certificate_chain(net_asio::buffer(c.cert_pem));
        ctx.use_private_key(net_asio::buffer(c.key_pem), net_ssl::context::pem);
        th = std::thread([this, ws, conns, reqs] {
            for (int k = 0; k < conns; ++k) {
                try {
                    net_asio::ip::tcp::socket sock(ioc);
                    acc.accept(sock);
                    net_beast::ssl_stream<net_asio::ip::tcp::socket> s(std::move(sock), ctx);
                    s.handshake(net_ssl::stream_base::server);
                    if (ws) {
                        net_ws::stream<net_beast::ssl_stream<net_asio::ip::tcp::socket>&> w(s);
                        w.accept();
                        w.text(true);
                        w.write(net_asio::buffer(std::string(R"({"type":"connected"})")));
                        for (;;) {
                            net_beast::flat_buffer b;
                            w.read(b);
                            w.write(b.data());
                        }
                    }
                    net_beast::flat_buffer b;
                    for (int q = 0; q < reqs; ++q) {
                        net_http::request<net_http::string_body> req;
                        net_http::read(s, b, req);
                        if (req.target() == "/slow") std::this_thread::sleep_for(std::chrono::milliseconds(800));
                        json body = {{"code", 200}, {"method", std::string(req.method_string())}, {"target", std::string(req.target())},
                                     {"body", req.body()}, {"auth", std::string(req[net_http::field::authorization])},
                                     {"ctype", std::string(req[net_http::field::content_type])}, {"conn", k}, {"req", q}};
                        net_http::response<net_http::string_body> res{net_http::status::ok, 11};
                        res.keep_alive(q + 1 < reqs);
                        res.body() = body.dump();
                        res.prepare_payload();
                        net_http::write(s, res);
                    }
                    net_beast::error_code ec;
                    s.shutdown(ec);
                } catch (...) {}
            }
        });
    }
    std::string port() const { return std::to_string(acc.local_endpoint().port()); }
    ~LocalTlsServer() { th.join(); }
};

TEST(endpoint_guard_allows_only_mainnet_or_explicit_loopback) {
    ASSERT_THROWS(BeastHttps(Endpoint{"testnet.zklighter.elliot.ai", "443", "", "", false}), ConfigError);
    ASSERT_THROWS(BeastHttps(Endpoint{"mainnet.zklighter.elliot.ai", "8443", "", "", false}), ConfigError);
    ASSERT_THROWS(BeastHttps(Endpoint{"example.com", "443", "", "", false}), ConfigError);
    ASSERT_THROWS(BeastWs(Endpoint{"10.0.0.1", "443", "localhost", "pem", true}), ConfigError);   // loopback means 127.0.0.1
    ASSERT_THROWS(BeastWs(Endpoint{"127.0.0.1", "443", "localhost", "", true}), ConfigError);    // and its own CA
    ASSERT_THROWS(BeastHttps(Endpoint{"127.0.0.1", "443", "testnet.zklighter.elliot.ai", "pem", true}), ConfigError);
    BeastHttps ok(Endpoint{});                                // mainnet default: constructing opens nothing
    (void)ok;
    // the Binance reference feed: exactly fstream.binance.com:443, WebSocket only, never HTTP, never by host name alone
    Endpoint ref; ref.host = kReferenceFeedHost; ref.reference_feed = true;
    BeastWs ref_ok(ref, "/stream?streams=avaxusdt@bookTicker");
    (void)ref_ok;
    ASSERT_THROWS(BeastHttps(ref), ConfigError);                                                    // no HTTP to it
    Endpoint plain; plain.host = kReferenceFeedHost;
    ASSERT_THROWS(BeastWs(plain), ConfigError);                                                     // the flag is required
    for (const char* h : {"api.binance.com", "fapi.binance.com", "example.com"}) {
        Endpoint bad = ref; bad.host = h;
        ASSERT_THROWS(BeastWs(bad), ConfigError);
    }
    Endpoint port = ref; port.port = "9443";
    ASSERT_THROWS(BeastWs(port), ConfigError);
}

TEST(real_https_client_posts_forms_and_reports_timeouts_as_unknown) {
    TestCert c = make_cert();
    LocalTlsServer srv(c, false, 2);
    BeastHttps http(Endpoint{"127.0.0.1", srv.port(), "localhost", c.cert_pem, true});
    SignedTx t; t.tx_type = 14; t.tx_info = R"({"Nonce":3,"Sig":"x+/="})";
    HttpRequest r;
    r.method = "POST"; r.target = "/api/v1/sendTx"; r.body = Sender::form_send_tx(t);
    r.content_type = "application/x-www-form-urlencoded"; r.headers.push_back({"Authorization", "tok"}); r.timeout_ms = 3'000;
    HttpResponse resp = http.request(r);
    ASSERT_EQ(resp.status, 200);
    auto j = json::parse(resp.body);
    ASSERT(j["method"] == "POST" && j["auth"] == "tok" && j["ctype"] == "application/x-www-form-urlencoded");
    ASSERT(form_get(form_decode(j["body"].get<std::string>()), "tx_info") == t.tx_info);
    HttpRequest slow; slow.target = "/slow"; slow.timeout_ms = 300;
    HttpResponse s = http.request(slow);
    ASSERT_EQ(s.status, 0);
    ASSERT(s.sent);                                           // written, no answer → UNKNOWN for a sendTx
    ASSERT(Sender::classify(s, 1).kind == SendTxResult::Kind::Unknown);
    std::string free_port;
    { net_asio::io_context io; net_asio::ip::tcp::acceptor a(io, {net_asio::ip::make_address("127.0.0.1"), 0}); free_port = std::to_string(a.local_endpoint().port()); }
    BeastHttps nowhere(Endpoint{"127.0.0.1", free_port, "localhost", c.cert_pem, true});
    HttpResponse n = nowhere.request(r);
    ASSERT_EQ(n.status, 0);
    ASSERT(!n.sent);                                          // refused before writing → definitely not sent
    ASSERT(Sender::classify(n, 1).kind == SendTxResult::Kind::NotSent);
}

TEST(real_https_client_reuses_one_warm_connection_and_replaces_a_closed_one) {
    TestCert c = make_cert();
    {   // keep-alive server: three requests ride ONE TLS connection (the 2026-10-26 probe stall was a handshake per sendTx)
        LocalTlsServer srv(c, false, 1, 3);
        BeastHttps http(Endpoint{"127.0.0.1", srv.port(), "localhost", c.cert_pem, true});
        for (int q = 0; q < 3; ++q) {
            HttpRequest r; r.target = "/api/v1/x"; r.timeout_ms = 3'000;
            HttpResponse resp = http.request(r);
            ASSERT_EQ(resp.status, 200);
            ASSERT_EQ(json::parse(resp.body)["req"].get<int>(), q);
        }
        ASSERT_EQ(http.connects(), 1);
    }
    {   // a server that closes after each answer: the client sees it, reconnects BEFORE writing, and nothing is UNKNOWN
        LocalTlsServer srv(c, false, 3, 1);
        BeastHttps http(Endpoint{"127.0.0.1", srv.port(), "localhost", c.cert_pem, true});
        for (int q = 0; q < 3; ++q) {
            HttpRequest r; r.target = "/api/v1/x"; r.timeout_ms = 3'000;
            HttpResponse resp = http.request(r);
            ASSERT_EQ(resp.status, 200);
            ASSERT_EQ(json::parse(resp.body)["conn"].get<int>(), q);
        }
        ASSERT_EQ(http.connects(), 3);
    }
}

TEST(real_websocket_client_round_trip) {
    TestCert c = make_cert();
    LocalTlsServer srv(c, true, 1);
    BeastWs ws(Endpoint{"127.0.0.1", srv.port(), "localhost", c.cert_pem, true}, "/stream");
    ASSERT(ws.open());
    std::optional<std::string> m;
    for (int k = 0; k < 200 && !m; ++k) { m = ws.poll(); if (!m) std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    ASSERT(m && *m == R"({"type":"connected"})");
    ASSERT(ws.send(R"({"type":"ping"})"));
    m.reset();
    for (int k = 0; k < 200 && !m; ++k) { m = ws.poll(); if (!m) std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    ASSERT(m && *m == R"({"type":"ping"})");
    ws.close();
    ASSERT(!ws.is_open());
}

// ═════════════════════════════════ quoter parity + the offline soak ═════════════════════════════════

TEST(engine_quotes_exactly_where_the_backtest_simulator_would) {
    Rig r("parity");
    ASSERT(r.start());
    r.run(2'000);
    hl::Params p = mmpaper::frozen_params();
    p.order_usd = 11; p.max_inv_units = 1; p.venue_tick = 0.001; p.strat_tick_rule = hl::TickRule::Hyperliquid; p.hl_sz_decimals = 2;
    hl::Simulator sim(p);
    hl::Book b;
    b.t_ms = r.clock.now_ms();
    b.bids = {{135.0, 5}, {134.95, 9}};
    b.asks = {{135.1, 5}, {135.15, 9}};
    sim.on_book(b);
    for (const auto& o : r.ex.resting(kMkt))
        ASSERT_EQ(o.price, std::llround((o.is_ask ? sim.quote_px(false) : sim.quote_px(true)) * 1000));
}

TEST(offline_soak_of_one_recorded_aave_day_has_zero_invariant_violations) {
    if (!fs::exists(kArchive)) throw std::runtime_error(std::string("archive missing: ") + kArchive);
    // both pilot settings: gross $12 (the default — one side at a time) and $24 (two-sided)
    for (double gross : {12.0, 24.0}) {
        fs::path d = fresh_dir("soak" + std::to_string(int(gross)));
        json j = gw_json("offline");
        j["state_dir"] = (d / "state").string();
        j["risk"] = {{"max_gross_usd", gross}};
        j["engine"]["journal_dir"] = (d / "journal").string();
        j["engine"]["status_file"] = (d / "status.json").string();
        MmLiveConfig mc = build_mm_live_config(parse_config(j, Mode::Offline, 1'793'000'000));
        fs::create_directories(mc.engine.state_dir);
        ensure_client();
        RiskGovernor risk(mc.engine.risk, mc.engine.state_dir, RiskGovernor::Open::Init);
        Journal journal(mc.engine.journal_dir, mc.engine.label);
        std::vector<std::string> alerts;
        Alerter al([&](const std::string& s) { alerts.push_back(s); });
        OfflineOptions o;
        o.archive = kArchive;   // read-only
        OfflineResult res = run_offline_soak(mc, signer(), risk, journal, al, 1, o);
        std::cout << "    soak (gross $" << gross << "): " << res.summary.dump() << "\n";
        for (auto& v : res.violations) std::cout << "    violation: " << v << "\n";
        ASSERT(res.ok);                                      // end state AND every step (position, gross, size caps)
        ASSERT(res.summary["events"].get<int64_t>() > 200'000);
        ASSERT_EQ(res.summary["steps_checked"].get<int64_t>(), res.summary["events"].get<int64_t>());
        ASSERT(res.summary["sends"].get<int64_t>() > 1'000);
        ASSERT_EQ(res.summary["fills"].get<int64_t>(), res.summary["journal_fills"].get<int64_t>());
        // a filled position is closed again (the $12 cap does not trap it): flat after fills at least once
        ASSERT(res.summary["fills"].get<int64_t>() >= 2);
        ASSERT(res.summary["returns_to_flat"].get<int64_t>() >= 1);
    }
}

// ═════════════════════════════════ the binary ═════════════════════════════════

static std::string mm_live_bin() {
    return (fs::read_symlink("/proc/self/exe").parent_path() / "mm_live").string();
}
// Runs mm_live with args (no shell), stdout+stderr to `out`. Returns the pid (wait with wait_exit).
static pid_t spawn_mm_live(const std::vector<std::string>& args, const fs::path& out) {
    pid_t pid = ::fork();
    if (pid == 0) {
        int fd = ::open(out.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        ::dup2(fd, 1); ::dup2(fd, 2);
        std::vector<char*> av;
        std::string bin = mm_live_bin();
        av.push_back(bin.data());
        std::vector<std::string> copy = args;
        for (auto& a : copy) av.push_back(a.data());
        av.push_back(nullptr);
        ::execv(bin.c_str(), av.data());
        ::_exit(127);
    }
    return pid;
}
static int wait_exit(pid_t pid) {
    int st = 0;
    ::waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
}
static std::string slurp(const fs::path& p) { std::ifstream f(p); return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }

TEST(binary_refuses_bad_invocations_and_stops_cleanly_on_sigterm) {
    if (!fs::exists(mm_live_bin())) throw std::runtime_error("mm_live binary not built next to the test");
    if (!fs::exists(kArchive)) throw std::runtime_error(std::string("archive missing: ") + kArchive);
    fs::path d = fresh_dir("binary");
    json j = gw_json("offline");
    j["state_dir"] = (d / "state").string();
    j["risk"] = {{"max_gross_usd", 24}};
    j["engine"]["journal_dir"] = (d / "journal").string();
    std::ofstream(d / "offline.json") << j.dump(1);
    std::string cfg = (d / "offline.json").string();
    ASSERT_EQ(wait_exit(spawn_mm_live({"--help"}, d / "o1")), 0);
    ASSERT(slurp(d / "o1").find("usage: mm_live") != std::string::npos);
    ASSERT_EQ(wait_exit(spawn_mm_live({"--config", cfg, "--mode", "offline", "--archive", kArchive, "--bogus"}, d / "o2")), 2);
    ASSERT_EQ(wait_exit(spawn_mm_live({"--config", cfg, "--mode", "live"}, d / "o3")), 2);      // config says offline
    ASSERT(slurp(d / "o3").find("config refused") != std::string::npos);
    ASSERT_EQ(wait_exit(spawn_mm_live({"--config", cfg, "--mode", "offline"}, d / "o4")), 2);   // no archive
    ASSERT_EQ(wait_exit(spawn_mm_live({"--config", cfg, "--mode", "offline", "--check-config"}, d / "o5")), 0);
    ASSERT(slurp(d / "o5").find("no socket opened") != std::string::npos);
    // a running soak holds the instance lock; SIGTERM stops it cleanly with every invariant checked
    pid_t a = spawn_mm_live({"--config", cfg, "--mode", "offline", "--archive", kArchive}, d / "o6");
    std::this_thread::sleep_for(std::chrono::seconds(3));
    ASSERT_EQ(wait_exit(spawn_mm_live({"--config", cfg, "--mode", "offline", "--archive", kArchive}, d / "o7")), 2);
    ASSERT(slurp(d / "o7").find("instance lock REFUSED") != std::string::npos);
    ::kill(a, SIGTERM);
    int rc = wait_exit(a);
    std::string out = slurp(d / "o6");
    if (rc != 0) std::cout << out.substr(out.size() > 3000 ? out.size() - 3000 : 0) << "\n";
    ASSERT_EQ(rc, 0);
    ASSERT(out.find("stopping the soak early") != std::string::npos);
    ASSERT(out.find("offline soak PASSED") != std::string::npos);
}

TEST(deadman_switches_to_abort_first_when_the_exchange_refuses_a_second_schedule) {
    Rig r("dm_21712");
    r.ex.set_schedule_mode(fake::FakeExchange::ScheduleMode::RejectWhileQueued);   // the 21712 reading of the spec
    ASSERT(r.start());
    int64_t exposed = 0;
    for (int k = 0; k < 900; ++k) {
        r.step(100);
        if (r.ex.scheduled_cancel_alls() == 0 && r.eng->quoting_allowed(r.clock.now_ms())) exposed += 100;
    }
    ASSERT_EQ(exposed, int64_t{0});
    ASSERT_EQ(r.ex.scheduled_cancel_alls(), size_t{1});
    ASSERT(r.eng->deadman_deadline() > r.clock.now_ms() + 60'000);   // extended by abort + schedule
    ASSERT(r.journal_count("deadman_mode") == 1);
    ASSERT_EQ(r.ex.open_orders(), 2);
}

TEST(deadman_health_needs_a_confirmed_deadline_60s_ahead) {
    Rig r("dm_age");
    ASSERT(r.start());
    r.run(2'000);
    ASSERT_EQ(r.ex.open_orders(), 2);
    int64_t first = r.eng->deadman_deadline();
    ASSERT(first > r.clock.now_ms() + 5 * 60'000 - 10'000);
    r.venue.suppress_tx_stream = true;                       // no sequencer result: no re-arm can be CONFIRMED
    r.run(270'000, 500);                                     // the first deadline ages past now + 60 s
    ASSERT_EQ(r.eng->deadman_deadline(), first);              // never taken from a resync or an abort
    ASSERT(!r.eng->quoting_allowed(r.clock.now_ms()));
    ASSERT(r.eng->halted("deadman"));
    ASSERT_EQ(r.ex.open_orders(), 0);                        // quotes pulled
    int n = 0;
    for (auto& a : r.alerts) n += a.find("HALT deadman") != std::string::npos;
    ASSERT_EQ(n, 1);                                         // one alert for the episode
    r.venue.suppress_tx_stream = false;
    r.run(30'000);
    ASSERT(!r.eng->halted("deadman"));
    ASSERT_EQ(r.ex.open_orders(), 2);
}

TEST(shutdown_leaves_the_deadman_armed_when_the_cancel_all_is_not_confirmed) {
    Rig r("dm_shutdown");
    ASSERT(r.start());
    r.run(2'000);
    r.venue.fail_next_rest = 1'000;                          // REST cannot show zero open orders
    ASSERT(!r.eng->shutdown("SIGTERM"));                     // unclean → the binary exits non-zero
    ASSERT_EQ(r.ex.scheduled_cancel_alls(), size_t{1});       // never aborted
    ASSERT(!r.eng->deadman_aborted());
    ASSERT(r.alerted("LEFT ARMED"));
    r.ex.set_now(r.clock.now_ms() + 6 * 60'000);
    ASSERT_EQ(r.ex.open_orders(), 0);
}

TEST(kill_flattens_even_when_disarmed) {
    Rig r("kill_disarmed");
    ASSERT(r.start());
    r.run(2'000);
    r.ex.hit(r.resting(false), 82);
    r.run(500);
    r.armed = false;                                         // arming expired: no new exposure …
    std::ofstream(r.cfg.state_dir + "/KILL") << "x";
    r.run(15'000);
    ASSERT_EQ(r.ex.position(kMkt), int64_t{0});              // … but the reduce-only flatten still goes
    ASSERT_EQ(r.ex.open_orders(), 0);
}

TEST(backfill_runs_from_the_rest_cursor_not_the_stream_high_water_mark) {
    Rig r("rest_cursor");
    ASSERT(r.start());
    r.run(2'000);
    int64_t bid = r.resting(false), ask = r.resting(true);
    r.venue.drop_next_stream_fills = 1;                      // the stream silently loses fill A …
    r.ex.hit(bid, 30);
    r.step(50);
    r.ex.hit(ask, 30);                                       // … then delivers the LATER fill B
    r.step(50);
    ASSERT_EQ(r.eng->position(), int64_t{-30});
    r.run(20'000);
    ASSERT_EQ(r.eng->fills(), int64_t{2});                   // A recovered by REST below B's trade id
    ASSERT_EQ(r.eng->position(), r.ex.position(kMkt));
    ASSERT_EQ(r.eng->mismatch_acts(), int64_t{0});           // found as a fill, not papered over by adoption
}

TEST(loss_stop_runs_on_rest_total_asset_value) {
    Rig r("rest_equity");
    r.venue.suppress_stats = true;                           // the stream never reports equity
    ASSERT(r.start());
    r.run(2'000);
    r.venue.collateral_micro -= 2 * kMicro;                  // −$2 ≤ the −$1.50 daily stop
    r.run(15'000);
    ASSERT(r.eng->halted("risk_stop"));
    ASSERT_EQ(r.ex.open_orders(), 0);
}

TEST(rate_window_is_persisted_for_a_restart) {
    Rig r("rate_persist");
    ASSERT(r.start());
    r.run(5'000);
    auto j = json::parse(std::ifstream(r.cfg.state_dir + "/rate_window.json"));
    ASSERT(j["all"].size() > 0 && j["send"].size() > 0);
    RateLimiter fresh(54, 24);
    fresh.preload(j["all"].get<std::vector<int64_t>>(), j["blocked_until"].get<int64_t>(), r.clock.now_ms());
    ASSERT_EQ(fresh.used(r.clock.now_ms()), int(j["all"].size()));
    ASSERT(fresh.used(r.clock.now_ms() + 61'000) == 0);
    ASSERT(r.eng->sends() <= 30);                            // the sendTx budget (default 30/min)
}

TEST(gross_cap_is_worst_case_exposure_so_a_closing_order_is_allowed) {
    Rig r("close_at_12");
    r.cfg.risk.max_gross_micro = 12 * kMicro;               // the pilot default
    ASSERT(r.start());
    r.run(2'000);
    ASSERT_EQ(r.ex.open_orders(), 2);                       // bid and ask cannot both add to one position: worst case $11
    ASSERT(r.ex.hit(r.resting(false), 82));
    r.run(3'000);
    ASSERT_EQ(r.eng->position(), int64_t{82});
    int64_t ask = r.resting(true);
    ASSERT(ask != 0);                                       // the closing ask stays (was refused as "$22 gross" before)
    ASSERT_EQ(r.resting(false), int64_t{0});                // the bid would raise the worst case: refused
    ASSERT(r.ex.hit(ask, 82));
    r.run(3'000);
    ASSERT_EQ(r.eng->position(), int64_t{0});               // back to flat
    ASSERT_EQ(r.ex.position(kMkt), int64_t{0});
    ASSERT_EQ(r.ex.open_orders(), 2);                       // quoting both sides again
}

TEST(cancel_all_rule_error_that_does_not_consume_the_nonce_resyncs_without_a_sticky_halt) {
    Rig r("ca_not_consumed");
    r.ex.set_cancel_all_errors_consume(false);              // 21714 leaves the nonce unconsumed on this exchange
    ASSERT(r.start());
    r.run(2'000);
    ASSERT_EQ(r.ex.open_orders(), 2);
    r.ex.set_min_cancel_all_ms(20 * 60'000);                // every re-arm now fails with 21714
    r.run(70'000);
    ASSERT(!r.eng->halted("nonce"));                        // no "nextNonce went backwards"
    ASSERT(r.eng->deadman_failures() >= 1);
    r.ex.set_min_cancel_all_ms(5 * 60'000);
    r.run(30'000);
    ASSERT(!r.eng->nonces().frozen());
    ASSERT_EQ(r.eng->nonces().peek_next(), r.ex.next_nonce(kKey));
    ASSERT(r.eng->shutdown("SIGTERM"));                     // and the cancel-all still goes, cleanly
    ASSERT_EQ(r.ex.open_orders(), 0);
}

TEST(startup_leaves_a_scheduled_cancel_all_when_orders_will_not_go) {
    Rig r("start_stuck");
    r.venue.phantom_open_order = true;                      // REST keeps showing an open order
    ASSERT(!r.start());
    ASSERT_EQ(r.ex.scheduled_cancel_alls(), size_t{1});     // armed before giving up
    ASSERT(r.ex.scheduled_cancel_all_time() >= r.clock.now_ms() + 5 * 60'000 - 30'000);
    ASSERT(r.alerted("scheduled cancel-all was sent"));
}

// From the independent review: every SCHEDULE fails in the sequencer while every ABORT succeeds. Quoting must never
// continue with resting quotes and no dead-man scheduled.
TEST(REVIEW_deadman_schedule_rejected_but_abort_ok) {
    Rig r("rv_dm");
    ASSERT(r.start());
    r.run(2'000);
    ASSERT_EQ(r.ex.open_orders(), 2);
    r.ex.set_min_cancel_all_ms(20 * 60'000);   // every SCHEDULE now fails in the sequencer; ABORT still succeeds
    int64_t exposed_ms = 0;
    for (int k = 0; k < 1200; ++k) { r.step(100); if (r.ex.scheduled_cancel_alls() == 0 && r.ex.open_orders() > 0) exposed_ms += 100; }
    std::cout << "      ms with resting quotes and NO dead-man scheduled: " << exposed_ms << " of 120000\n";
    std::cout << "      failures=" << r.eng->deadman_failures() << " scheduled=" << r.ex.scheduled_cancel_alls()
              << " open=" << r.ex.open_orders() << " block='" << r.eng->quoting_block(r.clock.now_ms()) << "'\n";
    ASSERT(!(r.ex.scheduled_cancel_alls() == 0 && r.eng->quoting_allowed(r.clock.now_ms())));
    ASSERT_EQ(exposed_ms, int64_t{0});                       // the old schedule stays armed until a new one is confirmed
    ASSERT(r.ex.scheduled_cancel_alls() >= 1);
    // the confirmed deadline ages: quoting stops (quotes cancelled) before it gets within 60 s of firing
    ASSERT(!r.eng->quoting_allowed(r.clock.now_ms()) || r.eng->deadman_deadline() > r.clock.now_ms() + 60'000);
    int dm_alerts = 0;
    for (auto& a : r.alerts) dm_alerts += a.find("dead-man") != std::string::npos;
    ASSERT(dm_alerts <= 3);                                  // one alert per episode, not a loop
}

int main(int, char** argv) {
    reexec_with_godebug_if_needed(argv);   // Go reads the exec-time environment, not setenv()
    std::cout << "=== mm_live (offline) ===\n";
    RUN(feed_subscribes_after_connected_with_auth_only_where_required);
    RUN(feed_auth_token_renewed_every_6h_deadline_within_8h);
    RUN(feed_keepalive_ping_pong_shutdown_silence_and_error_frames);
    RUN(parsers_read_lighters_shapes);
    RUN(sender_forms_and_classification);
    RUN(nonce_accepted_pending_then_rejected_before_apply);
    RUN(nonce_accepted_pending_survives_resync_and_settles_fifo);
    RUN(engine_accepted_then_rejected_before_apply_recovers);
    RUN(startup_cancels_everything_and_adopts_the_exchange_position);
    RUN(startup_refuses_capital_guard_and_metadata_mismatch);
    RUN(quotes_post_only_both_sides_at_the_strategy_prices);
    RUN(fill_moves_the_position_at_once_and_caps_that_side);
    RUN(closing_quote_is_sized_to_the_position_not_left_as_dust);
    RUN(day_summary_counts_fills_of_earlier_runs_today);
    RUN(fill_after_cancel_confirm_takes_the_live_path);
    RUN(account_stream_drop_cancels_all_then_backfills_and_reconciles);
    RUN(duplicate_fill_from_stream_and_rest_counts_once);
    RUN(unknown_exchange_order_from_an_earlier_run_is_cancelled);
    RUN(foreign_order_halts_quoting_account_used_by_something_else);
    RUN(position_mismatch_halts_alerts_and_adopts_the_exchange);
    RUN(leverage_mismatch_refuses_to_quote_until_fixed_in_the_app);
    RUN(journal_failure_halts_and_cancels_all);
    RUN(deadman_is_rearmed_every_20s_with_the_5_minute_window);
    RUN(deadman_cancels_everything_when_the_process_stops);
    RUN(REVIEW_deadman_schedule_rejected_but_abort_ok);
    RUN(gross_cap_is_worst_case_exposure_so_a_closing_order_is_allowed);
    RUN(cancel_all_rule_error_that_does_not_consume_the_nonce_resyncs_without_a_sticky_halt);
    RUN(startup_leaves_a_scheduled_cancel_all_when_orders_will_not_go);
    RUN(deadman_switches_to_abort_first_when_the_exchange_refuses_a_second_schedule);
    RUN(deadman_health_needs_a_confirmed_deadline_60s_ahead);
    RUN(shutdown_leaves_the_deadman_armed_when_the_cancel_all_is_not_confirmed);
    RUN(kill_flattens_even_when_disarmed);
    RUN(backfill_runs_from_the_rest_cursor_not_the_stream_high_water_mark);
    RUN(loss_stop_runs_on_rest_total_asset_value);
    RUN(rate_window_is_persisted_for_a_restart);
    RUN(http_429_backs_off_and_alerts);
    RUN(timeout_is_unknown_never_failed_and_reconcile_resolves_it);
    RUN(timeout_before_the_exchange_resends_identical_bytes);
    RUN(sigterm_path_cancels_all_and_aborts_the_deadman);
    RUN(cancel_of_a_filled_order_rejected_before_apply_does_not_loop_or_block_the_flatten);
    RUN(cancel_lost_to_a_known_fill_resyncs_quietly);
    RUN(periodic_reconcile_uses_a_short_timeout_and_falls_back);
    RUN(public_stream_sends_its_own_ping_every_minute);
    RUN(kill_file_halts_flattens_and_never_auto_resumes);
    RUN(guardian_start_with_kill_file_flattens_and_never_quotes);
    RUN(pause_file_pulls_quotes_and_keeps_the_position);
    RUN(disarming_stops_new_orders_but_not_cancels);
    RUN(book_gap_pulls_the_markets_quotes_before_resubscribing);
    RUN(public_feed_silence_cancels_all);
    RUN(single_instance_lock);
    RUN(probe_runs_end_to_end_against_the_fake);
    RUN(mm_live_config_is_strict);
    RUN(mm_live_strategy_overrides_are_bounded_and_reach_the_quoter);
    RUN(bn_lead_matches_the_lab_rule_threshold_hold_warmup_and_staleness);
    RUN(bn_pull_config_is_bounded_and_both_knobs_go_together);
    RUN(binance_drop_pulls_the_live_bid_keeps_the_ask_and_requotes_after_the_hold);
    RUN(no_bn_pull_config_means_no_reference_stream_and_unchanged_quotes);
    RUN(slow_book_stream_reopen_is_not_a_stall_but_a_slow_reference_open_is);
    RUN(alerts_are_rate_limited_and_fail_open);
    RUN(journal_is_append_only_jsonl_and_fails_loudly);
    RUN(rest_api_reads_through_the_venue_with_auth);
    RUN(endpoint_guard_allows_only_mainnet_or_explicit_loopback);
    RUN(real_https_client_posts_forms_and_reports_timeouts_as_unknown);
    RUN(real_websocket_client_round_trip);
    RUN(engine_quotes_exactly_where_the_backtest_simulator_would);
    RUN(binary_refuses_bad_invocations_and_stops_cleanly_on_sigterm);
    if (!std::getenv("MM_LIVE_SKIP_SOAK")) RUN(offline_soak_of_one_recorded_aave_day_has_zero_invariant_violations);
    fs::remove_all(scratch());
    std::cout << tests_run - tests_failed << "/" << tests_run << " passed\n";
    return tests_failed ? 1 : 0;
}
