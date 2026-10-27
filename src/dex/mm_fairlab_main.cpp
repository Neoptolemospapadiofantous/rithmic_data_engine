// mm_fairlab — scores fair-price ("centre") models for the Lighter market maker by how well they PREDICT the mid
// 1 s and 10 s ahead, not by simulated P&L (2026-10-27). Research only: reads archives, writes a report. No orders,
// no keys, no DB, no registry — it spends nothing of mm_lab's alpha budget.
//
// Why prediction, not P&L: on a 23 h window the judge's P&L has a ±2 bps path-noise floor (placebo test, 27 Oct);
// a prediction error over ~300 k samples per coin does not. A centre model that does not predict the mid cannot
// lower adverse selection, so only models that predict here go on to the P&L judge.
//
// Per coin, one UTC window [--from, --to), split in two at --split: models are FITTED on the first part and SCORED on
// the second. Defaults keep inside mm_lab's epoch-2 in-sample (26 Oct 00:00 → 23:00 UTC), so the sealed holdout is
// never read. Samples on a 250 ms grid; at a grid time g every input is causal:
//   * Lighter books / trades with exchange time ≤ g
//   * Extended / Hyperliquid books RECEIVED by g (the judge's rule), stale after 5 s
//   * Binance futures aggTrades with exchange time ≤ g − 100 ms (history has no receive time; 100 ms is an assumed
//     delivery delay to Cyprus — a lower bound on what we could act on)
// Target: Lighter mid at g + h (last book ≤ g + h) − mid at g, in bps, h = 1 s and 10 s.
//
// Features (bps or ratio): micro1 (1-level microprice − mid), imb1/imb3/imb5 (depth imbalance, levels weighted 1,
// ½, ¼, …), flow1/flow5 (aggressor-signed $ imbalance over 1/5 s), ofi1 (Cont–Kukanov–Stoikov order-flow imbalance
// over 1 s, / mean top size), ext_mv / hl_mv / bn_mv (reference move over the last 0.5 s and 1 s), ext_gap / hl_gap /
// bn_gap (reference − our mid, minus its own 5-min median of 1-s samples: convergence). Models: each feature alone
// (OLS slope + intercept per coin, fitted on the first part), micro1 as the strategy uses it (slope 1, no fit), a
// fitted microprice (mean target per imbalance decile × spread bucket), and all features together (ridge OLS).
// Score: out-of-sample R² against "the mid does not move" (1 − SSE / Σy²) — pooled over coins as Σ SSE / Σ y² — and
// correlation; + the mean |prediction| in the top decile of |prediction| and the realised move there.
//
//   mm_fairlab --root DIR --coins AAVE,BNB,… [--from 2026-10-26T00:00] [--to 2026-10-26T23:00] [--split 2026-10-26T12:00]
//              [--binance DIR] [--binance-delay-ms 100] [--out report.json]

#include "dex/hl_events.hpp"
#include "dex/hl_mm.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;
static constexpr double NaN = std::numeric_limits<double>::quiet_NaN();
static constexpr int64_t kGridMs = 250, kStaleMs = 5'000;
static int64_t kBinanceDelayMs = 100;   // --binance-delay-ms: robustness check against clock offsets
static const std::array<int64_t, 2> kH = {1'000, 10'000};

static int64_t parse_iso(const std::string& s) {     // YYYY-MM-DD[THH:MM] UTC → ms
    std::tm tm{}; int Y, M, D, h = 0, m = 0;
    if (std::sscanf(s.c_str(), "%d-%d-%dT%d:%d", &Y, &M, &D, &h, &m) < 3) return -1;
    tm.tm_year = Y - 1900; tm.tm_mon = M - 1; tm.tm_mday = D; tm.tm_hour = h; tm.tm_min = m;
    return static_cast<int64_t>(timegm(&tm)) * 1000;
}
static std::string day_of(int64_t ms) {
    std::time_t t = ms / 1000; std::tm tm{}; gmtime_r(&t, &tm);
    char b[16]; std::strftime(b, sizeof b, "%Y-%m-%d", &tm); return b;
}

