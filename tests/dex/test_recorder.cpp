/*  test_recorder.cpp — the recording side of the DEX archive (2026-10-26): Lighter market_stats parsing
    (lighter_feed.hpp), the shared loader skipping those lines while replaying books/trades identically and handing
    them out separately (hl_events.hpp), the daily orderBookDetails snapshot + diff (lighter_meta.hpp), the recorder
    health file (recorder_status.hpp) and the zstd writer's time-boxed frames (zstd_daily.hpp). No network.

    Optional: RECORDER_TEST_ARCHIVE_DAY=<a real <SYM>/<YYYY-MM-DD>.jsonl.zst> also re-writes that day with a
    market_stats line after every 50th line and checks the loader's events are identical to the original's.     */
#include "dex/hl_events.hpp"
#include "dex/lighter_meta.hpp"
#include "dex/recorder_status.hpp"
#include "zstd_daily.hpp"

#include <unistd.h>

#include <cmath>
#include <cstdlib>
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
#define ASSERT_NEAR(a, b, e) do { if (std::abs((a) - (b)) > (e)) throw std::runtime_error("ASSERT_NEAR failed: " #a " vs " #b); } while (0)

using json = nlohmann::json;
namespace fs = std::filesystem;

static fs::path tmpdir(const std::string& tag) {
    fs::path d = fs::temp_directory_path() / ("test_recorder_" + tag + "_" + std::to_string(::getpid()));
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

// Captured from the live mainnet stream 2026-10-26 (market_stats/25, public).
static const char* STATS_SNAPSHOT = R"({"channel":"market_stats:25","market_stats":{"symbol":"BNB","market_id":25,"index_price":"742.0016","mark_price":"741.7927","mid_price":"741.7800","best_ask_price":"741.8413","best_bid_price":"741.7186","open_interest":"4234301.090140","open_interest_limit":"72057594037927936.000000","open_interest_usd":"8468602.180280","funding_clamp_small":"0.0500","funding_clamp_big":"4.0000","last_trade_price":"741.7186","current_funding_rate":"0.0012","funding_rate":"0.0012","funding_timestamp":1793012400000,"daily_base_token_volume":3168.09,"daily_quote_token_volume":2339408.966088,"daily_price_low":732.0199,"daily_price_high":743.8772,"daily_price_change":0.35126798366116135,"base_interest_rate":"0.0100","premium":"-0.0217"},"timestamp":1793014968396,"type":"subscribed/market_stats"})";
static const char* STATS_UPDATE = R"({"channel":"market_stats:25","market_stats":{"symbol":"BNB","market_id":25,"index_price":"742.0118","mark_price":"741.8028","mid_price":"741.7800","best_ask_price":"741.8413","best_bid_price":"741.7186","open_interest":"4234358.742960","open_interest_usd":"8468717.485920","last_trade_price":"741.7186","current_funding_rate":"0.0012","funding_rate":"0.0012","funding_timestamp":1793012400000,"premium":"-0.0217"},"timestamp":1793014968639,"type":"update/market_stats"})";

TEST(market_stats_parse_live_shapes) {
    const auto snap = lighter::parse_market_stats(json::parse(STATS_SNAPSHOT));
    ASSERT_EQ(snap.size(), size_t{1});
    const auto& s = snap[0];
    ASSERT_EQ(s.market_id, 25);
    ASSERT(s.snapshot);
    ASSERT_EQ(s.t_ms, int64_t{1793014968396});
    ASSERT_NEAR(s.mark_price, 741.7927, 1e-9);
    ASSERT_NEAR(s.index_price, 742.0016, 1e-9);
    ASSERT_NEAR(s.last_trade_price, 741.7186, 1e-9);
    ASSERT_NEAR(s.funding_rate, 0.0012, 1e-12);
    ASSERT_NEAR(s.current_funding_rate, 0.0012, 1e-12);
    ASSERT_NEAR(s.open_interest, 4234301.09014, 1e-6);
    ASSERT_NEAR(s.premium, -0.0217, 1e-12);
    ASSERT_EQ(s.funding_timestamp_ms, int64_t{1793012400000});
    const auto upd = lighter::parse_market_stats(json::parse(STATS_UPDATE));
    ASSERT_EQ(upd.size(), size_t{1});
    ASSERT(!upd[0].snapshot);
    ASSERT_NEAR(upd[0].mark_price, 741.8028, 1e-9);
    ASSERT(lighter::is_market_stats(json::parse(STATS_UPDATE)));
    // "market_stats/all" nests one object per market id
    json all = json::parse(STATS_UPDATE);
    all["channel"] = "market_stats:all";
    json inner = all["market_stats"];
    json other = inner; other["market_id"] = 26; other["mark_price"] = "1.5";
    all["market_stats"] = {{"25", inner}, {"26", other}};
    const auto both = lighter::parse_market_stats(all);
    ASSERT_EQ(both.size(), size_t{2});
    // not stats: book / trade / error frames
    ASSERT(lighter::parse_market_stats(json::parse(R"({"channel":"trade:25","type":"update/trade","trades":[]})")).empty());
    ASSERT(lighter::parse_market_stats(json::parse(R"({"error":{"code":30005,"message":"Invalid Channel"}})")).empty());
    ASSERT(!lighter::is_market_stats(json::parse(R"({"channel":"order_book:25"})")));
}

// A synthetic Lighter day: snapshot, deltas, trades.
static std::vector<std::string> synthetic_day(int n) {
    std::vector<std::string> out;
    auto line = [](int64_t r, const std::string& m) { return "{\"r\":" + std::to_string(r) + ",\"m\":" + m + "}"; };
    const int64_t r0 = 1793014968000000LL;
    out.push_back(line(r0, R"({"channel":"order_book:25","type":"subscribed/order_book","timestamp":1793014968000,"order_book":{"code":0,"nonce":10,"asks":[{"price":"101.0","size":"2.0"},{"price":"102.0","size":"5.0"}],"bids":[{"price":"100.0","size":"3.0"},{"price":"99.0","size":"4.0"}]}})"));
    for (int i = 0; i < n; ++i) {
        const int64_t t = 1793014968001 + i * 7;
        const std::string sz = std::to_string(1 + i % 5) + ".0";
        out.push_back(line(r0 + (i + 1) * 7000, R"({"channel":"order_book:25","type":"update/order_book","timestamp":)" + std::to_string(t) +
                      R"(,"order_book":{"code":0,"begin_nonce":)" + std::to_string(10 + i) + R"(,"nonce":)" + std::to_string(11 + i) +
                      R"(,"asks":[{"price":"101.0","size":")" + sz + R"("}],"bids":[]}})"));
        if (i % 3 == 0)
            out.push_back(line(r0 + (i + 1) * 7000 + 1, R"({"channel":"trade:25","type":"update/trade","trades":[{"price":"101","size":"0.5","timestamp":)" +
                          std::to_string(t) + R"(,"is_maker_ask":)" + (i % 2 ? "true" : "false") + R"(,"type":"trade"}]})"));
    }
    return out;
}

static void write_day(const fs::path& dir, const std::vector<std::string>& lines, int frame_s, const std::string& stats_every = "") {
    ZstdDaily z(dir, {}, 3);
    z.set_frame_seconds(frame_s);
    int k = 0;
    for (const auto& l : lines) {
        const int64_t r = json::parse(l)["r"].get<int64_t>();
        z.write_line(l, r);
        if (!stats_every.empty() && ++k % 4 == 0) z.write_line("{\"r\":" + std::to_string(r) + ",\"m\":" + STATS_UPDATE + "}", r);
    }
}

static bool same_events(const std::vector<hl::Event>& a, const std::vector<hl::Event>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const auto &x = a[i], &y = b[i];
        if (x.t_ms != y.t_ms || x.is_book != y.is_book) return false;
        if (x.is_book) {
            if (x.book.t_ms != y.book.t_ms || x.book.bids.size() != y.book.bids.size() || x.book.asks.size() != y.book.asks.size()) return false;
            for (size_t j = 0; j < x.book.bids.size(); ++j) if (x.book.bids[j].px != y.book.bids[j].px || x.book.bids[j].sz != y.book.bids[j].sz) return false;
            for (size_t j = 0; j < x.book.asks.size(); ++j) if (x.book.asks[j].px != y.book.asks[j].px || x.book.asks[j].sz != y.book.asks[j].sz) return false;
        } else if (x.trade.t_ms != y.trade.t_ms || x.trade.px != y.trade.px || x.trade.sz != y.trade.sz || x.trade.buy_aggr != y.trade.buy_aggr) return false;
    }
    return true;
}

