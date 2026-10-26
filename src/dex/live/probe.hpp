#pragma once
/*  probe.hpp — the far-from-market probe as DATA (nothing is sent here).

    Sequence: one post-only BID at least 20 % below the best bid (it cannot fill: a post-only bid below the best bid
    rests, and a 20 % move inside the order's life is the trip-wire), wait for the exchange ack, cancel it, verify it
    is cancelled and that the account has no open orders. The size is the smallest valid order at the probe price
    (≥ min_quote × margin, ≥ min_base), and its notional must stay under the cap.                            */

#include "dex/live/market_spec.hpp"
#include "dex/live/signer.hpp"

#include <string>
#include <vector>

namespace lighter::live {

enum class ProbeStepKind { SignCreate, AwaitAck, SignCancel, AwaitCancelled, VerifyNoOpenOrders };

struct ProbeStep {
    ProbeStepKind kind;
    CreateOrderParams create{};   // SignCreate only (nonce filled by the sender)
    int64_t order_ref = 0;        // client index for cancel / await
    int64_t timeout_ms = 0;       // Await* / Verify
};

struct ProbeOptions {
    int min_distance_bp = 2'000;          // ≥ 20 % below the best bid
    int64_t order_expiry_ms = 10 * 60'000; // post-only orders need an explicit expiry
    int64_t ack_timeout_ms = 5'000;
    int64_t cancel_timeout_ms = 5'000;
};

// best_bid / best_ask are integer prices on the market's grid. now_ms = wall clock ms (for the order expiry).
inline std::vector<ProbeStep> build_probe_plan(const MarketSpec& m, const SizingRules& r, int64_t best_bid,
                                               int64_t best_ask, int64_t client_index, int64_t now_ms,
                                               const ProbeOptions& o = {}) {
    if (best_bid <= 0 || best_ask <= 0 || best_bid >= best_ask) throw SpecError("probe needs a valid, uncrossed book");
    if (o.min_distance_bp < 2'000) throw SpecError("probe distance must be at least 20%");
    check_client_order_index(client_index);
    check_market_index(m.market_index);
    size_quote(m, r, best_bid);   // a market the gateway cannot quote within the cap at today's price is not probed
    int64_t px = floor_price_frac(best_bid, 10'000 - o.min_distance_bp, 10'000);   // floor: never closer than 20 %
    check_price(px);
    if (__int128(px) * 10'000 > __int128(best_bid) * (10'000 - o.min_distance_bp))
        throw SpecError("probe price not far enough from the best bid");
    QuoteSize q = smallest_valid(m, r, px);
    std::string why = validate_order(m, r, q.base, px);
    if (!why.empty()) throw SpecError("probe order invalid at $" + std::to_string(px) + ": " + why);

    CreateOrderParams c;
    c.market_index = m.market_index;
    c.client_order_index = client_index;
    c.base_amount = q.base;
    c.price = px;
    c.is_ask = false;
    c.tif = TimeInForce::PostOnly;
    c.reduce_only = false;
    c.order_expiry_ms = now_ms + o.order_expiry_ms;
    c.self_trade_mode = 0;
    return {
        {ProbeStepKind::SignCreate, c, client_index, 0},
        {ProbeStepKind::AwaitAck, {}, client_index, o.ack_timeout_ms},
        {ProbeStepKind::SignCancel, {}, client_index, 0},
        {ProbeStepKind::AwaitCancelled, {}, client_index, o.cancel_timeout_ms},
        {ProbeStepKind::VerifyNoOpenOrders, {}, 0, o.cancel_timeout_ms},
    };
}

}  // namespace lighter::live
