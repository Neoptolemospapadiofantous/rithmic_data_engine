// 2026-10-22 flag_matrix_export.cpp (founder: "matrix with these and minutes chart", "and filters"). Writes the data of the Flag Matrix page:
// every flag with a trade, 2014-01 → 2026-10, from flag_hhhl.cpp `export FILE wide15` (1-5 minute charts, pole >= 2× the average bar, pullback
// 5-70 %, grades fitted on that set), NQ and ES, as column arrays: date, chart, side, grade, 5m HH/HL, 5m opening-range break (+1 with / −1
// against / 0 none yet), registration minute, pole / average bar ×10, pole / ATR ×100, pullback % , gap with the trade (+1 / −1 / 0), prior VIX,
// last 5 sessions down (1/0), ATR rising (1/0), first pullback of the day (1/0), entry and exit minute, R ×1000 (flag-mid trade, after cost).
// Usage: flag_matrix_export NQ.csv ES.csv OUT.json. Read-only.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
int main(int argc, char** argv) {
  if (argc < 4) { fprintf(stderr, "usage: flag_matrix_export NQ.csv ES.csv OUT.json\n"); return 1; }
  FILE* fo = fopen(argv[3], "w"); if (!fo) { perror(argv[3]); return 1; } fprintf(fo, "{");
  static const char* KEY[17] = {"d", "tf", "s", "g", "h", "o", "t", "m", "a", "p", "gp", "v", "d5", "au", "fp", "em", "r"};
  for (int mk = 0; mk < 2; ++mk) { std::ifstream f(argv[1 + mk]); if (!f) { perror(argv[1 + mk]); return 1; } std::string ln; std::getline(f, ln); std::vector<std::vector<long>> col(18);
    while (std::getline(f, ln)) { std::vector<double> a; const char* p = ln.c_str(); char* e; while (*p) { a.push_back(strtod(p, &e)); if (e == p) break; p = e; if (*p == ',') ++p; else break; }
      if (a.size() < 55 || a[11] < 0.5 || a[0] < 20140101) continue; const int side = (int)a[1];
      const long v[18] = {(long)a[0], (long)a[2], side, (long)a[6], (long)a[7], (long)a[47], (long)a[3], lround(a[22] * 10), lround(a[23] * 100), lround(a[24] * 100),
                          a[25] > 1e-9 ? 1 : a[25] < -1e-9 ? -1 : 0, lround(a[50]), a[48] * side < 0 ? 1 : 0, a[51] > 1 ? 1 : 0, (long)a[29], (long)a[12], lround(a[18] * 1000), (long)a[13]};
      for (int k = 0; k < 18; ++k) col[k].push_back(v[k]); }
    fprintf(fo, "%s\"%s\":{\"n\":%zu", mk ? "," : "", mk ? "ES" : "NQ", col[0].size());
    for (int k = 0; k < 18; ++k) { fprintf(fo, ",\"%s\":[", k < 17 ? KEY[k] : "xm"); for (size_t i = 0; i < col[k].size(); ++i) fprintf(fo, "%s%ld", i ? "," : "", col[k][i]); fprintf(fo, "]"); }
    fprintf(fo, "}"); fprintf(stderr, "%s: %zu flags\n", mk ? "ES" : "NQ", col[0].size()); }
  fprintf(fo, "}\n"); fclose(fo); return 0;
}
