// walk_forward — walk-forward test of parameter selection over the history replays (model: walk_forward.hpp).
//
//   walk_forward --cells FILE --train 3,6,12 [--min-train-trades 20] [--top 15] --report OUT.md --csv OUT.csv
//
// cells file (scripts/walk_forward.sh): strategy_id,family,session,YYYY-MM,trades,net   (net per MNQ contract)
// Groups: family × session, plus "ALL" × session (every strategy competing for the session).
#include "walk_forward.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

int main(int argc, char** argv) {
    std::string cells_path, report_path, csv_path;
    std::vector<int> trains = {6};
    int min_tt = 20, top = 15;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { if (i + 1 >= argc) { std::cerr << "missing value for " << a << "\n"; std::exit(2); } return argv[++i]; };
        if (a == "--cells") cells_path = next();
        else if (a == "--report") report_path = next();
        else if (a == "--csv") csv_path = next();
        else if (a == "--min-train-trades") min_tt = std::atoi(next().c_str());
        else if (a == "--top") top = std::atoi(next().c_str());
        else if (a == "--train") {
            trains.clear(); std::stringstream ss(next()); std::string x;
            while (std::getline(ss, x, ',')) if (std::atoi(x.c_str()) > 0) trains.push_back(std::atoi(x.c_str()));
        } else { std::cerr << "unknown arg " << a << "\n"; return 2; }
    }
    if (cells_path.empty() || report_path.empty() || csv_path.empty() || trains.empty()) {
        std::cerr << "usage: walk_forward --cells F --train 3,6,12 --report OUT.md --csv OUT.csv\n"; return 2;
    }

    struct Row { std::string id, fam, ses, month; int n; double net; };
    std::vector<Row> rows; std::set<std::string> month_set, sessions;
    {
        std::ifstream f(cells_path);
        if (!f) { std::cerr << "cannot open " << cells_path << "\n"; return 1; }
        std::string line;
        while (std::getline(f, line)) {
            std::stringstream ss(line); Row r; std::string n, net;
            if (!std::getline(ss, r.id, ',') || !std::getline(ss, r.fam, ',') || !std::getline(ss, r.ses, ',') ||
                !std::getline(ss, r.month, ',') || !std::getline(ss, n, ',') || !std::getline(ss, net, ',')) continue;
            r.n = std::atoi(n.c_str()); r.net = std::atof(net.c_str());
            rows.push_back(r); month_set.insert(r.month); sessions.insert(r.ses);
        }
    }
    const std::vector<std::string> months(month_set.begin(), month_set.end());
    std::map<std::string, int> midx; for (int i = 0; i < (int)months.size(); ++i) midx[months[i]] = i;
    const int M = (int)months.size();

    // Build groups: family|session and ALL|session.
    std::map<std::string, wf::Group> groups;
    std::map<std::string, std::map<std::string, int>> vidx;
    auto add = [&](const std::string& key, const Row& r) {
        auto& g = groups[key]; g.key = key;
        auto& vi = vidx[key];
        auto it = vi.find(r.id);
        int v;
        if (it == vi.end()) { v = (int)g.variants.size(); vi[r.id] = v; g.variants.push_back(r.id); g.cells.emplace_back(M); }
        else v = it->second;
        auto& c = g.cells[v][midx[r.month]]; c.n += r.n; c.net += r.net;
    };
    for (const auto& r : rows) { add(r.fam + " | " + r.ses, r); add("ALL | " + r.ses, r); }
    std::cerr << "[walk_forward] " << rows.size() << " cells, " << M << " months (" << (M ? months.front() : "") << " … "
              << (M ? months.back() : "") << "), " << groups.size() << " groups\n";

    std::ofstream csv(csv_path);
    csv << "train_months,group,variants,tested,traded,positive,oos_trades,oos_net,baseline_net,oracle_net,train_net_per_month\n";
    std::ofstream md(report_path);
    md << "# Walk-forward parameter selection\n\n"
       << M << " months of history (" << (M ? months.front() : "") << " … " << (M ? months.back() : "")
       << "; months with no data are skipped, so a window can span the 2026-03 → 2026-09 gap). Each **group** = one "
          "strategy family in one session window; every month it trades the variant with the best net over the previous "
          "N months (≥ " << min_tt << " trades there) and stands aside if that best is ≤ 0. All $ per MNQ contract.\n\n"
          "- **walk-forward** = what the picks made on the next month (never seen when picking)\n"
          "- **blind** = the average variant that month (no tuning)\n"
          "- **hindsight** = the best variant that month (what an in-sample ranking shows)\n"
          "- **picked made / mo (train)** vs **walk-forward / mo**: how much of the in-sample edge survives\n\n";

    for (int train : trains) {
        struct Out { std::string key; int V; wf::Summary s; };
        std::vector<Out> outs;
        for (const auto& [key, g] : groups) {
            auto s = wf::walk_forward(g, M, train, min_tt);
            csv << train << "," << key << "," << g.variants.size() << "," << s.tested << "," << s.traded << ","
                << s.positive << "," << s.oos_trades << "," << s.oos_net << "," << s.baseline_net << "," << s.oracle_net
                << "," << (s.traded ? s.train_net_per_month / s.traded : 0) << "\n";
            outs.push_back({key, (int)g.variants.size(), std::move(s)});
        }
        int traded_groups = 0, pos_groups = 0; double tot = 0;
        for (auto& o : outs) if (o.s.traded > 0) { ++traded_groups; tot += o.s.oos_net; if (o.s.oos_net > 0) ++pos_groups; }
        md << "## Train on the previous " << train << " months\n\n"
           << "Groups that traded at least one month: " << traded_groups << "; positive walk-forward: **" << pos_groups
           << "**; summed walk-forward net of all groups: **" << (long)tot << " $**.\n\n";

        for (const auto& ses : sessions) {
            std::vector<const Out*> v;
            for (auto& o : outs) {
                const auto bar = o.key.rfind(" | ");
                if (bar != std::string::npos && o.key.substr(bar + 3) == ses && o.s.traded > 0) v.push_back(&o);
            }
            std::sort(v.begin(), v.end(), [](auto* a, auto* b) { return a->s.oos_net > b->s.oos_net; });
            md << "### " << ses << "\n\n| family | variants | months traded / tested | positive months | trades | walk-forward $ | blind $ | hindsight $ | picked made / mo (train) | walk-forward / mo | most-picked variant |\n"
               << "|---|---|---|---|---|---|---|---|---|---|---|\n";
            char buf[768];
            for (int i = 0; i < (int)v.size() && i < top; ++i) {
                const auto& o = *v[i];
                std::map<int, int> cnt; for (auto& p : o.s.picks) ++cnt[p.variant];
                int mv = -1, mc = 0; for (auto& [k, c] : cnt) if (c > mc) { mv = k; mc = c; }
                const auto& g = groups.at(o.key);
                std::snprintf(buf, sizeof buf, "| %s | %d | %d / %d | %d | %d | %.0f | %.0f | %.0f | %.0f | %.0f | %s (%d×) |\n",
                              o.key.substr(0, o.key.rfind(" | ")).c_str(), o.V, o.s.traded, o.s.tested, o.s.positive,
                              o.s.oos_trades, o.s.oos_net, o.s.baseline_net, o.s.oracle_net,
                              o.s.traded ? o.s.train_net_per_month / o.s.traded : 0.0,
                              o.s.traded ? o.s.oos_net / o.s.traded : 0.0,
                              mv >= 0 ? g.variants[mv].c_str() : "-", mc);
                md << buf;
            }
            if (v.empty()) md << "| _no group passed the training filter_ | | | | | | | | | | |\n";
            md << "\n";
        }
    }
    std::cerr << "[walk_forward] report → " << report_path << "\n";
    return 0;
}
