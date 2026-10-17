// synth_market — synthetic NQ / ES 1-minute data with a KNOWN planted edge, for validating the edge_lab pipeline
// (scripts/edge_lab_validate.sh). A random walk (no edge anywhere) plus one planted effect: on days whose trading day of
// the month is even, price drifts up `edge` points between 10:00 and 15:55 ET. A working pipeline must PASS the idea
// that trades exactly that, and reject noise ideas run on the same data.
//
//   synth_market OUT_DIR [edge_pts=8] [seed=7]      writes OUT_DIR/{NQ,ES}.csv + fomc.txt (edge_lab cache format)
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <random>
#include <string>

static int64_t wall(int y, int m, int d, int h, int mi) {   // ET wall clock as epoch seconds (edge_lab's convention)
    struct tm g{}; g.tm_year = y - 1900; g.tm_mon = m - 1; g.tm_mday = d; g.tm_hour = h; g.tm_min = mi; return (int64_t)timegm(&g);
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: synth_market OUT_DIR [edge_pts] [seed]\n"); return 2; }
    const std::string out = argv[1];
    const double edge = argc > 2 ? std::atof(argv[2]) : 8.0;
    const unsigned seed = argc > 3 ? (unsigned)std::atoi(argv[3]) : 7u;
    for (int sym = 0; sym < 2; ++sym) {
        const char* name = sym == 0 ? "NQ" : "ES";
        const double p0 = sym == 0 ? 10000.0 : 2500.0, sd = sym == 0 ? 3.0 : 0.75, plant = sym == 0 ? edge : edge / 4;
        std::mt19937_64 rng(seed * 1000003ULL + sym);
        std::normal_distribution<double> n01(0, 1);
        FILE* f = std::fopen((out + "/" + name + ".csv").c_str(), "w");
        if (!f) { std::perror("open"); return 1; }
        double px = p0;
        int month = 0, tdom = 0;
        for (int64_t day = wall(2010, 7, 1, 12, 0); day <= wall(2026, 10, 16, 12, 0); day += 86400) {
            time_t tt = (time_t)day; struct tm g; gmtime_r(&tt, &g);
            if (g.tm_wday == 0 || g.tm_wday == 6) continue;                     // weekdays only, no holidays
            if (g.tm_mon != month) { month = g.tm_mon; tdom = 0; }
            ++tdom;
            const bool planted = tdom % 2 == 0;
            const int64_t start = day - 12 * 3600 - 6 * 3600;                 // 18:00 the evening before
            const int64_t t1000 = day - 12 * 3600 + 10 * 3600, t1555 = day - 12 * 3600 + 15 * 3600 + 55 * 60;
            const double per_bar = plant / ((t1555 - t1000) / 60.0);
            for (int64_t t = start; t < start + 23 * 3600; t += 60) {          // 18:00 → 17:00
                const int hm = (int)((t % 86400) / 60);
                const bool rth = hm >= 570 && hm < 960;
                const double s = sd * (rth ? 1.0 : 0.45);
                const double o = px;
                double c = o + s * n01(rng) + (planted && t >= t1000 && t < t1555 ? per_bar : 0.0);
                c = std::round(c * 4) / 4;
                const double h = std::max(o, c) + std::round(std::fabs(n01(rng)) * s * 0.5 * 4) / 4;
                const double l = std::min(o, c) - std::round(std::fabs(n01(rng)) * s * 0.5 * 4) / 4;
                std::fprintf(f, "%lld,%.2f,%.2f,%.2f,%.2f,%d,0\n", (long long)t, o, h, l, c, rth ? 1000 : 100);
                px = c;
            }
        }
        std::fclose(f);
    }
    if (FILE* f = std::fopen((out + "/fomc.txt").c_str(), "w")) std::fclose(f);   // no events: nothing else to explain a result
    std::printf("synthetic NQ / ES written to %s (planted edge %.1f pts on even trading days, 10:00 → 15:55)\n", out.c_str(), edge);
    return 0;
}
