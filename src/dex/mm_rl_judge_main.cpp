// mm_rl_judge — the contextual-bandit judge over mm_rl_label's dataset (2026-10-28, RL phase 1).
// For each side and each quoting action (the widths in the file), a ridge regression predicts the window's markout-adjusted
// reward from the state at the window start. The learned policy picks, per window and side, the action with the highest
// predicted reward — or PULL (reward 0) when every prediction is below --margin. Because every action is labelled in every
// window, the policy's realised reward on held-out windows is read straight from the file (no simulation at judge time).
// Splits: --test-days (walk-forward: train on the other days) and --test-coins (unseen coins: train on the other coins).
// Reported against: always width W (--base), the frozen policy column, the best single action in hindsight on the test set,
// the per-window oracle (an upper bound), and a SHUFFLED-LABEL control (rewards permuted within coin-day; a policy that
// still "wins" there is fitting noise). Pass rule lives in docs/market_making_plan.md.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

struct Row { std::string coin, day; double f[14]; std::map<std::string, double> r; std::map<std::string, int> n; };
static const char* FEATS[] = {"hour_s", "hour_c", "spread_bps", "imb", "depth_bid_usd", "depth_ask_usd", "trades_60", "buy_share_60", "trade_usd_60", "range_60_bps", "dmid_1s_bps", "dmid_5s_bps", "ms_since_jump", "one"};
static constexpr int NF = 14;

struct Ridge {   // closed-form ridge on standardised features; small NF → solve normal equations by Gauss-Jordan
    double w[NF] = {}, mu[NF] = {}, sd[NF] = {};
    void fit(const std::vector<const Row*>& rows, const std::string& key, double lambda) {
        const size_t n = rows.size(); if (!n) return;
        for (int k = 0; k < NF; ++k) { double s = 0, s2 = 0; for (auto* r : rows) { s += r->f[k]; s2 += r->f[k] * r->f[k]; } mu[k] = s / n; sd[k] = std::sqrt(std::max(s2 / n - mu[k] * mu[k], 1e-12)); }
        mu[NF - 1] = 0; sd[NF - 1] = 1;   // intercept column
        double A[NF][NF] = {}, b[NF] = {};
        for (auto* r : rows) {
            double x[NF]; for (int k = 0; k < NF; ++k) x[k] = (r->f[k] - mu[k]) / sd[k];
            const double y = r->r.at(key);
            for (int i = 0; i < NF; ++i) { b[i] += x[i] * y; for (int j = 0; j < NF; ++j) A[i][j] += x[i] * x[j]; }
        }
        for (int i = 0; i < NF - 1; ++i) A[i][i] += lambda * n;
        // Gauss-Jordan
        double M[NF][NF + 1]; for (int i = 0; i < NF; ++i) { for (int j = 0; j < NF; ++j) M[i][j] = A[i][j]; M[i][NF] = b[i]; }
        for (int c = 0; c < NF; ++c) {
            int p = c; for (int r2 = c + 1; r2 < NF; ++r2) if (std::fabs(M[r2][c]) > std::fabs(M[p][c])) p = r2;
            for (int j = 0; j <= NF; ++j) std::swap(M[c][j], M[p][j]);
            const double d = M[c][c]; if (std::fabs(d) < 1e-12) continue;
            for (int j = 0; j <= NF; ++j) M[c][j] /= d;
            for (int r2 = 0; r2 < NF; ++r2) if (r2 != c) { const double f = M[r2][c]; for (int j = 0; j <= NF; ++j) M[r2][j] -= f * M[c][j]; }
        }
        for (int i = 0; i < NF; ++i) w[i] = M[i][NF];
    }
    double predict(const Row& r) const { double y = 0; for (int k = 0; k < NF; ++k) y += w[k] * (r.f[k] - mu[k]) / sd[k]; return y; }
};