TEST(loader_skips_stats_and_replays_identically) {
    const auto d = tmpdir("loader");
    const auto lines = synthetic_day(200);
    write_day(d / "old" / "BNB", lines, 0);
    write_day(d / "new" / "BNB", lines, 60, "stats");
    const auto fo = d / "old" / "BNB" / "2026-10-26.jsonl.zst", fn = d / "new" / "BNB" / "2026-10-26.jsonl.zst";
    ASSERT(fs::exists(fo) && fs::exists(fn));
    int64_t bad_o = 0, bad_n = 0;
    std::vector<lighter::MarketStats> st_o, st_n;
    const auto eo = hl::load_archive(fo, &bad_o, &st_o);
    const auto en = hl::load_archive(fn, &bad_n, &st_n);
    ASSERT(!eo.empty());
    ASSERT(same_events(eo, en));
    ASSERT_EQ(bad_o, int64_t{0});
    ASSERT_EQ(bad_n, int64_t{0});                // stats lines are not "bad"
    ASSERT(st_o.empty());                       // an old archive simply has none
    ASSERT_EQ(st_n.size(), lines.size() / 4);
    ASSERT_EQ(st_n[0].market_id, 25);
    ASSERT(st_n[0].recv_us > 0);
    // without the stats pointer the result is the same too, and load_market_stats finds the same lines
    ASSERT(same_events(eo, hl::load_archive(fn)));
    ASSERT_EQ(hl::load_market_stats(fn).size(), st_n.size());
    // list_days ignores the _meta directory beside the markets
    fs::create_directories(d / "new" / "_meta");
    std::ofstream(d / "new" / "_meta" / "2026-10-26.json") << "{}";
    const auto days = hl::list_days(d / "new");
    ASSERT_EQ(days.size(), size_t{1});
    ASSERT_EQ(days[0].coin, std::string("BNB"));
    fs::remove_all(d);
}

