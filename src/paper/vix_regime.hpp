// vix_regime.hpp — the VIX regime the hold-mode gates read (TrendConfig::hold_vix_*), from CBOE's daily history
// files in data/regime/ (scripts/vix_fetch.sh). Research 2026-10-11: buying weakness at the daily horizon is worth
// ≈ 0 when VIX < 15 and 3–8× its average when VIX ≥ 20, rose > 10 % in 5 days, or sits in the top tercile of its
// year — monotonic and in every year 2010–2026 (NQ and ES). Fail-open: no file or no row → "unknown", and a gated
// variant does not enter. The row used for a trading day is the LAST close strictly before it (the signal day's).
#pragma once
#include <sys/stat.h>
#include <algorithm>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
namespace paper {
struct VixDay { double level = 0, vix3m = 0, chg5 = 0, chg1 = 0, pct = 0; bool known = false; };
class VixRegime {
public:
    // Loads VIX_History.csv (+ VIX3M_History.csv when present). CBOE format: DATE(MM/DD/YYYY),OPEN,HIGH,LOW,CLOSE.
    bool load(const std::string& dir) {
        rows_.clear();
        std::map<std::string, double> v3;
        read(dir + "/VIX3M_History.csv", [&](const std::string& d, double c) { v3[d] = c; });
        std::vector<std::pair<std::string, double>> v;
        if (!read(dir + "/VIX_History.csv", [&](const std::string& d, double c) { v.emplace_back(d, c); })) return false;
        std::sort(v.begin(), v.end());
        for (size_t i = 0; i < v.size(); ++i) {
            VixDay x; x.level = v[i].second; x.known = true;
            auto it = v3.find(v[i].first); x.vix3m = it == v3.end() ? 0 : it->second;
            x.chg5 = i >= 5 && v[i - 5].second > 0 ? v[i].second / v[i - 5].second - 1.0 : 0;
            x.chg1 = i >= 1 && v[i - 1].second > 0 ? v[i].second / v[i - 1].second - 1.0 : 0;
            const size_t from = i >= 251 ? i - 251 : 0; size_t below = 0, n = 0;   // rank within the trailing 252 closes
            for (size_t k = from; k <= i; ++k) { ++n; if (v[k].second <= v[i].second) ++below; }
            x.pct = n ? (double)below / n : 0;
            rows_[v[i].first] = x;
        }
        return !rows_.empty();
    }
    // Regime for a trading day: the last close strictly before `ymd` (YYYY-MM-DD). A close more than 5 calendar days
    // older than `ymd` (a weekend + holiday is at most 4) means the file is stale → unknown, so gated variants stay
    // out instead of trading on an old regime (2026-10-14: the fetch ran before the close and the file was read once).
    VixDay before(const std::string& ymd) const {
        auto it = rows_.lower_bound(ymd);
        if (it == rows_.begin()) return VixDay{};
        --it;
        if (days_between(it->first, ymd) > 5) return VixDay{};
        return it->second;
    }
    size_t size() const { return rows_.size(); }
    // Reload when the CSV changed on disk (scripts/vix_fetch.sh replaces it); cheap stat otherwise. True = reloaded.
    bool reload_if_changed(const std::string& dir) {
        struct stat st{};
        if (::stat((dir + "/VIX_History.csv").c_str(), &st) != 0) return false;
        if (st.st_mtime == loaded_mtime_) return false;
        if (!load(dir)) return false;
        loaded_mtime_ = st.st_mtime; return true;
    }
    static int days_between(const std::string& a, const std::string& b) {   // YYYY-MM-DD, b − a in days
        auto t = [](const std::string& s) { struct tm m{}; m.tm_year = std::stoi(s.substr(0, 4)) - 1900; m.tm_mon = std::stoi(s.substr(5, 2)) - 1;
                                              m.tm_mday = std::stoi(s.substr(8, 2)); m.tm_hour = 12; return timegm(&m); };
        return (int)((t(b) - t(a)) / 86400);
    }
private:
    time_t loaded_mtime_ = 0;
    template <class F> static bool read(const std::string& path, F&& on_row) {
        FILE* f = std::fopen(path.c_str(), "r"); if (!f) return false;
        char line[256]; int n = 0;
        while (std::fgets(line, sizeof line, f)) {
            int mo, dd, yy; double o, h, l, c;
            if (std::sscanf(line, "%d/%d/%d,%lf,%lf,%lf,%lf", &mo, &dd, &yy, &o, &h, &l, &c) == 7 && c > 0) {
                char buf[16]; std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", yy, mo, dd); on_row(buf, c); ++n;
            }
        }
        std::fclose(f); return n > 0;
    }
    std::map<std::string, VixDay> rows_;
};
}  // namespace paper