int main(int argc, char** argv) {
    std::string file; std::set<std::string> test_days, test_coins; double lambda = 1.0, margin = 0.0; std::string base = "5"; unsigned seed = 7;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i]; auto val = [&] { return std::string(argv[++i]); };
        if (a == "--file") file = val();
        else if (a == "--test-days") { std::stringstream ss(val()); for (std::string c; std::getline(ss, c, ',');) test_days.insert(c); }
        else if (a == "--test-coins") { std::stringstream ss(val()); for (std::string c; std::getline(ss, c, ',');) test_coins.insert(c); }
        else if (a == "--lambda") lambda = std::atof(val().c_str());
        else if (a == "--margin") margin = std::atof(val().c_str());
        else if (a == "--base") base = val();
        else { std::fprintf(stderr, "unknown arg %s\n", a.c_str()); return 2; }
    }
    std::ifstream in(file); if (!in) { std::fprintf(stderr, "cannot read --file\n"); return 2; }
    std::string line; std::getline(in, line); std::vector<std::string> hdr; { std::stringstream ss(line); for (std::string c; std::getline(ss, c, ',');) hdr.push_back(c); }
    std::map<std::string, int> col; for (size_t i = 0; i < hdr.size(); ++i) col[hdr[i]] = (int)i;
    std::vector<std::string> acts; for (auto& h : hdr) if (h.rfind("r_bid_", 0) == 0) acts.push_back(h.substr(6));
    std::vector<Row> rows;
    while (std::getline(in, line)) {
        std::vector<std::string> c; { std::stringstream ss(line); for (std::string x; std::getline(ss, x, ',');) c.push_back(x); }
        if (c.size() != hdr.size()) continue;
        Row r; r.coin = c[col["coin"]]; r.day = c[col["day"]];
        const double h = std::atof(c[col["hour"]].c_str()); r.f[0] = std::sin(2 * M_PI * h / 24); r.f[1] = std::cos(2 * M_PI * h / 24);
        for (int k = 2; k < NF - 1; ++k) r.f[k] = std::atof(c[col[FEATS[k]]].c_str());
        r.f[NF - 1] = 1;
        for (auto& a : acts) for (const char* s : {"bid", "ask"}) { r.r[std::string(s) + "_" + a] = std::atof(c[col["r_" + std::string(s) + "_" + a]].c_str()); r.n[std::string(s) + "_" + a] = std::atoi(c[col["n_" + std::string(s) + "_" + a]].c_str()); }
        rows.push_back(std::move(r));
    }
    std::vector<std::string> widths; for (auto& a : acts) if (a != "policy") widths.push_back(a);
    auto is_test = [&](const Row& r) { return (test_days.empty() || test_days.count(r.day)) && (test_coins.empty() || test_coins.count(r.coin)); };
    std::vector<const Row*> train, test;
    for (auto& r : rows) {
        const bool td = test_days.empty() || test_days.count(r.day), tc = test_coins.empty() || test_coins.count(r.coin);
        if (td && tc) test.push_back(&r);
        else if ((!test_days.empty() && !td) || (!test_coins.empty() && !tc)) train.push_back(&r);
    }
    if (train.empty() || test.empty()) { std::fprintf(stderr, "need both --test-days and/or --test-coins that split the data (train %zu test %zu)\n", train.size(), test.size()); return 2; }
    std::printf("mm_rl_judge: %zu rows, actions {", rows.size()); for (auto& a : acts) std::printf(" %s", a.c_str()); std::printf(" }, train %zu windows, test %zu windows, lambda %.2f, margin $%.4f\n", train.size(), test.size(), lambda, margin);

    auto evaluate = [&](const std::vector<const Row*>& tr, const std::vector<const Row*>& te, const char* tag) {
        std::map<std::string, Ridge> models;
        for (auto& w : widths) for (const char* s : {"bid", "ask"}) { Ridge m; m.fit(tr, std::string(s) + "_" + w, lambda); models[std::string(s) + "_" + w] = m; }
        double learned = 0, oracle = 0, basev = 0, policy = 0; std::map<std::string, double> single; int pulls = 0, decisions = 0; std::map<std::string, int> picked; double learned_fills_usd = 0, base_fills_usd = 0;
        for (auto* r : te) for (const char* s : {"bid", "ask"}) {
            std::string best; double bp = margin; double orc = 0;
            for (auto& w : widths) { const std::string k = std::string(s) + "_" + w; const double p = models[k].predict(*r); if (p > bp) { bp = p; best = w; } orc = std::max(orc, r->r.at(k)); single[w] += r->r.at(k); }
            ++decisions; oracle += orc;
            basev += r->r.at(std::string(s) + "_" + base); base_fills_usd += 11.0 * r->n.at(std::string(s) + "_" + base);
            policy += r->r.count(std::string(s) + "_policy") ? r->r.at(std::string(s) + "_policy") : 0;
            if (best.empty()) ++pulls; else { learned += r->r.at(std::string(s) + "_" + best); ++picked[best]; learned_fills_usd += 11.0 * r->n.at(std::string(s) + "_" + best); }
        }
        std::string bs; double bv = -1e18; for (auto& [w, v] : single) if (v > bv) { bv = v; bs = w; }
        std::printf("[%s] test windows %zu · learned policy $%+.2f (%.1f bps of $%.0f filled; pulls %.0f%%; picks", tag, te.size(), learned, learned_fills_usd > 0 ? 1e4 * learned / learned_fills_usd : 0, learned_fills_usd, 100.0 * pulls / decisions);
        for (auto& [w, c] : picked) std::printf(" %s:%d", w.c_str(), c);
        std::printf(") · always %s $%+.2f (%.1f bps of $%.0f) · frozen policy $%+.2f · best single in hindsight %s $%+.2f · oracle $%+.2f\n", base.c_str(), basev, base_fills_usd > 0 ? 1e4 * basev / base_fills_usd : 0, base_fills_usd, policy, bs.c_str(), bv, oracle);
        return learned - basev;
    };
    const double real = evaluate(train, test, "real");
    // shuffled-label control: permute every action's rewards within each coin-day of the TRAIN set, refit, evaluate on the same test
    std::mt19937 rng(seed); std::vector<Row> shuf; shuf.reserve(rows.size()); for (auto& r : rows) shuf.push_back(r);
    std::map<std::string, std::vector<Row*>> groups; for (auto& r : shuf) groups[r.coin + r.day].push_back(&r);
    for (auto& [g, v] : groups) { std::vector<size_t> idx(v.size()); for (size_t i = 0; i < v.size(); ++i) idx[i] = i; std::shuffle(idx.begin(), idx.end(), rng); std::vector<std::map<std::string, double>> rr; for (auto* r : v) rr.push_back(r->r); for (size_t i = 0; i < v.size(); ++i) v[i]->r = rr[idx[i]]; }
    std::vector<const Row*> strain; for (auto& r : shuf) { const bool td = test_days.empty() || test_days.count(r.day), tc = test_coins.empty() || test_coins.count(r.coin); if ((!test_days.empty() && !td) || (!test_coins.empty() && !tc)) strain.push_back(&r); }
    const double ctrl = evaluate(strain, test, "shuffled-train control");
    std::printf("learned − always %s: real $%+.2f vs shuffled-train $%+.2f\n", base.c_str(), real, ctrl);
    return 0;
}