TEST(real_archive_day_replays_identically_with_stats_injected) {
    const char* p = std::getenv("RECORDER_TEST_ARCHIVE_DAY");
    if (!p || !*p) { std::cout << "  (skipped: RECORDER_TEST_ARCHIVE_DAY not set)\n"; return; }
    const fs::path src(p);
    // RECORDER_TEST_KEEP_DIR=<dir>: keep the injected copy there (<dir>/<SYM>/<day>.jsonl.zst) for an mm_sim comparison
    const char* keep = std::getenv("RECORDER_TEST_KEEP_DIR");
    const fs::path d = keep && *keep ? fs::path(keep) : tmpdir("real");
    const fs::path dst = d / src.parent_path().filename() / src.filename();
    fs::remove(dst);                                       // ZstdDaily appends
    {
        ZstdLines in(src);
        ASSERT(in.ok());
        ZstdDaily z(dst.parent_path(), {}, 3);
        z.set_frame_seconds(60);
        std::string l;
        int64_t k = 0, last_r = 0;
        while (in.next(l)) {
            const auto j = json::parse(l, nullptr, false);
            const int64_t r = j.is_object() ? j.value("r", last_r) : last_r;
            last_r = r;
            z.write_line(l, r);
            if (++k % 50 == 0) z.write_line("{\"r\":" + std::to_string(r) + ",\"m\":" + STATS_UPDATE + "}", r);
        }
    }
    int64_t b1 = 0, b2 = 0;
    std::vector<lighter::MarketStats> st;
    const auto a = hl::load_archive(src, &b1), b = hl::load_archive(dst, &b2, &st);
    std::cout << "  " << src << ": " << a.size() << " events, " << st.size() << " stats lines injected\n";
    ASSERT(same_events(a, b));
    ASSERT_EQ(b1, b2);
    ASSERT(!st.empty());
    if (!(keep && *keep)) fs::remove_all(d);
}

