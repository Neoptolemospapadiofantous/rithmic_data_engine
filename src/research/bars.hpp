// bars.hpp — edge_lab's market data: one symbol's 1-minute bars (ET wall clock) split into prop trading days
// (18:00 → 17:00 ET) with the day-level facts a strategy may know before it trades.
//
// Input: scripts/edge_lab.sh exports bars_1m to data/edge_lab/cache/<SYM>.csv
//   t,open,high,low,close,volume,buy_volume     (t = ET wall-clock time as epoch seconds, i.e. "UTC" = ET)
// and the first load writes <SYM>.bin next to it (rebuilt when the CSV is newer).
//
// Day facts are known at the 18:00 open of the trading day (all from earlier days): RTH = 09:30–16:00 ET.
//   pc ph pl pc2      prior RTH close / high / low, close two days back
//   atr               mean of the prior 14 RTH ranges (reset after a > 7-day hole)
//   pibs              prior day (pc − pl) / (ph − pl)
//   sma50 rsi2        of prior RTH closes
//   vix vixc1 vixc5 vixpct   CBOE VIX close of the last day before (data/regime, paper::VixRegime)
//   dow tdom tdleft   weekday 1–5, trading day of month (1-based), trading days left in the month after today
//   fomc opex nfp rollwin    FOMC decision day (calendar table), 3rd Friday, 1st Friday, ±roll window
//   mtd prehol        month-to-date return to the prior close; next session > 1 weekday away (holiday ahead)
// Prices are the continuous front contract, NOT back-adjusted: compare prices across days only away from a roll
// (rollwin = roll Thursday − 3 … + 6 calendar days, the research convention).
#pragma once
#include <sys/stat.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "../paper/vix_regime.hpp"

