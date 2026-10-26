#pragma once
/*  market_spec.hpp — exact integer encoding of Lighter prices/sizes and the quote-size rule.

    base_amount = size × 10^size_decimals, price = price × 10^price_decimals (orderBookDetails). All decimal strings
    are parsed exactly into integers (never float). Dollar amounts in this module are micro-USD (1e-6) int64.

    Quote sizing (R-ORD-9): the target $ size is rounded UP to the size grid at the signing price, then rechecked:
      notional ≥ min_quote_amount × margin (default 1.05 → $10.50 on the $10 minimum),
      base ≥ min_base_amount,
      notional ≤ cap (default $12).
    A market whose SMALLEST valid order already exceeds the cap is refused outright (coarse grids such as BNB). */

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

namespace lighter::live {

struct SpecError : std::runtime_error { using std::runtime_error::runtime_error; };

inline constexpr int64_t kMicro = 1'000'000;   // micro-USD per USD

inline int64_t pow10i(int n) {
    if (n < 0 || n > 18) throw SpecError("decimals out of range");
    int64_t r = 1;
    for (int i = 0; i < n; ++i) r *= 10;
    return r;
}

// Parse a non-negative decimal string ("0.0020", "10", "2325.74") into an integer at `decimals` places.
// Extra fractional digits are allowed only if they are zeros (no silent rounding).
inline int64_t parse_decimal(const std::string& s, int decimals) {
    if (s.empty()) throw SpecError("empty decimal");
    int64_t scale = pow10i(decimals);
    size_t dot = s.find('.');
    std::string ip = s.substr(0, dot), fp = dot == std::string::npos ? "" : s.substr(dot + 1);
    if (ip.empty()) ip = "0";
    for (char c : ip) if (c < '0' || c > '9') throw SpecError("bad decimal '" + s + "'");
    for (char c : fp) if (c < '0' || c > '9') throw SpecError("bad decimal '" + s + "'");
    if (ip.size() > 15) throw SpecError("decimal too large '" + s + "'");
    for (size_t i = size_t(decimals); i < fp.size(); ++i)
        if (fp[i] != '0') throw SpecError("'" + s + "' has more than " + std::to_string(decimals) + " decimals");
    __int128 v = __int128(std::stoll(ip)) * scale;
    int64_t frac = 0, fs = scale;
    for (int i = 0; i < decimals; ++i) {
        fs /= 10;
        if (size_t(i) < fp.size()) frac += int64_t(fp[size_t(i)] - '0') * fs;
    }
    v += frac;
    if (v > INT64_MAX) throw SpecError("decimal overflow '" + s + "'");
    return int64_t(v);
}

struct MarketSpec {
    std::string symbol;
    int market_index = -1;
    int size_decimals = 0;
    int price_decimals = 0;
    int64_t min_base_amount = 0;    // integer base units
    int64_t min_quote_micro = 0;    // micro-USD

    static MarketSpec from_strings(std::string sym, int market, int sd, int pd, const std::string& min_base,
                                   const std::string& min_quote) {
        MarketSpec m{std::move(sym), market, sd, pd, 0, 0};
        if (sd < 0 || sd > 10 || pd < 0 || pd > 10) throw SpecError("decimals out of range");
        m.min_base_amount = parse_decimal(min_base, sd);
        m.min_quote_micro = parse_decimal(min_quote, 6);
        return m;
    }

    int64_t price_int(const std::string& px) const { return parse_decimal(px, price_decimals); }
    int64_t base_int(const std::string& sz) const { return parse_decimal(sz, size_decimals); }

    // notional (micro-USD) of base × price, rounded DOWN (conservative for the minimum check).
    int64_t notional_micro_floor(int64_t base, int64_t price) const {
        __int128 n = __int128(base) * price * kMicro / pow10i(size_decimals + price_decimals);
        return int64_t(n);
    }
    // notional (micro-USD), rounded UP (conservative for the cap check).
    int64_t notional_micro_ceil(int64_t base, int64_t price) const {
        __int128 den = pow10i(size_decimals + price_decimals);
        __int128 num = __int128(base) * price * kMicro;
        return int64_t((num + den - 1) / den);
    }
    // smallest base (integer units) whose notional at `price` is ≥ usd_micro.
    int64_t base_for_usd_ceil(int64_t usd_micro, int64_t price) const {
        if (price <= 0) throw SpecError("price must be > 0");
        __int128 num = __int128(usd_micro) * pow10i(size_decimals + price_decimals);
        __int128 den = __int128(price) * kMicro;
        return int64_t((num + den - 1) / den);
    }
};

struct SizingRules {
    int64_t quote_micro = 11 * kMicro;           // target quote size ($11)
    int64_t cap_micro = 12 * kMicro;             // max notional of any order ($12)
    int64_t min_quote_margin_bp = 10'500;        // ×1.05 over min_quote_amount (basis points of 1.0 = 10'000)
};

struct QuoteSize {
    int64_t base = 0;
    int64_t notional_micro = 0;   // floor
};

// Minimum notional (micro-USD) the gateway accepts for this market: max(min_quote × margin, min_base × price).
inline int64_t required_min_micro(const MarketSpec& m, const SizingRules& r) {
    __int128 v = __int128(m.min_quote_micro) * r.min_quote_margin_bp;
    return int64_t((v + 9'999) / 10'000);
}

// Validate an already-sized order (R-ORD-9). Returns the reason, empty = ok.
inline std::string validate_order(const MarketSpec& m, const SizingRules& r, int64_t base, int64_t price) {
    if (price <= 0) return "price <= 0";
    if (base <= 0) return "base <= 0";
    if (base < m.min_base_amount) return "below min_base_amount";
    if (m.notional_micro_floor(base, price) < required_min_micro(m, r)) return "below min_quote_amount x margin";
    if (m.notional_micro_ceil(base, price) > r.cap_micro) return "above notional cap";
    return {};
}

// The smallest valid order at `price`: rounds UP to the grid to clear both minimums (with margin).
inline QuoteSize smallest_valid(const MarketSpec& m, const SizingRules& r, int64_t price) {
    int64_t b = std::max(m.base_for_usd_ceil(required_min_micro(m, r), price), m.min_base_amount);
    return {b, m.notional_micro_floor(b, price)};
}

// Size a quote: target $ rounded UP to the size grid, then rechecked. Throws SpecError when the market cannot quote
// within the cap at this price (the caller then refuses the market).
inline QuoteSize size_quote(const MarketSpec& m, const SizingRules& r, int64_t price) {
    QuoteSize s = smallest_valid(m, r, price);
    if (m.notional_micro_ceil(s.base, price) > r.cap_micro)
        throw SpecError(m.symbol + ": smallest valid order $" + std::to_string(double(s.notional_micro) / kMicro) +
                        " exceeds the cap");
    int64_t b = std::max(m.base_for_usd_ceil(r.quote_micro, price), s.base);
    if (m.notional_micro_ceil(b, price) > r.cap_micro) b = s.base;   // target overshoots on a coarse grid: use the minimum
    std::string why = validate_order(m, r, b, price);
    if (!why.empty()) throw SpecError(m.symbol + ": " + why);
    return {b, m.notional_micro_floor(b, price)};
}

// Price grid helpers.
inline int64_t floor_price_frac(int64_t px, int num, int den) {   // floor(px × num/den), integer exact
    return int64_t(__int128(px) * num / den);
}

}  // namespace lighter::live