static json obd(const std::string& bnb_price_dec, const std::string& bnb_min_base, bool with_gmx = true) {
    json j = {{"code", 200}, {"order_book_details", json::array()}};
    j["order_book_details"].push_back({{"symbol", "BNB"}, {"market_id", 25}, {"status", "active"}, {"price_decimals", std::stoi(bnb_price_dec)},
                                       {"size_decimals", 2}, {"supported_price_decimals", std::stoi(bnb_price_dec)}, {"supported_size_decimals", 2},
                                       {"min_base_amount", bnb_min_base}, {"min_quote_amount", "10.000000"}, {"maker_fee", "0.0000"}, {"taker_fee", "0.0000"},
                                       {"mark_price", "741.79"}});
    if (with_gmx)
        j["order_book_details"].push_back({{"symbol", "GMX"}, {"market_id", 61}, {"status", "active"}, {"price_decimals", 4}, {"size_decimals", 2},
                                           {"min_base_amount", "1.00"}, {"min_quote_amount", "10.000000"}});
    return j;
}

TEST(meta_diff_reports_decimals_minimums_and_listing) {
    // a price that moves is NOT a change; decimals and minimums are
    json a = obd("4", "0.01"), b = obd("4", "0.01");
    b["order_book_details"][0]["mark_price"] = "750.00";
    ASSERT(lighter::meta_diff(a, b).empty());
    const auto c = lighter::meta_diff(a, obd("3", "0.05"));
    ASSERT_EQ(c.size(), size_t{3});             // price_decimals, supported_price_decimals, min_base_amount
    ASSERT(c[0].find("BNB(25)") == 0);
    bool saw = false;
    for (const auto& s : c) if (s == "BNB(25) min_base_amount 0.01 → 0.05") saw = true;
    ASSERT(saw);
    // only the recorded markets
    ASSERT(lighter::meta_diff(a, obd("3", "0.05"), {61}).empty());
    // a market that disappears / appears
    const auto gone = lighter::meta_diff(a, obd("4", "0.01", false));
    ASSERT_EQ(gone.size(), size_t{1});
    ASSERT(gone[0] == "GMX(61) no longer listed");
    ASSERT(lighter::meta_diff(obd("4", "0.01", false), a)[0].find("new market") != std::string::npos);
}

TEST(meta_snapshot_files_and_diff_against_the_newest) {
    const auto d = tmpdir("meta") / "_meta";
    auto r1 = lighter::write_meta_snapshot(d, "2026-10-25", obd("4", "0.01").dump());
    ASSERT(r1.ok);
    ASSERT(r1.previous.empty());
    ASSERT(r1.changes.empty());
    ASSERT_EQ(r1.markets, 2);
    ASSERT(fs::exists(d / "2026-10-25.json"));
    auto r2 = lighter::write_meta_snapshot(d, "2026-10-26", obd("4", "0.02").dump(), {25});
    ASSERT(r2.ok);
    ASSERT_EQ(r2.previous.filename().string(), std::string("2026-10-25.json"));
    ASSERT_EQ(r2.changes.size(), size_t{1});
    // a restart on the same day compares with that day's own earlier snapshot, then replaces it
    auto r3 = lighter::write_meta_snapshot(d, "2026-10-26", obd("4", "0.02").dump());
    ASSERT(r3.ok);
    ASSERT_EQ(r3.previous.filename().string(), std::string("2026-10-26.json"));
    ASSERT(r3.changes.empty());
    // an invalid reply is never written
    auto r4 = lighter::write_meta_snapshot(d, "2026-10-27", R"({"code":500})");
    ASSERT(!r4.ok);
    ASSERT(!fs::exists(d / "2026-10-27.json"));
    ASSERT_EQ(lighter::latest_meta(d).filename().string(), std::string("2026-10-26.json"));
    // written byte-for-byte as received (+ newline)
    std::ifstream in(d / "2026-10-26.json");
    std::stringstream ss; ss << in.rdbuf();
    ASSERT_EQ(ss.str(), obd("4", "0.02").dump() + "\n");
    fs::remove_all(d.parent_path());
}

