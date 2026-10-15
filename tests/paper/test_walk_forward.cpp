// test_walk_forward — selection on past months only, stand-aside, blind / hindsight comparisons.
#include "walk_forward.hpp"

#include <cmath>
#include <cstdio>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

int main() {
    // Two variants, four months. A wins months 0-1, B wins months 2-3.
    wf::Group g{"fam | ny", {"a", "b"}, {
        {{10, 100}, {10, 100}, {10, -50}, {10, -50}},
        {{10, -20}, {10, -20}, {10, 80}, {10, 80}}}};

    auto s = wf::walk_forward(g, 4, 2, 5);
    CHECK(s.tested == 2);
    CHECK(s.traded == 2);
    CHECK(s.picks[0].month == 2 && s.picks[0].variant == 0);   // trained on months 0-1 → A, never sees month 2
    CHECK(std::fabs(s.oos_net - (-50 + 80)) < 1e-9);           // month 3: train 1-2 → A 50, B 60 → B
    CHECK(s.picks[1].variant == 1);
    CHECK(std::fabs(s.baseline_net - (15 + 15)) < 1e-9);        // (−50+80)/2 each month
    CHECK(std::fabs(s.oracle_net - 160) < 1e-9);
    CHECK(s.positive == 1);

    // Stand aside when the best training net is ≤ 0, and when no variant has enough training trades.
    wf::Group losing{"x | ny", {"a"}, {{{10, -10}, {10, -10}, {10, 500}}}};
    auto l = wf::walk_forward(losing, 3, 2, 5);
    CHECK(l.tested == 1 && l.traded == 0 && l.oos_net == 0);
    auto thin = wf::walk_forward(g, 4, 2, 50);
    CHECK(thin.traded == 0);

    std::printf("test_walk_forward: %d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