// A reference price series ordered by the time we may use it (receive time, or exchange time + delay).
struct Ref { std::vector<std::pair<int64_t, double>> pts; };
static Ref ref_from_archive(const fs::path& f) {
    Ref r;
    std::error_code ec;
    if (!fs::exists(f, ec)) return r;
    for (const auto& e : hl::load_archive(f)) if (e.is_book && e.book.valid() && e.r_ms > 0) r.pts.push_back({e.r_ms, e.book.mid()});
    std::stable_sort(r.pts.begin(), r.pts.end(), [](auto& a, auto& b) { return a.first < b.first; });
    return r;
}
static Ref ref_from_binance(const fs::path& zip) {    // aggTrades: id,price,qty,first,last,time,is_buyer_maker
    Ref r;
    std::error_code ec;
    if (!fs::exists(zip, ec)) return r;
    FILE* p = popen(("unzip -p '" + zip.string() + "'").c_str(), "r");
    if (!p) return r;
    char line[512];
    while (std::fgets(line, sizeof line, p)) {
        double px; long long id, ts; char* s = line;
        if (std::sscanf(s, "%lld,%lf", &id, &px) != 2) continue;           // header
        int commas = 0; char* q = s; for (; *q && commas < 5; ++q) if (*q == ',') ++commas;
        if (commas < 5 || std::sscanf(q, "%lld", &ts) != 1) continue;
        r.pts.push_back({ts + kBinanceDelayMs, px});
    }
    pclose(p);
    std::stable_sort(r.pts.begin(), r.pts.end(), [](auto& a, auto& b) { return a.first < b.first; });
    return r;
}

