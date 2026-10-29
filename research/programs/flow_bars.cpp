// flow_bars.cpp — 1-minute ORDER-FLOW bars from Binance aggTrades (2026-10-24,
// research/preregistered/2026-10-24_btc_orderflow_strategies.md). Read-only on data/history/binance.
// usage: flow_bars OUT.csv AGGTRADES.zip…   (files in time order)
// Columns: minute (UTC epoch s), open, high, low, close, vol, buy_vol (coins), trades, big_buy, big_sell (coins in taker orders
// with notional ≥ $100,000), max_notional (largest single taker order, $).
#include "history/binance_feed.hpp"
#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: flow_bars OUT.csv AGGTRADES.zip...\n"); return 2; }
  hist::AggTradesFeed feed(std::vector<std::string>(argv + 2, argv + argc), 0, INT64_MAX);
  FILE* fo = fopen(argv[1], "w"); fprintf(fo, "minute,open,high,low,close,vol,buy_vol,trades,big_buy,big_sell,max_notional\n");
  std::vector<hist::Tick> ticks; std::deque<hist::Quote> q;
  long long m = -1, n = 0; double o = 0, h = 0, l = 0, c = 0, v = 0, bv = 0, bb = 0, bs = 0, mx = 0; long long rows = 0;
  auto emit = [&]() { if (m < 0) return; fprintf(fo, "%lld,%.2f,%.2f,%.2f,%.2f,%.4f,%.4f,%lld,%.4f,%.4f,%.0f\n", m, o, h, l, c, v, bv, n, bb, bs, mx); rows++; };
  while (feed.next_batch(ticks, q, 500000)) {
    for (auto& t : ticks) {
      const long long mm = t.ts_us / 60'000'000LL * 60;
      if (mm != m) { emit(); m = mm; o = h = l = t.price; v = bv = bb = bs = mx = 0; n = 0; }
      if (t.price > h) h = t.price; if (t.price < l) l = t.price; c = t.price;
      const double qty = t.size / 1000.0, notional = qty * t.price;
      v += qty; n++; if (t.is_buy) bv += qty;
      if (notional >= 100000.0) { if (t.is_buy) bb += qty; else bs += qty; }
      if (notional > mx) mx = notional; }
    ticks.clear(); }
  emit(); fclose(fo); fprintf(stderr, "%s: %lld bars\n", argv[1], rows); return 0;
}