namespace lab {

constexpr int kRthOpen = 930;    // minutes into the trading day (18:00 = 0): 09:30 ET
constexpr int kRthClose = 1320;  // 16:00 ET
constexpr int kDayEnd = 1380;    // 17:00 ET

inline int tday_min(int64_t t) { return (int)(((t % 86400) / 60 - 1080 + 1440) % 1440); }   // 18:00 = 0
inline int hm_to_tday(int h, int m) { return ((h * 60 + m) - 1080 + 1440) % 1440; }
inline int parse_hhmm(const std::string& s) {                                              // "HH:MM" → trading-day minute
    int h = 0, m = 0;
    if (std::sscanf(s.c_str(), "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59)
        throw std::runtime_error("bad time '" + s + "' (want HH:MM ET)");
    return hm_to_tday(h, m);
}
inline int ymd_of(int64_t t) {   // calendar date of an ET wall-clock epoch
    time_t tt = (time_t)t; struct tm g{}; gmtime_r(&tt, &g);
    return (g.tm_year + 1900) * 10000 + (g.tm_mon + 1) * 100 + g.tm_mday;
}
inline int weekday_of(int ymd) {   // 0 = Sunday
    struct tm g{}; g.tm_year = ymd / 10000 - 1900; g.tm_mon = ymd / 100 % 100 - 1; g.tm_mday = ymd % 100; g.tm_hour = 12;
    time_t t = timegm(&g); gmtime_r(&t, &g); return g.tm_wday;
}
inline int add_days(int ymd, int d) {
    struct tm g{}; g.tm_year = ymd / 10000 - 1900; g.tm_mon = ymd / 100 % 100 - 1; g.tm_mday = ymd % 100 + d; g.tm_hour = 12;
    time_t t = timegm(&g); gmtime_r(&t, &g); return (g.tm_year + 1900) * 10000 + (g.tm_mon + 1) * 100 + g.tm_mday;
}
inline int64_t timegm_ymd(int ymd) {
    struct tm g{}; g.tm_year = ymd / 10000 - 1900; g.tm_mon = ymd / 100 % 100 - 1; g.tm_mday = ymd % 100; g.tm_hour = 12;
    return (int64_t)timegm(&g);
}
inline std::string ymd_str(int ymd) {
    char b[16]; std::snprintf(b, sizeof b, "%04d-%02d-%02d", ymd / 10000, ymd / 100 % 100, ymd % 100); return b;
}

struct Day {
    int date = 0;                 // YYYYMMDD of the RTH session (the trading day ends 17:00 that date)
    int b0 = 0, b1 = 0;           // bars [b0, b1)
    int rth0 = -1, rth1 = -1;     // first bar ≥ 09:30, first bar ≥ 16:00 (or b1)
    bool ok = false;              // has a full RTH session (≥ 350 RTH bars)
    double ro = NAN, rh = NAN, rl = NAN, rc = NAN;
    bool flow = false;            // aggressor volume present (buy_volume > 0)
    // known before the day
    double pc = NAN, ph = NAN, pl = NAN, pc2 = NAN, atr = NAN, pibs = NAN, sma50 = NAN, rsi2 = NAN;
    double vix = NAN, vixc1 = NAN, vixc5 = NAN, vixpct = NAN, vts = NAN;   // vts = VIX / VIX3M (prior close)
    int dow = 0, tdom = 0, tdleft = 0;
    bool fomc = false, opex = false, nfp = false, rollwin = false;
    double mtd = NAN;             // month-to-date return through the prior RTH close (fraction of the last close of the previous month)
    // liquidity levels known before the day (2026-10-16): yesterday's RTH volume profile (busiest price, 70 % value area),
    // last calendar week's / month's RTH high and low, the nearest "naked" busiest price of the last 20 sessions (not
    // traded through since) above / below today's 09:30 open. Volume is spread evenly over each 1-minute bar's range.
    double ppoc = NAN, pvah = NAN, pval = NAN, pwh = NAN, pwl = NAN, pmh = NAN, pml = NAN, npoca = NAN, npocb = NAN;
    // trend-follower triggers (2026-10-16): 100- / 200-day averages of RTH closes, and xma = the net sign of yesterday's
    // close crossing the 50 / 100 / 200-day average (+1 per upward cross, −1 per downward), all from earlier days
    double sma100 = NAN, sma200 = NAN; int xma = 0;
    // free daily macro / options data (load_macro, data/edge_lab/cache/macro.csv), last observation dated before the day:
    // VVIX, SKEW, 10- and 2-year Treasury yields (%), y10c = the 10-year's last daily change in bp, usdc = the broad dollar
    // index's last daily change in %
    double vvix = NAN, skew = NAN, y10 = NAN, y2 = NAN, y10c = NAN, usdc = NAN;
    bool earn = false;            // a big-tech earnings reaction session (load_events, events.csv kind "earn")
    bool cpi = false, ppi = false; // a CPI / PPI release day, 08:30 ET (load_events, releases.csv from the calendar table)
    int64_t pct = 0;             // wall-clock time of the prior ok day's RTH close (end of its last RTH bar), 0 = none
    double poc = NAN, vah = NAN, val = NAN;   // this day's own profile (feeds the next day's p* and the naked list)
    bool prehol = false;          // the next session is more than one weekday away (exchange holiday follows; schedule is public)
    // Wyckoff phase inputs (2026-10-18, research/preregistered/2026-10-18_wyckoff_phases.md), from the last ok sessions' RTH
    // closes C and volumes: e20 = |C[-1] − C[-21]| / Σ|ΔC| over those 20 steps (1 = straight line, ~0.2 = random walk),
    // r20 = (C[-1] − C[-21]) / atr, r60 = (C[-21] − C[-61]) / atr (the 40 sessions before that window), uv20 = the share of
    // the last 20 sessions' RTH volume traded on up-close sessions (close > the session before), p20 = where C[-1] sits in
    // the last 20 closes' min–max (0 = at the low, 1 = at the high)
    double e20 = NAN, r20 = NAN, r60 = NAN, uv20 = NAN, p20 = NAN;
    // the same daily path / direction over 5 and 10 sessions (2026-10-18_daily_path_direction): eN = |C[-1] − C[-1-N]| / Σ|ΔC|,
    // rN = (C[-1] − C[-1-N]) / atr
    double e5 = NAN, r5 = NAN, e10 = NAN, r10 = NAN;
};

struct Series {
    std::string sym;
    std::vector<int64_t> t;
    std::vector<float> o, h, l, c, v, bv;
    // Order-book minutes (load_book, optional; NaN where absent): qty imbalance (bid − ask)/(bid + ask) of the resting
    // size within 10 bps of the price, cancel-to-add ratio, resting bid / ask size within 10 bps. Same index as t.
    std::vector<float> qimb, cxl, bidq, askq;
    std::vector<Day> days;
    std::vector<int> day_of;      // bar → day index
    size_t size() const { return t.size(); }
};

namespace detail {
inline bool newer(const std::string& a, const std::string& b) {   // a exists and is newer than b (or b missing)
    struct stat sa{}, sb{};
    if (::stat(a.c_str(), &sa) != 0) return false;
    if (::stat(b.c_str(), &sb) != 0) return true;
    return sa.st_mtime > sb.st_mtime;
}
constexpr uint32_t kMagic = 0x45444731;   // "EDG1"
template <class T> void wr(FILE* f, const std::vector<T>& v) { std::fwrite(v.data(), sizeof(T), v.size(), f); }
template <class T> bool rd(FILE* f, std::vector<T>& v, size_t n) { v.resize(n); return std::fread(v.data(), sizeof(T), n, f) == n; }
}  // namespace detail

// <SYM>.book.csv (scripts/edge_lab.sh export, from mbo_1m): t,qimb,cxl,bidq,askq — t as in <SYM>.csv. Absent file =
// no book (the arrays stay NaN). Returns the number of minutes matched to a bar.
inline size_t load_book(const std::string& dir, const std::string& sym, Series& s) {
    const size_t n = s.size();
    s.qimb.assign(n, NAN); s.cxl.assign(n, NAN); s.bidq.assign(n, NAN); s.askq.assign(n, NAN);
    FILE* f = std::fopen((dir + "/" + sym + ".book.csv").c_str(), "r");
    if (!f) return 0;
    char line[256]; long long t; double qi, cx, bq, aq; size_t hit = 0;
    while (std::fgets(line, sizeof line, f)) {
        if (std::sscanf(line, "%lld,%lf,%lf,%lf,%lf", &t, &qi, &cx, &bq, &aq) != 5) continue;
        auto it = std::lower_bound(s.t.begin(), s.t.end(), (int64_t)t);
        if (it == s.t.end() || *it != t) continue;
        const size_t i = it - s.t.begin();
        s.qimb[i] = (float)qi; s.cxl[i] = (float)cx; s.bidq[i] = (float)bq; s.askq[i] = (float)aq; ++hit;
    }
    std::fclose(f);
    return hit;
}

// macro.csv (scripts/edge_lab.sh macro): YYYYMMDD,dgs10,dgs2,usd,vvix,skew — empty field = no observation that day.
// Each Day gets the last observation dated strictly before its date. Returns the number of days given a 10-year yield.
inline size_t load_macro(const std::string& dir, Series& s) {
    FILE* f = std::fopen((dir + "/macro.csv").c_str(), "r");
    if (!f) return 0;
    std::vector<std::pair<int, double>> col[5];
    char line[256];
    while (std::fgets(line, sizeof line, f)) {
        int d = 0; char* p = line;
        d = (int)std::strtol(p, &p, 10);
        for (int c = 0; c < 5 && *p == ','; ++c) {
            ++p;
            if (*p == ',' || *p == '\n' || *p == '\0') continue;
            char* e; const double v = std::strtod(p, &e);
            if (e != p) { col[c].emplace_back(d, v); p = e; }
        }
    }
    std::fclose(f);
    for (auto& c : col) std::sort(c.begin(), c.end());
    auto before = [&](int c, int date, int back) -> double {   // value `back` observations before the last one < date
        auto it = std::lower_bound(col[c].begin(), col[c].end(), std::make_pair(date, -1e300));
        const long idx = (long)(it - col[c].begin()) - 1 - back;
        return idx >= 0 ? col[c][idx].second : NAN;
    };
    size_t n = 0;
    for (auto& d : s.days) {
        d.y10 = before(0, d.date, 0); d.y2 = before(1, d.date, 0); d.vvix = before(3, d.date, 0); d.skew = before(4, d.date, 0);
        const double y10p = before(0, d.date, 1), u = before(2, d.date, 0), up = before(2, d.date, 1);
        d.y10c = (d.y10 - y10p) * 100.0;
        d.usdc = up > 0 ? (u / up - 1.0) * 100.0 : NAN;
        if (!std::isnan(d.y10)) ++n;
    }
    return n;
}

// events.csv: YYYYMMDD,kind per line — "earn" marks a big-tech earnings reaction session (built from SEC 8-K item 2.02
// filings). Returns the number of matched days. Absent file = no events.
inline size_t load_events(const std::string& dir, Series& s) {
    std::set<int> earn; char line[128]; int d; char kind[32];
    if (FILE* f = std::fopen((dir + "/events.csv").c_str(), "r")) {
        while (std::fgets(line, sizeof line, f)) if (std::sscanf(line, "%d,%31s", &d, kind) == 2 && std::strcmp(kind, "earn") == 0) earn.insert(d);
        std::fclose(f);
    }
    size_t n = 0;
    for (auto& day : s.days) if (earn.count(day.date)) { day.earn = true; ++n; }
    // releases.csv (scripts/edge_lab.sh export: calendar kinds cpi / ppi / nfp, BLS archive). When it lists nfp days they
    // replace the first-Friday approximation from build_days (real Employment Situation days, incl. shutdown moves).
    if (FILE* g = std::fopen((dir + "/releases.csv").c_str(), "r")) {
        std::set<int> cpi, ppi, nfp;
        while (std::fgets(line, sizeof line, g)) if (std::sscanf(line, "%d,%31s", &d, kind) == 2) {
            if (std::strcmp(kind, "cpi") == 0) cpi.insert(d); else if (std::strcmp(kind, "ppi") == 0) ppi.insert(d); else if (std::strcmp(kind, "nfp") == 0) nfp.insert(d);
        }
        std::fclose(g);
        for (auto& day : s.days) {
            day.cpi = cpi.count(day.date) > 0; day.ppi = ppi.count(day.date) > 0;
            if (!nfp.empty()) day.nfp = nfp.count(day.date) > 0;
            n += day.cpi + day.ppi;
        }
    }
    return n;
}

inline bool load_raw(const std::string& dir, const std::string& sym, Series& s, std::string& err) {
    const std::string csv = dir + "/" + sym + ".csv", bin = dir + "/" + sym + ".bin";
    s.sym = sym;
    if (detail::newer(csv, bin)) {
        FILE* f = std::fopen(csv.c_str(), "r");
        if (!f) { err = "cannot open " + csv; return false; }
        char line[256]; long long t; double o, h, l, c, v, bv;
        while (std::fgets(line, sizeof line, f)) {
            if (std::sscanf(line, "%lld,%lf,%lf,%lf,%lf,%lf,%lf", &t, &o, &h, &l, &c, &v, &bv) != 7) continue;
            if (!(o > 0 && h > 0 && l > 0 && c > 0)) continue;
            s.t.push_back(t); s.o.push_back((float)o); s.h.push_back((float)h); s.l.push_back((float)l); s.c.push_back((float)c);
            s.v.push_back((float)v); s.bv.push_back((float)bv);
        }
        std::fclose(f);
        if (s.t.empty()) { err = csv + " has no rows"; return false; }
        FILE* g = std::fopen((bin + ".tmp").c_str(), "wb");
        if (g) {
            uint64_t n = s.t.size();
            std::fwrite(&detail::kMagic, 4, 1, g); std::fwrite(&n, 8, 1, g);
            detail::wr(g, s.t); detail::wr(g, s.o); detail::wr(g, s.h); detail::wr(g, s.l); detail::wr(g, s.c); detail::wr(g, s.v); detail::wr(g, s.bv);
            bool good = std::fclose(g) == 0;
            if (good) std::rename((bin + ".tmp").c_str(), bin.c_str());
        }
        return true;
    }
    FILE* f = std::fopen(bin.c_str(), "rb");
    if (!f) { err = "no data for " + sym + " (run scripts/edge_lab.sh export)"; return false; }
    uint32_t m = 0; uint64_t n = 0;
    bool good = std::fread(&m, 4, 1, f) == 1 && m == detail::kMagic && std::fread(&n, 8, 1, f) == 1 &&
                detail::rd(f, s.t, n) && detail::rd(f, s.o, n) && detail::rd(f, s.h, n) && detail::rd(f, s.l, n) &&
                detail::rd(f, s.c, n) && detail::rd(f, s.v, n) && detail::rd(f, s.bv, n);
    std::fclose(f);
    if (!good) err = bin + " is corrupt — delete it and re-export";
    return good;
}

// First bar of day D at or after index `from` whose trading-day minute is ≥ tmin (D.b1 if none). The minute is
// monotonic inside a trading day, so this is a binary search.
inline int first_bar_at(const Series& s, const Day& D, int tmin, int from) {
    int lo = std::max(from, D.b0), hi = D.b1;
    while (lo < hi) { int mid = (lo + hi) / 2; if (tday_min(s.t[mid]) >= tmin) hi = mid; else lo = mid + 1; }
    return lo;
}

// Quarterly roll Thursday: 8 days before the third Friday of Mar/Jun/Sep/Dec (contracts_calendar.sql convention).
inline std::vector<int> roll_dates(int y0, int y1) {
    std::vector<int> out;
    for (int y = y0; y <= y1; ++y)
        for (int m : {3, 6, 9, 12}) {
            int d = y * 10000 + m * 100 + 15;
            while (weekday_of(d) != 5) d = add_days(d, 1);
            out.push_back(add_days(d, -8));
        }
    return out;
}

// Splits bars into trading days and fills the day facts. `fomc` = YYYYMMDD set, `vix` may be empty.
inline void build_days(Series& s, const std::set<int>& fomc, const paper::VixRegime& vix) {
    s.days.clear(); s.day_of.assign(s.size(), -1);
    for (size_t i = 0; i < s.size(); ++i) {
        int tm = tday_min(s.t[i]);
        if (tm >= kDayEnd) continue;                               // 17:00–17:59 maintenance hour
        int date = ymd_of(s.t[i] + 6 * 3600);                      // 18:00 belongs to the next date
        if (s.days.empty() || s.days.back().date != date) { Day d; d.date = date; d.b0 = d.b1 = (int)i; s.days.push_back(d); }
        Day& d = s.days.back();
        d.b1 = (int)i + 1; s.day_of[i] = (int)s.days.size() - 1;
        if (tm >= kRthOpen && d.rth0 < 0) d.rth0 = (int)i;
        if (tm >= kRthClose && d.rth1 < 0) d.rth1 = (int)i;
    }
    for (auto& d : s.days) {
        if (d.rth1 < 0) d.rth1 = d.b1;
        if (d.rth0 < 0) continue;
        int n = 0; double hi = -1e18, lo = 1e18, bvs = 0;
        for (int i = d.rth0; i < d.rth1; ++i) { ++n; hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]); bvs += s.bv[i]; }
        d.ok = n >= 350 && tday_min(s.t[d.rth0]) == kRthOpen;
        if (n > 0) { d.ro = s.o[d.rth0]; d.rh = hi; d.rl = lo; d.rc = s.c[d.rth1 - 1]; }
        d.flow = bvs > 0;
    }
    const std::vector<int> rolls = roll_dates(2009, 2028);
    // prior-day facts from the chain of ok days (reset after a > 7 calendar-day hole)
    std::vector<double> ranges, closes, vols;
    int prev = -1;
    for (size_t k = 0; k < s.days.size(); ++k) {
        Day& d = s.days[k];
        int wd = weekday_of(d.date); d.dow = wd;
        d.opex = wd == 5 && d.date % 100 >= 15 && d.date % 100 <= 21;
        d.nfp = wd == 5 && d.date % 100 <= 7;
        d.fomc = fomc.count(d.date) > 0;
        for (int r : rolls) if (d.date >= add_days(r, -3) && d.date <= add_days(r, 6)) { d.rollwin = true; break; }
        if (vix.size()) {
            paper::VixDay x = vix.before(ymd_str(d.date));
            if (x.known) { d.vix = x.level; d.vixc1 = x.chg1; d.vixc5 = x.chg5; d.vixpct = x.pct; if (x.vix3m > 0) d.vts = x.level / x.vix3m; }
        }
        if (prev >= 0) {
            const Day& p = s.days[prev];
            int gap = (int)((timegm_ymd(d.date) - timegm_ymd(p.date)) / 86400);
            if (gap > 7) { ranges.clear(); closes.clear(); vols.clear(); }
            else {
                d.pc = p.rc; d.ph = p.rh; d.pl = p.rl;
                d.pibs = p.rh > p.rl ? (p.rc - p.rl) / (p.rh - p.rl) : NAN;
                if (closes.size() >= 2) d.pc2 = closes[closes.size() - 2];
            }
        }
        if (ranges.size() >= 14) {
            double a = 0; for (size_t j = ranges.size() - 14; j < ranges.size(); ++j) a += ranges[j]; d.atr = a / 14;
        }
        if (closes.size() >= 50) {
            double a = 0; for (size_t j = closes.size() - 50; j < closes.size(); ++j) a += closes[j]; d.sma50 = a / 50;
        }
        {
            auto mean_last = [&](size_t n, size_t skip) -> double {   // mean of n closes ending `skip` before the newest
                if (closes.size() < n + skip) return NAN;
                double a = 0; for (size_t j = closes.size() - skip - n; j < closes.size() - skip; ++j) a += closes[j];
                return a / (double)n;
            };
            d.sma100 = mean_last(100, 0); d.sma200 = mean_last(200, 0);
            if (closes.size() >= 2) {
                const double c1 = closes.back(), c2 = closes[closes.size() - 2];
                for (size_t n : {50, 100, 200}) {
                    const double now = mean_last(n, 0), before = mean_last(n, 1);
                    if (std::isnan(now) || std::isnan(before)) continue;
                    if (c1 > now && c2 <= before) ++d.xma;
                    else if (c1 < now && c2 >= before) --d.xma;
                }
            }
        }
        if (prev >= 0 && !std::isnan(d.pc)) { const Day& p = s.days[prev]; d.pct = s.t[p.rth1 - 1] + 60; }
        if (closes.size() >= 3) {   // RSI(2), Wilder over the closes so far
            double up = 0, dn = 0; bool init = false;
            size_t from = closes.size() > 60 ? closes.size() - 60 : 1;
            for (size_t j = from; j < closes.size(); ++j) {
                double ch = closes[j] - closes[j - 1], u = std::max(ch, 0.0), w = std::max(-ch, 0.0);
                if (!init) { up = u; dn = w; init = true; } else { up = (up + u) / 2; dn = (dn + w) / 2; }
            }
            d.rsi2 = up + dn > 0 ? 100 * up / (up + dn) : 50;
        }
        for (size_t N : {5, 10}) {   // daily path / direction over N sessions (see Day)
            if (closes.size() < N + 1 || std::isnan(d.atr) || d.atr <= 0) continue;
            const size_t n = closes.size(); double path = 0;
            for (size_t j = n - N; j < n; ++j) path += std::fabs(closes[j] - closes[j - 1]);
            const double e = path > 0 ? std::fabs(closes[n - 1] - closes[n - 1 - N]) / path : NAN, r = (closes[n - 1] - closes[n - 1 - N]) / d.atr;
            if (N == 5) { d.e5 = e; d.r5 = r; } else { d.e10 = e; d.r10 = r; }
        }
        if (closes.size() >= 21 && !std::isnan(d.atr) && d.atr > 0) {   // Wyckoff phase inputs (see Day)
            const size_t n = closes.size();
            double path = 0, lo = 1e18, hi = -1e18, upv = 0, allv = 0;
            for (size_t j = n - 20; j < n; ++j) {
                path += std::fabs(closes[j] - closes[j - 1]);
                if (closes[j] > closes[j - 1]) upv += vols[j];
                allv += vols[j];
            }
            for (size_t j = n - 20; j < n; ++j) { lo = std::min(lo, closes[j]); hi = std::max(hi, closes[j]); }
            d.e20 = path > 0 ? std::fabs(closes[n - 1] - closes[n - 21]) / path : NAN;
            d.r20 = (closes[n - 1] - closes[n - 21]) / d.atr;
            if (n >= 61) d.r60 = (closes[n - 21] - closes[n - 61]) / d.atr;
            d.uv20 = allv > 0 ? upv / allv : NAN;
            d.p20 = hi > lo ? (closes[n - 1] - lo) / (hi - lo) : NAN;
        }
        if (d.ok) {
            double rv = 0; for (int j = d.rth0; j < d.rth1; ++j) rv += s.v[j];
            ranges.push_back(d.rh - d.rl); closes.push_back(d.rc); vols.push_back(rv); prev = (int)k;
        }
    }
    // month-to-date and pre-holiday, over ok days
    {
        int last_ok = -1; double prev_month_close = NAN, last_close = NAN; int cur_month = 0;
        for (size_t k = 0; k < s.days.size(); ++k) {
            Day& d = s.days[k];
            if (!d.ok) continue;
            if (d.date / 100 != cur_month) { prev_month_close = last_close; cur_month = d.date / 100; }
            if (last_ok >= 0 && !std::isnan(prev_month_close) && prev_month_close > 0 && !std::isnan(d.pc))
                d.mtd = d.pc / prev_month_close - 1.0;
            if (last_ok >= 0) {
                int wd = 0;   // weekdays strictly between the previous ok day and this one
                for (int x = add_days(s.days[last_ok].date, 1); x < d.date; x = add_days(x, 1)) { int w = weekday_of(x); if (w >= 1 && w <= 5) ++wd; }
                if (wd > 0 && wd <= 2) s.days[last_ok].prehol = true;   // 1–2 missing weekdays = a holiday, not a data hole
            }
            last_close = d.rc; last_ok = (int)k;
        }
    }
    // volume profile of each ok day's RTH: bins of max(0.25, range / 80), volume spread over each bar's high–low
    for (auto& d : s.days) {
        if (!d.ok || !(d.rh > d.rl)) continue;
        const double B = std::max(0.25, (d.rh - d.rl) / 80.0);
        const int nb = (int)((d.rh - d.rl) / B) + 1;
        std::vector<double> vol(nb, 0.0);
        for (int i = d.rth0; i < d.rth1; ++i) {
            int a = std::clamp((int)((s.l[i] - d.rl) / B), 0, nb - 1), b = std::clamp((int)((s.h[i] - d.rl) / B), 0, nb - 1);
            const double share = s.v[i] / (b - a + 1);
            for (int k = a; k <= b; ++k) vol[k] += share;
        }
        int pk = (int)(std::max_element(vol.begin(), vol.end()) - vol.begin());
        double tot = 0; for (double x : vol) tot += x;
        if (!(tot > 0)) continue;
        int lo = pk, hi = pk; double in = vol[pk];
        while (in < 0.7 * tot && (lo > 0 || hi < nb - 1)) {
            const double up = hi < nb - 1 ? vol[hi + 1] : -1, dn = lo > 0 ? vol[lo - 1] : -1;
            if (up >= dn) in += vol[++hi]; else in += vol[--lo];
        }
        d.poc = d.rl + (pk + 0.5) * B; d.val = d.rl + lo * B; d.vah = d.rl + (hi + 1) * B;
    }
    // prior day's profile, last week's / month's RTH extremes, naked POCs — over ok days, reset after a > 7-day hole
    {
        int prev = -1; int cur_w = 0, cur_m = 0;
        double wh = -1e18, wl = 1e18, mh = -1e18, ml = 1e18, last_wh = NAN, last_wl = NAN, last_mh = NAN, last_ml = NAN;
        std::vector<int> recent;                                   // ok-day indices since the last hole, newest last
        std::vector<double> dayhi(s.days.size(), -1e18), daylo(s.days.size(), 1e18);   // full trading-day range
        for (size_t k = 0; k < s.days.size(); ++k)
            for (int i = s.days[k].b0; i < s.days[k].b1; ++i) { dayhi[k] = std::max(dayhi[k], (double)s.h[i]); daylo[k] = std::min(daylo[k], (double)s.l[i]); }
        for (size_t k = 0; k < s.days.size(); ++k) {
            Day& d = s.days[k];
            if (!d.ok) continue;
            if (prev >= 0 && (timegm_ymd(d.date) - timegm_ymd(s.days[prev].date)) / 86400 > 7) {
                prev = -1; cur_w = cur_m = 0; last_wh = last_wl = last_mh = last_ml = NAN; recent.clear();
            }
            const int wk = add_days(d.date, -(weekday_of(d.date) - 1)), mo = d.date / 100;
            if (wk != cur_w) { if (cur_w) { last_wh = wh; last_wl = wl; } cur_w = wk; wh = -1e18; wl = 1e18; }
            if (mo != cur_m) { if (cur_m) { last_mh = mh; last_ml = ml; } cur_m = mo; mh = -1e18; ml = 1e18; }
            d.pwh = last_wh; d.pwl = last_wl; d.pmh = last_mh; d.pml = last_ml;
            if (prev >= 0) { const Day& p = s.days[prev]; d.ppoc = p.poc; d.pvah = p.vah; d.pval = p.val; }
            // naked POCs: a POC from one of the last 20 ok days, not inside any later day's full range before today
            double above = 1e18, below = -1e18;
            const size_t from = recent.size() > 20 ? recent.size() - 20 : 0;
            for (size_t r = from; r < recent.size(); ++r) {
                const Day& o = s.days[recent[r]];
                if (std::isnan(o.poc)) continue;
                bool naked = true;
                for (size_t q = r + 1; q < recent.size() && naked; ++q) {
                    if (daylo[recent[q]] <= o.poc && o.poc <= dayhi[recent[q]]) naked = false;
                }
                if (!naked) continue;
                if (o.poc > d.ro) above = std::min(above, o.poc); else if (o.poc < d.ro) below = std::max(below, o.poc);
            }
            if (above < 1e17) d.npoca = above;
            if (below > -1e17) d.npocb = below;
            wh = std::max(wh, d.rh); wl = std::min(wl, d.rl); mh = std::max(mh, d.rh); ml = std::min(ml, d.rl);
            recent.push_back((int)k); prev = (int)k;
        }
    }
    // trading day of month / days left, over ok days
    for (size_t k = 0; k < s.days.size();) {
        size_t e = k; int ym = s.days[k].date / 100;
        while (e < s.days.size() && s.days[e].date / 100 == ym) ++e;
        int n = 0; for (size_t j = k; j < e; ++j) if (s.days[j].ok) ++n;
        int idx = 0;
        for (size_t j = k; j < e; ++j) if (s.days[j].ok) { ++idx; s.days[j].tdom = idx; s.days[j].tdleft = n - idx; }
        k = e;
    }
}

}  // namespace lab