// Cursor over a Ref: price usable at g, its move over the last w ms, and the gap to our mid less its 5-min median.
struct RefCursor {
    const Ref* r = nullptr;
    size_t j = 0;
    double px = NaN; int64_t at = -1;
    std::deque<std::pair<int64_t, double>> hist;          // (time, px), last 2 s
    std::deque<std::pair<int64_t, double>> gaps;           // (second, raw gap bps), last 5 min
    int64_t gap_sec = -1; double gap_med = NaN;
    void advance(int64_t g) {
        if (!r) return;
        while (j < r->pts.size() && r->pts[j].first <= g) { at = r->pts[j].first; px = r->pts[j].second; hist.push_back({at, px}); ++j; }
        while (hist.size() >= 2 && hist[1].first <= g - 2'000) hist.pop_front();
    }
    bool ok(int64_t g) const { return r && at >= 0 && g - at <= kStaleMs; }
    double move(int64_t g, int64_t w) const {             // bps change of the last price over [g − w, g]
        if (!ok(g) || hist.empty() || hist.front().first > g - w) return NaN;
        double then = hist.front().second;
        for (const auto& [t, p] : hist) { if (t <= g - w) then = p; else break; }
        return (px - then) / then * 1e4;
    }
    double gap(int64_t g, double mid) {                    // (ref − mid)/mid bps − its own trailing median
        if (!ok(g)) return NaN;
        const double raw = (px - mid) / mid * 1e4;
        if (g / 1000 != gap_sec) {
            gap_sec = g / 1000;
            gaps.push_back({gap_sec, raw});
            while (!gaps.empty() && gaps.front().first <= gap_sec - 300) gaps.pop_front();
            if (gaps.size() >= 60) {
                std::vector<double> v; v.reserve(gaps.size());
                for (const auto& x : gaps) v.push_back(x.second);
                std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
                gap_med = v[v.size() / 2];
            } else gap_med = NaN;
        }
        return std::isfinite(gap_med) ? raw - gap_med : NaN;
    }
};

static const std::vector<std::string> kFeat = {"micro1", "imb1", "imb3", "imb5", "flow1", "flow5", "ofi1",
                                               "ext_mv500", "ext_mv1000", "ext_gap", "hl_mv500", "hl_mv1000", "hl_gap",
                                               "bn_mv500", "bn_mv1000", "bn_gap"};
struct Sample { int64_t g; double mid, spread_ticks; std::array<double, 2> y; std::vector<double> x; };

static double wimb(const hl::Book& b, int n) {
    double bs = 0, as = 0, w = 1;
    for (int k = 0; k < n; ++k, w *= 0.5) {
        if (k < static_cast<int>(b.bids.size())) bs += w * b.bids[k].sz;
        if (k < static_cast<int>(b.asks.size())) as += w * b.asks[k].sz;
    }
    return bs + as > 0 ? (bs - as) / (bs + as) : 0;
}

static std::vector<Sample> build(const std::string& coin, const fs::path& root, const fs::path& bn_dir, int64_t w0, int64_t w1) {
    std::vector<Sample> out;
    const std::string day = day_of(w0);
    const fs::path lf = root / "lighter" / coin / (day + ".jsonl.zst");
    std::error_code ec;
    if (!fs::exists(lf, ec)) return out;
    auto ev = hl::load_archive(lf);
    hl::sort_events(ev);
    const Ref ext = ref_from_archive(root / "extended" / coin / (day + ".jsonl.zst"));
    const Ref hlr = ref_from_archive(root / "hyperliquid" / coin / (day + ".jsonl.zst"));
    const Ref bn = ref_from_binance(bn_dir / (coin + "USDT") / (coin + "USDT-aggTrades-" + day + ".zip"));
    RefCursor X, H, B;
    if (!ext.pts.empty()) X.r = &ext;
    if (!hlr.pts.empty()) H.r = &hlr;
    if (!bn.pts.empty()) B.r = &bn;
    // Lighter mid timeline for the targets
    std::vector<std::pair<int64_t, double>> mids;
    for (const auto& e : ev) if (e.is_book && e.book.valid()) mids.push_back({e.t_ms, e.book.mid()});
    auto mid_at = [&](int64_t t) -> double {
        auto it = std::upper_bound(mids.begin(), mids.end(), t, [](int64_t v, const auto& m) { return v < m.first; });
        return it == mids.begin() ? NaN : std::prev(it)->second;
    };
    hl::Book book; bool have = false;
    std::deque<std::tuple<int64_t, double, double>> flow;       // (t, buy $, sell $), last 5 s
    std::deque<std::pair<int64_t, double>> ofi;                 // (t, e_n), last 1 s
    double top_mean = 0; int64_t top_n = 0;
    size_t i = 0;
    for (int64_t g = (w0 / kGridMs + 1) * kGridMs; g + kH[1] < w1 + kH[1] && g < w1; g += kGridMs) {
        for (; i < ev.size() && ev[i].t_ms <= g; ++i) {
            const auto& e = ev[i];
            if (e.is_book) {
                if (!e.book.valid()) continue;
                if (have) {      // Cont–Kukanov–Stoikov: e = Δ demand at the bid − Δ supply at the ask
                    const auto &pb = book.bids[0], &pa = book.asks[0], &nb = e.book.bids[0], &na = e.book.asks[0];
                    const double en = (nb.px >= pb.px ? nb.sz : 0) - (nb.px <= pb.px ? pb.sz : 0)
                                    - (na.px <= pa.px ? na.sz : 0) + (na.px >= pa.px ? pa.sz : 0);
                    ofi.push_back({e.t_ms, en});
                }
                book = e.book; have = true;
                top_mean += (book.bids[0].sz + book.asks[0].sz) / 2; ++top_n;
            } else {
                const double usd = e.trade.px * e.trade.sz;
                flow.push_back({e.t_ms, e.trade.buy_aggr ? usd : 0, e.trade.buy_aggr ? 0 : usd});
            }
        }
        while (!flow.empty() && std::get<0>(flow.front()) <= g - 5'000) flow.pop_front();
        while (!ofi.empty() && ofi.front().first <= g - 1'000) ofi.pop_front();
        X.advance(g); H.advance(g); B.advance(g);
        if (!have) continue;
        const double m0 = book.mid();
        Sample s; s.g = g; s.mid = m0;
        const double tick = std::max(book.tick(), 1e-12);
        s.spread_ticks = (book.asks[0].px - book.bids[0].px) / tick;
        bool ok = true;
        for (int k = 0; k < 2; ++k) { const double m1 = mid_at(g + kH[k]); s.y[k] = std::isfinite(m1) ? (m1 - m0) / m0 * 1e4 : NaN; ok &= std::isfinite(s.y[k]); }
        if (!ok || g + kH[1] > w1) continue;                       // the target must stay inside the window
        double b1 = 0, s1 = 0, b5 = 0, s5 = 0;
        for (const auto& [t, b, sl] : flow) { b5 += b; s5 += sl; if (t > g - 1'000) { b1 += b; s1 += sl; } }
        double o = 0; for (const auto& x : ofi) o += x.second;
        s.x = {(book.microprice() - m0) / m0 * 1e4, wimb(book, 1), wimb(book, 3), wimb(book, 5),
               b1 + s1 > 0 ? (b1 - s1) / (b1 + s1) : 0, b5 + s5 > 0 ? (b5 - s5) / (b5 + s5) : 0,
               top_n ? o / (top_mean / top_n) : 0,
               X.move(g, 500), X.move(g, 1000), X.gap(g, m0), H.move(g, 500), H.move(g, 1000), H.gap(g, m0),
               B.move(g, 500), B.move(g, 1000), B.gap(g, m0)};
        out.push_back(std::move(s));
    }
    return out;
}

// ── scoring ─────────────────────────────────────────────────────────────────────────────────────────────────────
struct Score { double sse = 0, yy = 0, n = 0, sxy = 0, sxx = 0; std::vector<std::pair<double, double>> py; };
static void ols1(const std::vector<std::pair<double, double>>& xy, double& a, double& b) {
    double n = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (const auto& [x, y] : xy) { n += 1; sx += x; sy += y; sxx += x * x; sxy += x * y; }
    const double d = n * sxx - sx * sx;
    b = (n > 2 && std::abs(d) > 1e-12) ? (n * sxy - sx * sy) / d : 0;
    a = n > 0 ? (sy - b * sx) / n : 0;
}
// small ridge OLS (normal equations, Gauss-Jordan); x already standardised on the fit part
static std::vector<double> ridge(const std::vector<std::vector<double>>& X, const std::vector<double>& y, double lam) {
    const size_t k = X.empty() ? 0 : X[0].size() + 1;
    std::vector<std::vector<double>> A(k, std::vector<double>(k + 1, 0));
    for (size_t r = 0; r < X.size(); ++r) {
        std::vector<double> z(k, 1.0); for (size_t c = 1; c < k; ++c) z[c] = X[r][c - 1];
        for (size_t a = 0; a < k; ++a) { for (size_t b = 0; b < k; ++b) A[a][b] += z[a] * z[b]; A[a][k] += z[a] * y[r]; }
    }
    for (size_t a = 1; a < k; ++a) A[a][a] += lam * X.size();
    for (size_t c = 0; c < k; ++c) {
        size_t p = c; for (size_t r = c + 1; r < k; ++r) if (std::abs(A[r][c]) > std::abs(A[p][c])) p = r;
        std::swap(A[c], A[p]);
        if (std::abs(A[c][c]) < 1e-12) continue;
        for (size_t r = 0; r < k; ++r) if (r != c) { const double f = A[r][c] / A[c][c]; for (size_t j = c; j <= k; ++j) A[r][j] -= f * A[c][j]; }
    }
    std::vector<double> w(k, 0); for (size_t c = 0; c < k; ++c) w[c] = std::abs(A[c][c]) > 1e-12 ? A[c][k] / A[c][c] : 0;
    return w;
}
static void add(Score& s, double pred, double y) { s.sse += (y - pred) * (y - pred); s.yy += y * y; s.n += 1; s.sxy += pred * y; s.sxx += pred * pred; s.py.push_back({pred, y}); }
static json finish(Score& s) {
    if (s.n < 100) return json{{"n", s.n}};
    std::sort(s.py.begin(), s.py.end(), [](auto& a, auto& b) { return std::abs(a.first) > std::abs(b.first); });
    const size_t top = std::max<size_t>(1, s.py.size() / 10);
    double ap = 0, ry = 0, hit = 0;
    for (size_t i = 0; i < top; ++i) { ap += std::abs(s.py[i].first); ry += (s.py[i].first > 0 ? 1 : -1) * s.py[i].second; hit += (s.py[i].first * s.py[i].second > 0); }
    const double corr = s.sxx > 0 && s.yy > 0 ? s.sxy / std::sqrt(s.sxx * s.yy) : 0;
    return {{"n", s.n}, {"r2_pct", 100 * (1 - s.sse / s.yy)}, {"corr", corr}, {"top10_pred_bps", ap / top},
            {"top10_realised_bps", ry / top}, {"top10_hit", hit / top}};
}

int main(int argc, char** argv) {
    fs::path root = "/media/theone/381C48C51C48803C/rithmic_dex", bn_dir = "data/history/binance", out;
    std::vector<std::string> coins;
    std::string from = "2026-10-26T00:00", to = "2026-10-26T23:00", split = "2026-10-26T12:00";
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto val = [&]() -> std::string { if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", a.c_str()); std::exit(2); } return argv[++i]; };
        if (a == "--root") root = val();
        else if (a == "--binance") bn_dir = val();
        else if (a == "--coins") { std::stringstream ss(val()); for (std::string c; std::getline(ss, c, ',');) if (!c.empty()) coins.push_back(c); }
        else if (a == "--from") from = val();
        else if (a == "--to") to = val();
        else if (a == "--split") split = val();
        else if (a == "--out") out = val();
        else if (a == "--binance-delay-ms") kBinanceDelayMs = std::atoll(val().c_str());
        else { std::fprintf(stderr, "unknown argument %s\n", a.c_str()); return 2; }
    }
    const int64_t w0 = parse_iso(from), w1 = parse_iso(to), ws = parse_iso(split);
    if (coins.empty() || w0 < 0 || w1 <= w0 || ws <= w0 || ws >= w1 || day_of(w0) != day_of(w1 - 1)) {
        std::fprintf(stderr, "usage: mm_fairlab --coins A,B,… [--from T --to T --split T] (one UTC day) [--root DIR] [--binance DIR] [--out FILE]\n");
        return 2;
    }
    const size_t F = kFeat.size();
    // models: each feature alone, micro1 as used (β = 1), fitted microprice, all together
    std::map<std::string, std::array<Score, 2>> sc;
    json per_coin = json::object();
    for (const auto& coin : coins) {
        const auto S = build(coin, root, bn_dir, w0, w1);
        if (S.size() < 1000) { std::fprintf(stderr, "%s: %zu samples, skipped\n", coin.c_str(), S.size()); continue; }
        std::vector<const Sample*> fit, test;
        for (const auto& s : S) (s.g < ws ? fit : test).push_back(&s);
        std::array<int, 16> avail{};
        for (size_t f = 0; f < F; ++f) { int n = 0; for (auto* s : fit) n += std::isfinite(s->x[f]); avail[f] = n > static_cast<int>(fit.size()) / 2; }
        std::fprintf(stderr, "%s: %zu fit / %zu test samples; ext %s, hl %s, binance %s\n", coin.c_str(), fit.size(), test.size(),
                     avail[7] ? "yes" : "no", avail[10] ? "yes" : "no", avail[13] ? "yes" : "no");
        json cj = json::object();
        for (int h = 0; h < 2; ++h) {
            // single features
            for (size_t f = 0; f < F; ++f) {
                if (!avail[f]) continue;
                std::vector<std::pair<double, double>> xy;
                for (auto* s : fit) if (std::isfinite(s->x[f])) xy.push_back({s->x[f], s->y[h]});
                double a, b; ols1(xy, a, b);
                Score one;
                for (auto* s : test) if (std::isfinite(s->x[f])) { add(sc[kFeat[f]][h], a + b * s->x[f], s->y[h]); add(one, a + b * s->x[f], s->y[h]); }
                cj[kFeat[f]][h ? "10s" : "1s"] = finish(one);
            }
            // micro1 exactly as the strategy uses it: fair = microprice (β = 1, no fit)
            for (auto* s : test) add(sc["micro1_as_used"][h], s->x[0], s->y[h]);
            // fitted microprice: mean target per imbalance decile × spread bucket (1, 2, 3, 4+ ticks) on the fit part
            {
                std::map<std::pair<int, int>, std::pair<double, double>> cell;
                auto key = [](const Sample* s) { return std::make_pair(std::clamp(static_cast<int>((s->x[1] + 1) * 5), 0, 9),
                                                                       std::clamp(static_cast<int>(std::lround(s->spread_ticks)), 1, 4)); };
                for (auto* s : fit) { auto& c = cell[key(s)]; c.first += s->y[h]; c.second += 1; }
                for (auto* s : test) { auto it = cell.find(key(s)); const double p = it != cell.end() && it->second.second >= 30 ? it->second.first / it->second.second : 0; add(sc["micro_fitted"][h], p, s->y[h]); }
            }
            // all available features, standardised on the fit part, ridge λ = 0.01
            {
                std::vector<size_t> use; for (size_t f = 0; f < F; ++f) if (avail[f]) use.push_back(f);
                std::vector<double> mu(use.size(), 0), sd(use.size(), 0);
                for (size_t u = 0; u < use.size(); ++u) {
                    double n = 0; for (auto* s : fit) if (std::isfinite(s->x[use[u]])) { mu[u] += s->x[use[u]]; n += 1; }
                    mu[u] /= std::max(1.0, n);
                    for (auto* s : fit) if (std::isfinite(s->x[use[u]])) sd[u] += (s->x[use[u]] - mu[u]) * (s->x[use[u]] - mu[u]);
                    sd[u] = std::sqrt(sd[u] / std::max(1.0, n)); if (sd[u] <= 0) sd[u] = 1;
                }
                auto z = [&](const Sample* s) { std::vector<double> v(use.size()); for (size_t u = 0; u < use.size(); ++u) { const double x = s->x[use[u]]; v[u] = std::isfinite(x) ? (x - mu[u]) / sd[u] : 0; } return v; };
                std::vector<std::vector<double>> X; std::vector<double> y;
                for (auto* s : fit) { X.push_back(z(s)); y.push_back(s->y[h]); }
                const auto w = ridge(X, y, 0.01);
                Score one;
                for (auto* s : test) { const auto v = z(s); double p = w[0]; for (size_t u = 0; u < v.size(); ++u) p += w[u + 1] * v[u]; add(sc["all_features"][h], p, s->y[h]); add(one, p, s->y[h]); }
                cj["all_features"][h ? "10s" : "1s"] = finish(one);
            }
        }
        per_coin[coin] = cj;
    }
    json pooled = json::object();
    for (auto& [m, arr] : sc) pooled[m] = {{"1s", finish(arr[0])}, {"10s", finish(arr[1])}};
    // report: models sorted by pooled 10 s R²
    std::vector<std::pair<double, std::string>> order;
    for (auto& [m, v] : pooled.items()) order.push_back({v["10s"].value("r2_pct", -1e9), m});
    std::sort(order.rbegin(), order.rend());
    std::printf("# mm_fairlab — fair-price models scored on predicting Lighter's mid\n\nfit %s → %s, scored %s → %s, %zu coins requested, 250 ms grid\n\n",
                from.c_str(), split.c_str(), split.c_str(), to.c_str(), coins.size());
    std::printf("| model | samples | R² 1 s %% | corr 1 s | R² 10 s %% | corr 10 s | top-10%% predicted 10 s bps | realised | hit |\n|---|---|---|---|---|---|---|---|---|\n");
    for (auto& [_, m] : order) {
        const auto& a = pooled[m]["1s"]; const auto& b = pooled[m]["10s"];
        if (!b.contains("r2_pct")) continue;
        std::printf("| %s | %.0f | %.3f | %.3f | %.3f | %.3f | %.2f | %.2f | %.0f%% |\n", m.c_str(), b["n"].get<double>(), a["r2_pct"].get<double>(), a["corr"].get<double>(),
                    b["r2_pct"].get<double>(), b["corr"].get<double>(), b["top10_pred_bps"].get<double>(), b["top10_realised_bps"].get<double>(), 100 * b["top10_hit"].get<double>());
    }
    if (!out.empty()) std::ofstream(out) << json{{"from", from}, {"to", to}, {"split", split}, {"pooled", pooled}, {"per_coin", per_coin}}.dump(1) << "\n";
    return 0;
}
