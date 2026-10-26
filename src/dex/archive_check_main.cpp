// archive_check — health of recorded DEX archive files (lighter_recorder / hl_recorder / dex_recorder, one
// <dir>/<MARKET>/<YYYY-MM-DD>.jsonl.zst per market-day). Read-only; used by scripts/mm_debug.sh archive.
//
//   archive_check FILE... [--gap-s 60]
//
// One JSON object per file on stdout:
//   {"file","market","day","lines","bad_lines","skipped_frames","corrupt","seq_gaps","snapshots",
//    "books","trades","first_ms","last_ms","hours","max_gap_s","gaps_over","gap_s"}
//   lines           complete lines decoded (ZstdLines; a trailing partial line of a file still being written is not counted)
//   bad_lines       lines that are not a JSON object with "m"
//   skipped_frames  damaged zstd frames skipped (a recorder killed mid-frame) — data inside them is lost
//   seq_gaps        Lighter only: order-book updates whose begin_nonce did not follow the previous nonce (the book is
//                   untrustworthy until the next snapshot; the loader drops it until then)
//   snapshots       Lighter full-book messages (connect, reconnect, day-file header)
//   books / trades  events the replay loader (hl_events.hpp load_archive) produces — what mm_sim sees
//   max_gap_s       longest silence between two consecutive book events; gaps_over = how many silences exceed --gap-s

#include "dex/hl_events.hpp"
#include "zstd_daily.hpp"

#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

int main(int argc, char** argv) {
    std::vector<fs::path> files;
    double gap_s = 60;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--gap-s" && i + 1 < argc) gap_s = std::atof(argv[++i]);
        else if (a == "-h" || a == "--help") { std::fprintf(stderr, "usage: archive_check FILE... [--gap-s 60]\n"); return 2; }
        else files.emplace_back(a);
    }
    if (files.empty()) { std::fprintf(stderr, "usage: archive_check FILE... [--gap-s 60]\n"); return 2; }
    int rc = 0;
    for (const auto& f : files) {
        json o = {{"file", f.string()}, {"market", f.parent_path().filename().string()}, {"day", f.stem().stem().string()}};
        ZstdLines in(f);
        if (!in.ok()) { o["error"] = "cannot open"; std::printf("%s\n", o.dump().c_str()); rc = 1; continue; }
        int64_t lines = 0, bad = 0, seq_gaps = 0, snapshots = 0;
        std::map<int, lighter::Book> lb;
        std::string line;
        while (in.next(line)) {
            ++lines;
            json j = json::parse(line, nullptr, false);
            if (j.is_discarded() || !j.is_object() || !j.contains("m")) { ++bad; continue; }
            const json& m = j["m"];
            if (const int mk = lighter::market_of(m, "order_book"); mk >= 0) {
                const auto r = lb[mk].apply(m);
                if (r == lighter::Book::Apply::Gap) ++seq_gaps;
                else if (r == lighter::Book::Apply::Snapshot) ++snapshots;
            }
        }
        int64_t bad2 = 0;
        const auto ev = hl::load_archive(f, &bad2);
        int64_t books = 0, trades = 0, first = 0, last = 0, max_gap = 0, over = 0;
        for (const auto& e : ev) {
            if (!e.is_book) { ++trades; continue; }
            ++books;
            if (first) {
                const int64_t g = e.t_ms - last;
                max_gap = std::max(max_gap, g);
                if (g > gap_s * 1000) ++over;
            } else first = e.t_ms;
            last = e.t_ms;
        }
        o["lines"] = lines; o["bad_lines"] = bad; o["skipped_frames"] = in.skipped_frames(); o["corrupt"] = in.corrupt();
        o["seq_gaps"] = seq_gaps; o["snapshots"] = snapshots; o["books"] = books; o["trades"] = trades;
        o["first_ms"] = first; o["last_ms"] = last; o["hours"] = first ? (last - first) / 3'600'000.0 : 0.0;
        o["max_gap_s"] = max_gap / 1000.0; o["gaps_over"] = over; o["gap_s"] = gap_s;
        std::printf("%s\n", o.dump().c_str());
        std::fflush(stdout);
    }
    return rc;
}