TEST(status_file_busiest_market_and_ages) {
    rec::Status s("lighter", 1'000'000'000);
    s.add_market("BNB"); s.add_market("GMX"); s.add_market("AAVE");
    ASSERT(s.busiest().empty());
    auto j0 = s.to_json(1'030'000'000);
    ASSERT_NEAR(j0["busiest_age_s"].get<double>(), 30.0, 1e-9);     // no data yet: age since start
    for (int i = 0; i < 5; ++i) s.on_msg("BNB", rec::Status::Kind::Book, 1'001'000'000 + i * 1'000'000);
    s.on_msg("GMX", rec::Status::Kind::Trade, 1'010'000'000);
    for (int i = 0; i < 50; ++i) s.on_msg("GMX", rec::Status::Kind::Stats, 1'020'000'000);   // stats never make a market "busy"
    s.on_gap("BNB");
    s.on_reconnect();
    s.set_writer_totals(2, 3, 7);
    ASSERT_EQ(s.busiest(), std::string("BNB"));
    const auto j = s.to_json(1'070'000'000);
    ASSERT_EQ(j["busiest"].get<std::string>(), std::string("BNB"));
    ASSERT_EQ(j["busiest_msgs"].get<int64_t>(), int64_t{5});
    ASSERT_NEAR(j["busiest_age_s"].get<double>(), 65.0, 1e-9);        // last BNB book at 1'005 s, now 1'070 s
    ASSERT_EQ(j["markets"]["GMX"]["stats"].get<int64_t>(), int64_t{50});
    ASSERT_EQ(j["markets"]["GMX"]["last_us"].get<int64_t>(), int64_t{1'020'000'000});
    ASSERT_EQ(j["markets"]["GMX"]["last_data_us"].get<int64_t>(), int64_t{1'010'000'000});
    ASSERT_NEAR(j["markets"]["BNB"]["silence_max_s"].get<double>(), 1.0, 1e-9);
    ASSERT_EQ(j["markets"]["BNB"]["gaps"].get<int64_t>(), int64_t{1});
    ASSERT_EQ(j["reconnects"].get<int64_t>(), int64_t{1});
    ASSERT_EQ(j["write_errors"].get<int64_t>(), int64_t{2});
    ASSERT_EQ(j["dropped_lines"].get<int64_t>(), int64_t{3});
    ASSERT(!j["connected"].get<bool>());
    const auto d = tmpdir("status");
    ASSERT(s.write(d / "sub" / "lighter.json", 1'070'000'000));
    std::ifstream in(d / "sub" / "lighter.json");
    const auto back = json::parse(in);
    ASSERT_EQ(back["busiest"].get<std::string>(), std::string("BNB"));
    ASSERT(!fs::exists(d / "sub" / "lighter.json.tmp"));
    fs::remove_all(d);
}

static std::vector<std::string> read_all(const fs::path& f, int* skipped = nullptr) {
    ZstdLines in(f);
    std::vector<std::string> out;
    std::string l;
    while (in.next(l)) out.push_back(l);
    if (skipped) *skipped = in.skipped_frames();
    return out;
}

static std::string raw_bytes(const fs::path& f) { std::ifstream in(f, std::ios::binary); std::stringstream ss; ss << in.rdbuf(); return ss.str(); }
static void put_bytes(const fs::path& f, const std::string& b) { std::ofstream o(f, std::ios::binary | std::ios::trunc); o << b; }

// Frame starts (zstd magic 28 B5 2F FD) in a file.
static std::vector<size_t> frame_offsets(const std::string& b) {
    std::vector<size_t> o;
    for (size_t i = 0; i + 4 <= b.size(); ++i)
        if ((unsigned char)b[i] == 0x28 && (unsigned char)b[i + 1] == 0xB5 && (unsigned char)b[i + 2] == 0x2F && (unsigned char)b[i + 3] == 0xFD) o.push_back(i);
    return o;
}

TEST(zstd_frames_bound_what_a_kill_or_damage_loses) {
    const auto d = tmpdir("frames");
    const int64_t t0 = 1793014968000000LL;                 // 2026-10-26
    auto line = [](int i) { return "{\"r\":" + std::to_string(i) + ",\"m\":{\"n\":" + std::to_string(i) + ",\"pad\":\"" + std::string(40, 'x') + "\"}}"; };
    // 600 lines, one per second, frames every 60 s → 10 complete frames
    {
        ZstdDaily z(d / "A", {}, 3);
        z.set_frame_seconds(60);
        for (int i = 0; i < 600; ++i) z.write_line(line(i), t0 + int64_t{i} * 1'000'000);
        ASSERT(z.frames() >= 9);
        ASSERT_EQ(z.write_errors(), int64_t{0});
        ASSERT_EQ(z.lines(), int64_t{600});
    }
    const fs::path f = d / "A" / "2026-10-26.jsonl.zst";
    auto all = read_all(f);
    ASSERT_EQ(all.size(), size_t{600});
    for (int i = 0; i < 600; ++i) ASSERT_EQ(all[i], line(i));
    const std::string bytes = raw_bytes(f);
    const auto offs = frame_offsets(bytes);
    ASSERT(offs.size() >= 10);
    // damage one byte inside the 3rd frame: only that frame's lines are lost, every later frame survives
    {
        std::string bad = bytes;
        bad[offs[2] + (offs[3] - offs[2]) / 2] ^= 0x5A;
        put_bytes(f, bad);
        int skipped = 0;
        const auto got = read_all(f, &skipped);
        ASSERT(skipped >= 1);
        ASSERT(got.size() >= 600 - 61);          // ≤ one 60 s frame (+ the line cut at the damage) lost
        ASSERT(got.size() < 600);
        ASSERT_EQ(got.back(), line(599));
    }
    // a kill mid-frame (file cut inside the last frame) then a restart that appends: everything before the cut
    // that was flushed survives, and the next run's lines all read back
    {
        put_bytes(f, bytes.substr(0, offs[9] + 7));         // last frame cut a few bytes after its header
        {
            ZstdDaily z(d / "A", {}, 3);
            z.set_frame_seconds(60);
            for (int i = 1000; i < 1010; ++i) z.write_line(line(i), t0 + 700'000'000 + int64_t{i});
        }
        const auto got = read_all(f);
        ASSERT(got.size() >= 540 + 10);
        ASSERT_EQ(got[539], line(539));
        ASSERT_EQ(got.back(), line(1009));
    }
    // flush(now) closes an idle frame by the caller's clock
    {
        ZstdDaily z(d / "B", {}, 3);
        z.set_frame_seconds(10);
        z.write_line(line(1), t0);
        z.flush(t0 + 5'000'000);
        ASSERT_EQ(z.frames(), int64_t{0});
        z.flush(t0 + 11'000'000);
        ASSERT_EQ(z.frames(), int64_t{1});
        z.flush(t0 + 30'000'000);                           // nothing open: no empty frame
        ASSERT_EQ(z.frames(), int64_t{1});
    }
    ASSERT_EQ(read_all(d / "B" / "2026-10-26.jsonl.zst").size(), size_t{1});
    // default (frame_seconds 0) keeps the old one-frame-per-run behaviour
    {
        ZstdDaily z(d / "C", {}, 3);
        for (int i = 0; i < 300; ++i) z.write_line(line(i), t0 + int64_t{i} * 1'000'000);
        z.flush(t0 + 400'000'000);
        ASSERT_EQ(z.frames(), int64_t{0});
    }
    ASSERT_EQ(frame_offsets(raw_bytes(d / "C" / "2026-10-26.jsonl.zst")).size(), size_t{1});
    // an unwritable directory counts dropped lines instead of crashing
    {
        ZstdDaily z(fs::path("/proc/nonexistent_dir_for_test"), {}, 3);
        z.write_line(line(1), t0);
        z.write_line(line(2), t0);
        ASSERT_EQ(z.dropped_lines(), int64_t{2});
        ASSERT(z.write_errors() >= 1);
    }
    fs::remove_all(d);
}

int main() {
    RUN(market_stats_parse_live_shapes);
    RUN(loader_skips_stats_and_replays_identically);
    RUN(real_archive_day_replays_identically_with_stats_injected);
    RUN(meta_diff_reports_decimals_minimums_and_listing);
    RUN(meta_snapshot_files_and_diff_against_the_newest);
    RUN(status_file_busiest_market_and_ages);
    RUN(zstd_frames_bound_what_a_kill_or_damage_loses);
    std::cout << "\n" << (tests_run - tests_failed) << "/" << tests_run << " passed\n";
    return tests_failed ? 1 : 0;
}
