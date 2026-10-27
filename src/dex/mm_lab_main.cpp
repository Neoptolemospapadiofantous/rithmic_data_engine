// mm_lab — the deterministic research judge for the DEX market maker (src/dex/mm_lab.hpp has the idea format, the
// feature library, the pre-registered evaluation and the PASS rule). Research only: reads archives, writes cards.
//
//   mm_lab run IDEAS.json [--out DIR] [--registry FILE] [--asof TIME] [--threads N] [--root DIR] [--params-dir DIR]
//              [--summary FILE] [--now ISO]
//        judge every idea IN-SAMPLE (events before the venue's frozen boundary only), write cards to
//        DIR/cards/<id>.{md,json}, append one line per idea to the registry. Ids already in it are refused.
//   mm_lab run IDEAS.json --holdout --out HOLDOUT_DIR [--asof TIME] [...]
//        the sealed holdout [frozen boundary, asof], ONLY for ids with an in-sample PASS of the same spec in the PINNED
//        registry (<repo>/data/mm_lab/registry.jsonl — --registry is refused here), one look per id. The look is logged
//        to the registry (no result); the cards go to HOLDOUT_DIR and HOLDOUT_DIR/holdout_log.jsonl (final reporter).
//   mm_lab check IDEAS.json      parse + validate only (exit 1 if any idea is refused)
//   mm_lab features              the feature library
//   mm_lab split VENUE [--asof TIME]   the split the 30 % rule would freeze now (the frozen one is in the file below)
//
// TIME = epoch ms, YYYY-MM-DD, or YYYY-MM-DDTHH:MM[:SS]. Default --asof = the last full UTC hour that ended ≥ 5 min ago;
// a later --asof than now − 5 min is refused (a live archive is still being written). Default --threads 3.
// Pinned files (compile-time repo root MM_LAB_REPO, else the current directory):
//   <repo>/data/mm_lab/registry.jsonl            the registry (append-only; ops: sudo chattr +a on it)
//   <repo>/data/mm_lab/holdout_boundaries.json   one frozen holdout boundary per venue, written once (commit it)
//   <repo>/data/mm_lab/excluded_days.json        venue → day → reason: damaged archive days never loaded (commit it)
#include "dex/mm_lab.hpp"

#include <cstdio>
#include <iomanip>
#include <iostream>

using namespace mmlab;

#ifndef MM_LAB_REPO
#define MM_LAB_REPO "."
#endif

