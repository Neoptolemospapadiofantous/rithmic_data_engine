#pragma once
// lighter_meta.hpp — the daily market-metadata snapshot of lighter_recorder: <archive>/_meta/<UTC date>.json holds
// GET /api/v1/orderBookDetails as received (decimals, minimum sizes, status, fees for every market), and a change
// in any recorded market's terms is reported by meta_diff. Why: Lighter can change a market's price/size decimals
// or minimums, and P&L / order sizing measured on the archive must use the terms that were live that day.
//
// No network here (the fetch is lighter::order_book_details in lighter_ws.hpp); unit-tested by tests/dex/test_recorder.cpp.
// The _meta directory sits beside the per-symbol directories; hl::list_days only takes <SYM>/<YYYY-MM-DD>.jsonl.zst,
// so it never mistakes it for a market.

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace lighter {

// The fields whose change matters for recording and sizing (decimals define the tick and lot, minimums the
// smallest order, status whether it trades at all, fees the P&L).
inline const std::vector<std::string>& meta_fields() {
    static const std::vector<std::string> f{"size_decimals", "price_decimals", "supported_size_decimals",
                                            "supported_price_decimals", "min_base_amount", "min_quote_amount",
                                            "status", "maker_fee", "taker_fee"};
    return f;
}

// market_id → details object, from an orderBookDetails response ({"code":200,"order_book_details":[…]}).
inline std::map<int, nlohmann::json> meta_markets(const nlohmann::json& body) {
    std::map<int, nlohmann::json> out;
    if (!body.is_object() || !body.contains("order_book_details") || !body["order_book_details"].is_array()) return out;
    for (const auto& m : body["order_book_details"])
        if (m.is_object() && m.contains("market_id") && m["market_id"].is_number_integer()) out[m["market_id"].get<int>()] = m;
    return out;
}

// Human-readable changes between two snapshots, one line each, e.g. "BNB(25) price_decimals 4 → 3".
// `only` = market ids to compare (empty = all). A market that appears or disappears is reported too.
inline std::vector<std::string> meta_diff(const nlohmann::json& before, const nlohmann::json& after, const std::set<int>& only = {}) {
    std::vector<std::string> out;
    const auto a = meta_markets(before), b = meta_markets(after);
    auto label = [](const nlohmann::json& m, int id) { return m.value("symbol", std::string("?")) + "(" + std::to_string(id) + ")"; };
    auto str = [](const nlohmann::json& m, const std::string& k) {
        if (!m.contains(k)) return std::string("<none>");
        return m[k].is_string() ? m[k].get<std::string>() : m[k].dump();
    };
    std::set<int> ids;
    for (const auto& [id, m] : a) ids.insert(id);
    for (const auto& [id, m] : b) ids.insert(id);
    for (int id : ids) {
        if (!only.empty() && !only.count(id)) continue;
        const auto ia = a.find(id), ib = b.find(id);
        if (ia == a.end()) { out.push_back(label(ib->second, id) + " new market (status " + str(ib->second, "status") + ")"); continue; }
        if (ib == b.end()) { out.push_back(label(ia->second, id) + " no longer listed"); continue; }
        for (const auto& f : meta_fields()) {
            const std::string x = str(ia->second, f), y = str(ib->second, f);
            if (x != y) out.push_back(label(ib->second, id) + " " + f + " " + x + " → " + y);
        }
    }
    return out;
}

// The newest <YYYY-MM-DD>.json in `dir` (by name), or empty.
inline std::filesystem::path latest_meta(const std::filesystem::path& dir) {
    std::filesystem::path best;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return best;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        const std::string n = e.path().filename().string();
        if (n.size() == 15 && n.substr(10) == ".json" && (best.empty() || n > best.filename().string())) best = e.path();
    }
    return best;
}

struct MetaResult {
    bool ok = false;
    std::string error;
    std::filesystem::path file, previous;
    int markets = 0;
    std::vector<std::string> changes;
};

// Validates `body`, diffs it against the newest snapshot already in `dir` (today's own earlier one after a restart,
// else the last day's), then writes it to dir/<date>.json (tmp + rename). Nothing is written for an invalid body.
inline MetaResult write_meta_snapshot(const std::filesystem::path& dir, const std::string& date, const std::string& body,
                                      const std::set<int>& only = {}) {
    MetaResult r;
    const auto now = nlohmann::json::parse(body, nullptr, false);
    const auto mk = meta_markets(now);
    if (mk.empty()) { r.error = "orderBookDetails reply has no order_book_details"; return r; }
    r.markets = static_cast<int>(mk.size());
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    r.previous = latest_meta(dir);
    if (!r.previous.empty()) {
        std::ifstream in(r.previous);
        std::stringstream ss; ss << in.rdbuf();
        const auto before = nlohmann::json::parse(ss.str(), nullptr, false);
        if (!before.is_discarded()) r.changes = meta_diff(before, now, only);
    }
    r.file = dir / (date + ".json");
    const auto tmp = r.file.string() + ".tmp";
    {
        std::ofstream o(tmp, std::ios::trunc);
        o << body;
        if (body.empty() || body.back() != '\n') o << '\n';
        if (!o.good()) { r.error = "cannot write " + tmp; return r; }
    }
    std::filesystem::rename(tmp, r.file, ec);
    if (ec) { r.error = "cannot rename " + tmp + ": " + ec.message(); return r; }
    r.ok = true;
    return r;
}

}  // namespace lighter
