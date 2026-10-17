static int64_t et(int ymd, int h, int m) { return timegm_ymd(ymd) - 12 * 3600 + h * 3600 + m * 60; }
static void bar(Series& s, int64_t t, double o, double h, double l, double c, double v = 10) {
static void bar(Series& s, int64_t t, double o, double h, double l, double c, double v = 10) {
    s.t.push_back(t); s.o.push_back((float)o); s.h.push_back((float)h); s.l.push_back((float)l); s.c.push_back((float)c);
    s.t.push_back(t); s.o.push_back((float)o); s.h.push_back((float)h); s.l.push_back((float)l); s.c.push_back((float)c);
    s.v.push_back((float)v); s.bv.push_back(0);
    s.v.push_back((float)v); s.bv.push_back(0);
}
}
static Series synth(int ndays, double pre, double post, bool alternate = true) {
    Series s; s.sym = "NQ";
    int d = 20200106, made = 0;
    double base = 1000;
    while (made < ndays) {
        int wd = weekday_of(d);
        if (wd >= 1 && wd <= 5) {
            int prev = add_days(d, wd == 1 ? -3 : -1);
            for (int m = 18 * 60; m < 24 * 60; ++m) bar(s, et(prev, m / 60, m % 60), base, base + 0.5, base - 0.5, base);
            for (int m = 0; m < 17 * 60; ++m) {
                double sgn = alternate ? ((made % 2) ? -1 : 1) : 1;
                double p = base;
                if (m >= 9 * 60 + 30 && m < 9 * 60 + 50) p = base + sgn * pre * (m - 570) / 20.0;
                else if (m >= 9 * 60 + 50 && m < 16 * 60) p = base + sgn * (pre + post * (m - 590) / 370.0);
                else if (m >= 16 * 60) p = base + sgn * (pre + post);
                bar(s, et(d, m / 60, m % 60), p, p + 0.5, p - 0.5, p);
            }
            ++made;
        }
        d = add_days(d, 1);
    }
    return s;
}