static int usage() {
    std::fprintf(stderr, "usage: mm_lab run IDEAS.json [--holdout] [--out DIR] [--registry FILE] [--asof TIME] [--threads N] [--root DIR] "
                         "[--params-dir DIR] [--summary FILE] [--now ISO]\n       mm_lab check IDEAS.json | mm_lab features | mm_lab split VENUE [--asof TIME]\n"
                         "       mm_lab daily VENUE --from TIME --to TIME --file OUT.json [--size USD]   (G1: frozen setting, real limits)\n");
    return 2;
}

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    const std::string cmd = argv[1];
    Options opt;
    opt.pinned_registry = fs::path(MM_LAB_REPO) / "data/mm_lab/registry.jsonl";
    opt.registry = opt.pinned_registry;
    opt.boundaries = fs::path(MM_LAB_REPO) / "data/mm_lab/holdout_boundaries.json";
    try { opt.env.excluded_days = load_excluded_days(fs::path(MM_LAB_REPO) / "data/mm_lab/excluded_days.json"); }
    catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 2; }
    bool registry_given = false;
    fs::path ideas_file, summary, out_file;
    int64_t from_ms = -1, to_ms = -1;
    double size_usd = 0;
    std::string venue_arg;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto val = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--out") opt.out_dir = val();
        else if (a == "--registry") { opt.registry = val(); registry_given = true; }
        else if (a == "--asof") { opt.asof_ms = parse_time(val()); if (opt.asof_ms < 0) { std::fprintf(stderr, "bad --asof\n"); return 2; } }
        else if (a == "--threads") opt.threads = std::max(1, std::min(8, std::atoi(val().c_str())));
        else if (a == "--root") opt.env.root = val();
        else if (a == "--params-dir") opt.env.params_dir = val();
        else if (a == "--summary") summary = val();
        else if (a == "--now") opt.now_iso = val();
        else if (a == "--holdout") opt.holdout = true;
        else if (a == "--from") from_ms = parse_time(val());
        else if (a == "--to") to_ms = parse_time(val());
        else if (a == "--size") size_usd = std::atof(val().c_str());
        else if (a == "--file") out_file = val();
        else if (!a.empty() && a[0] != '-' && ideas_file.empty() && venue_arg.empty()) { if (cmd == "split" || cmd == "daily") venue_arg = a; else ideas_file = a; }
        else return usage();
    }
    const int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    if (!opt.asof_ms) opt.asof_ms = (now_ms - 5 * 60'000) / 3'600'000 * 3'600'000;
    if (opt.now_iso.empty()) opt.now_iso = iso_ms(now_ms);
    if (const std::string why = check_asof(opt.asof_ms, now_ms); !why.empty() && cmd == "run") { std::fprintf(stderr, "mm_lab: %s\n", why.c_str()); return 2; }
    if (opt.holdout && (registry_given || std::getenv("MM_LAB_REGISTRY"))) {
        std::fprintf(stderr, "mm_lab: --holdout reads only the pinned registry %s — --registry / MM_LAB_REGISTRY refused\n", opt.pinned_registry.c_str());
        return 2;
    }

    if (cmd == "features") {
        std::printf("| feature | default window ms | source | robustness step | meaning |\n|---|---|---|---|---|\n");
        for (const auto& f : features())
            std::printf("| %s | %s | %s | %g | %s |\n", f.name, f.default_window ? std::to_string(f.default_window).c_str() : "—",
                        f.ref == kRefHl ? "Hyperliquid (receive time)" : f.ref == kRefExt ? "Extended (receive time)" : f.state ? "simulator state" : "own venue", f.step, f.desc);
        return 0;
    }
    if (cmd == "split") {
        const auto V = venue_of(venue_arg, opt.env);
        if (!V) { std::fprintf(stderr, "unknown venue\n"); return 2; }
        std::cout << compute_split(*V, venue_days(*V), opt.asof_ms).to_json().dump(1) << "\n";
        return 0;
    }
    if (cmd == "daily") {    // G1's data check: the frozen setting under the venue's standard execution, per coin-day
        if (venue_arg.empty() || from_ms < 0 || to_ms <= from_ms || out_file.empty()) {
            std::fprintf(stderr, "usage: mm_lab daily VENUE --from TIME --to TIME --file OUT.json [--size USD]\n"); return 2;
        }
        try {
            const json d = daily_baseline(opt.env, venue_arg, size_usd, from_ms, to_ms, opt.threads);
            std::ofstream(out_file) << d.dump(1) << "\n";
            std::printf("%zu coin-day rows → %s\n", d["rows"].size(), out_file.c_str());
        } catch (const std::exception& e) { std::fprintf(stderr, "mm_lab daily: %s\n", e.what()); return 2; }
        return 0;
    }
    if (cmd != "run" && cmd != "check") return usage();
    if (ideas_file.empty()) return usage();
    std::ifstream in(ideas_file);
    json doc = json::parse(in, nullptr, false);
    if (doc.is_discarded()) { std::fprintf(stderr, "%s: not valid JSON\n", ideas_file.c_str()); return 2; }
    if (cmd == "check") {
        if (!doc.is_object() || !doc.contains("ideas") || !doc["ideas"].is_array()) { std::fprintf(stderr, "expected {\"ideas\":[...]}\n"); return 1; }
        int bad = 0;
        for (const auto& j : doc["ideas"]) {
            std::string err;
            std::optional<Idea> I;
            try { I = parse_idea(j, opt.env, err); } catch (const json::exception& e) { err = e.what(); }
            const std::string id = j.is_object() && j.contains("id") && j["id"].is_string() ? j["id"].get<std::string>() : "?";
            if (I) std::printf("ok      %s (%s, hash %s, %zu rules, %zu neighbours)\n", I->id.c_str(), I->venue.c_str(), I->hash.c_str(), I->rules.size(),
                               neighbours(*I, *venue_of(I->venue, opt.env)).size());
            else { ++bad; std::printf("REFUSED %s: %s\n", id.c_str(), err.c_str()); }
        }
        return bad ? 1 : 0;
    }
    if (const char* e = std::getenv("MM_FILLS"); e && *e && std::string(e) != "nocross")
        std::fprintf(stderr, "note: MM_FILLS=%s ignored — mm_lab always judges with the NoCross fill model\n", e);
    BatchResult br;
    try { br = judge_batch(doc, opt); }
    catch (const std::exception& e) { std::fprintf(stderr, "mm_lab: %s\n", e.what()); return 1; }
    std::error_code ec;
    const fs::path cdir = opt.holdout ? opt.out_dir : opt.out_dir / "cards";
    fs::create_directories(cdir, ec);
    for (const auto& c : br.cards) {
        const std::string id = c["id"].get<std::string>() + (opt.holdout ? ".holdout" : "");
        std::ofstream(cdir / (id + ".json")) << c.dump(1) << "\n";
        std::ofstream(cdir / (id + ".md")) << card_md(c);
        if (opt.holdout) {
            std::ofstream log(opt.out_dir / "holdout_log.jsonl", std::ios::app);
            log << json{{"ts", opt.now_iso}, {"id", c["id"]}, {"hash", c["hash"]}, {"verdict", c["verdict"]}, {"net_bps", c["idea"]["net_bps"]},
                        {"delta_bps", c["delta_bps"]}, {"pnl_usd", c["idea"]["pnl_usd"]}, {"fills", c["idea"]["fills"]}, {"window", c["window"]}}.dump() << "\n";
        }
    }
    // summary (stdout + optional file)
    std::ostringstream o;
    char b[512];
    o << "# mm_lab " << (opt.holdout ? "HOLDOUT" : "in-sample") << " run — " << br.cards.size() << " idea(s), " << br.refused.size() << " refused, "
      << std::fixed << std::setprecision(1) << br.seconds << " s\n\n";
    if (!br.cards.empty()) {
        o << "| id | venue | verdict | fills | P&L $ | net bps | same-size base bps | Δ bps | $100 base bps | mk10 | coins + | units + | t / needed (k) | neighbours failing | first fail |\n"
             "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n";
        for (const auto& c : br.cards) {
            const auto& cr = c["criteria"];
            const double b100 = c.contains("baseline_100") ? c["baseline_100"]["net_bps"].get<double>() : c["baseline"]["net_bps"].get<double>();
            std::snprintf(b, sizeof b, "| %s | %s | %s | %lld | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %d/%d | %d/%d | %.2f / %.2f (%lld) | %d/%zu | %s |\n",
                          c["id"].get<std::string>().c_str(), c["venue"].get<std::string>().c_str(), c["verdict"].get<std::string>().c_str(),
                          static_cast<long long>(c["idea"]["fills"].get<int64_t>()), c["idea"]["pnl_usd"].get<double>(), c["idea"]["net_bps"].get<double>(),
                          c["baseline"]["net_bps"].get<double>(), c["delta_bps"].get<double>(), b100, c["idea"]["mk10_bps"].get<double>(),
                          cr["coins_positive"].get<int>(), cr["coins_traded"].get<int>(), cr["units_positive"].get<int>(), cr["units"].get<int>(),
                          cr["t"].get<double>(), cr["t_needed"].get<double>(), static_cast<long long>(cr["k"].get<int64_t>()),
                          cr["neighbours_bad"].get<int>(), cr["neighbours"].get<size_t>(),
                          c["fails"].empty() ? "—" : c["fails"][0].get<std::string>().c_str());
            o << b;
        }
    }
    if (!br.refused.empty()) { o << "\n## Refused\n\n"; for (const auto& r : br.refused) o << "- " << r << "\n"; }
    o << "\nruntime: " << br.runtime.dump() << "\n";
    std::fputs(o.str().c_str(), stdout);
    if (!summary.empty()) {
        if (summary.has_parent_path()) fs::create_directories(summary.parent_path(), ec);
        std::ofstream(summary) << o.str();
        json sj = {{"kind", opt.holdout ? "holdout" : "in_sample"}, {"seconds", br.seconds}, {"refused", br.refused}, {"runtime", br.runtime}, {"ideas", json::array()}};
        for (const auto& c : br.cards) sj["ideas"].push_back({{"id", c["id"]}, {"venue", c["venue"]}, {"verdict", c["verdict"]}, {"fails", c["fails"]},
                                                               {"idea", c["idea"]}, {"baseline", c["baseline"]}, {"delta_bps", c["delta_bps"]}, {"criteria", c["criteria"]}});
        std::ofstream(fs::path(summary).replace_extension(".json")) << sj.dump(1) << "\n";
    }
    return 0;
}
