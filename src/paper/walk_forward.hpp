// walk_forward.hpp — does picking the best parameters on past months make money on the NEXT month?
//
// A group is a set of variants that compete for one job: same strategy family (id prefix) in the same
// session window (by entry time, ET). For every test month t, each group picks the variant with the best
// net over the previous `train` months (≥ `min_train_trades` trades there); if even the best is ≤ 0 the
// group stands aside that month. The pick then trades month t — data the choice never saw.
// Compared with:  baseline = the average variant in month t (picking blind),
//                 oracle   = the best variant in month t (hindsight — what in-sample results look like).
// If the walk-forward total is not clearly above the baseline, "tuning" is choosing noise.
#pragma once
#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace wf {

struct Cell { int n = 0; double net = 0; };

// One group: cells[v][m] = trades of variant v in month index m.
struct Group {
    std::string key;
    std::vector<std::string> variants;
    std::vector<std::vector<Cell>> cells;
};

struct Pick { int month; int variant; double train_net; Cell oos; };

struct Summary {
    int tested = 0, traded = 0, positive = 0, oos_trades = 0;
    double oos_net = 0, baseline_net = 0, oracle_net = 0, train_net_per_month = 0;
    std::vector<Pick> picks;
};

inline Summary walk_forward(const Group& g, int months, int train, int min_train_trades) {
    Summary s;
    const int V = (int)g.variants.size();
    for (int t = train; t < months; ++t) {
        ++s.tested;
        int best = -1; double best_net = 0; int best_n = 0;
        double avg = 0, oracle = -1e18;
        for (int v = 0; v < V; ++v) {
            int n = 0; double net = 0;
            for (int m = t - train; m < t; ++m) { n += g.cells[v][m].n; net += g.cells[v][m].net; }
            if (n >= min_train_trades && (best < 0 || net > best_net || (net == best_net && n > best_n))) {
                best = v; best_net = net; best_n = n;
            }
            avg += g.cells[v][t].net;
            oracle = std::max(oracle, g.cells[v][t].net);
        }
        if (V > 0) { s.baseline_net += avg / V; s.oracle_net += oracle; }
        if (best < 0 || best_net <= 0) continue;   // stand aside
        const Cell& o = g.cells[best][t];
        ++s.traded; s.oos_trades += o.n; s.oos_net += o.net; if (o.net > 0) ++s.positive;
        s.train_net_per_month += best_net / train;
        s.picks.push_back({t, best, best_net, o});
    }
    return s;
}

}  // namespace wf
