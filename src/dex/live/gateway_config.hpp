#pragma once
/*  gateway_config.hpp — config + network guard for the Lighter live gateway.

    Rules (any violation throws ConfigError; the binary maps that to exit 2 before any socket opens):
      * MAINNET ONLY: host must be exactly mainnet.zklighter.elliot.ai and chain id 304. Every other host or chain is
        refused — the testnet host included, by name (founder rule: never use testnet).
      * mode ∈ {offline, probe, live} and must equal the mode given on the command line.
      * probe and live send real orders, so both need the arming file to exist (content: the account index and a
        date YYYY-MM-DD at most 7 days old) and NO_DEPLOY_MM to be absent. offline needs neither.
      * unknown config keys are refused.
      * the key file is opened with O_NOFOLLOW and checked with fstat on that same fd: a regular file, mode exactly
        0600, owned by this user; no directory on its absolute path may be a symlink; it must lie outside every git
        work tree (no .git in any parent directory) and outside the repo root. Its JSON carries network,
        account_index, api_key_index — all must match the config.
      * ArmingGate re-checks the arming file before every batch (not only at parse time).                 */

#include "dex/live/market_spec.hpp"

#include <nlohmann/json.hpp>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cerrno>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

namespace lighter::live {

struct ConfigError : std::runtime_error { using std::runtime_error::runtime_error; };

inline constexpr const char* kMainnetHost = "mainnet.zklighter.elliot.ai";
inline constexpr const char* kTestnetHost = "testnet.zklighter.elliot.ai";   // appears ONLY to be refused

enum class Mode { Offline, Probe, Live };

inline Mode parse_mode(const std::string& s) {
    if (s == "offline") return Mode::Offline;
    if (s == "probe") return Mode::Probe;
    if (s == "live") return Mode::Live;
    throw ConfigError("unknown mode '" + s + "'");
}
inline const char* mode_name(Mode m) {
    switch (m) { case Mode::Offline: return "offline"; case Mode::Probe: return "probe"; case Mode::Live: return "live"; }
    return "?";
}
inline bool sends_orders(Mode m) { return m != Mode::Offline; }

// Host/chain pairing is hard-coded; the config cannot override it.
inline void network_guard(const std::string& host, int chain_id) {
    if (host.find("testnet") != std::string::npos)
        throw ConfigError("testnet host '" + host + "' refused (mainnet only, never testnet)");
    if (host != kMainnetHost) throw ConfigError("host '" + host + "' refused (only " + std::string(kMainnetHost) + ")");
    if (chain_id != 304) throw ConfigError("chain id " + std::to_string(chain_id) + " refused (mainnet 304 only)");
}

struct RiskConfig {
    int max_markets = 1;
    int64_t quote_micro = 11 * kMicro;
    int64_t max_order_micro = 12 * kMicro;       // refuse a market whose smallest valid order is above this
    int max_position_quotes = 1;                 // per market, each way
    int64_t max_gross_micro = 12 * kMicro;
    int64_t daily_stop_micro = -1'500'000;       // −$1.50
    int64_t total_stop_micro = -2'500'000;       // −$2.50
    int leverage = 5;
};

struct GatewayConfig {
    Mode mode = Mode::Offline;
    std::string host = kMainnetHost;
    int chain_id = 304;
    int64_t account_index = 0;
    int api_key_index = 0;
    std::string key_file;
    std::string arming_file;
    std::string no_deploy_file;
    std::string state_dir;           // persisted risk baselines, kill file
    std::string market;              // the one market symbol
    int max_txs_per_batch = 15;
    int rate_budget_per_min = 54;
    RiskConfig risk;
    nlohmann::json engine = nlohmann::json::object();   // mm_live engine section (parsed strictly by mm_live_config.hpp)
};

// ── key file ──────────────────────────────────────────────────────────────────────────────────────────────────
inline bool inside_git_tree(std::filesystem::path p) {
    std::error_code ec;
    p = std::filesystem::weakly_canonical(p, ec);
    for (auto d = p.parent_path(); !d.empty(); d = d.parent_path()) {
        if (std::filesystem::exists(d / ".git", ec)) return true;
        if (d == d.root_path()) break;
    }
    return false;
}

inline bool path_under(const std::filesystem::path& p, const std::filesystem::path& root) {
    if (root.empty()) return false;
    std::error_code ec;
    auto a = std::filesystem::weakly_canonical(p, ec), b = std::filesystem::weakly_canonical(root, ec);
    auto it = std::mismatch(b.begin(), b.end(), a.begin(), a.end());
    return it.first == b.end();
}

struct KeyMaterial {
    std::string private_key;
    int64_t account_index = 0;
    int api_key_index = 0;
    ~KeyMaterial() { std::fill(private_key.begin(), private_key.end(), '\0'); }
};

// Every directory on the (absolute) path must be a real directory, not a symlink.
inline void check_no_symlinked_parents(const std::string& path) {
    std::filesystem::path p(path);
    if (!p.is_absolute()) throw ConfigError("key file path must be absolute: " + path);
    std::filesystem::path cur = p.root_path();
    for (auto it = std::next(p.begin()); it != p.end(); ++it) {
        if (std::next(it) == p.end()) break;   // the file itself is handled by O_NOFOLLOW
        cur /= *it;
        struct stat st{};
        if (::lstat(cur.c_str(), &st) != 0) throw ConfigError("key file parent missing: " + cur.string());
        if (S_ISLNK(st.st_mode)) throw ConfigError("key file parent directory is a symlink: " + cur.string());
        if (!S_ISDIR(st.st_mode)) throw ConfigError("key file parent is not a directory: " + cur.string());
    }
}

// Opens the key file with O_NOFOLLOW and checks the SAME fd with fstat (no check-then-open race); returns its bytes.
inline std::string read_key_file_checked(const std::string& path, const std::string& repo_root) {
    check_no_symlinked_parents(path);
    if (path_under(path, repo_root)) throw ConfigError("key file inside the repo refused: " + path);
    if (inside_git_tree(path)) throw ConfigError("key file inside a git work tree refused: " + path);
    int fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NOCTTY);
    if (fd < 0) {
        if (errno == ELOOP) throw ConfigError("key file is a symlink: " + path);
        throw ConfigError("key file missing or unreadable: " + path);
    }
    struct Closer { int fd; ~Closer() { ::close(fd); } } closer{fd};
    struct stat st{};
    if (::fstat(fd, &st) != 0) throw ConfigError("fstat failed on key file: " + path);
    if (!S_ISREG(st.st_mode)) throw ConfigError("key file is not a regular file: " + path);
    if ((st.st_mode & 07777) != 0600) {
        std::ostringstream o;
        o << "key file mode " << std::oct << (st.st_mode & 07777) << " refused (must be 0600): " << path;
        throw ConfigError(o.str());
    }
    if (st.st_uid != ::geteuid()) throw ConfigError("key file not owned by this user: " + path);
    if (st.st_size > 64 * 1024) throw ConfigError("key file too large: " + path);
    std::string body(size_t(st.st_size), '\0');
    size_t off = 0;
    while (off < body.size()) {
        ssize_t r = ::read(fd, body.data() + off, body.size() - off);
        if (r <= 0) throw ConfigError("key file read failed: " + path);
        off += size_t(r);
    }
    return body;
}

inline KeyMaterial load_key_file(const GatewayConfig& c, const std::string& repo_root) {
    std::string body = read_key_file_checked(c.key_file, repo_root);
    nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
    std::fill(body.begin(), body.end(), '\0');
    if (j.is_discarded() || !j.is_object()) throw ConfigError("key file is not a JSON object");
    static const std::set<std::string> allowed = {"network", "account_index", "api_key_index", "private_key", "l1_address"};
    for (auto& [k, v] : j.items()) if (!allowed.count(k)) throw ConfigError("unknown key file field '" + k + "'");
    if (j.value("network", "") != "mainnet") throw ConfigError("key file network is not mainnet");
    KeyMaterial km;
    km.account_index = j.value("account_index", int64_t{0});
    km.api_key_index = j.value("api_key_index", 0);
    km.private_key = j.value("private_key", "");
    if (km.account_index != c.account_index) throw ConfigError("key file account_index does not match the config");
    if (km.api_key_index != c.api_key_index) throw ConfigError("key file api_key_index does not match the config");
    if (km.private_key.empty()) throw ConfigError("key file has no private_key");
    return km;
}

// ── arming ────────────────────────────────────────────────────────────────────────────────────────────────────
inline int64_t days_from_civil(int y, unsigned m, unsigned d) {   // Howard Hinnant
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = unsigned(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return int64_t(era) * 146097 + int64_t(doe) - 719468;
}

// Requires: the arming file exists and holds "<account_index> <YYYY-MM-DD>" with the date ≤ 7 days old and not in
// the future; NO_DEPLOY_MM absent. now_unix_s is passed in (testable).
inline void check_arming(const GatewayConfig& c, int64_t now_unix_s) {
    if (!c.no_deploy_file.empty() && std::filesystem::exists(c.no_deploy_file))
        throw ConfigError("NO_DEPLOY_MM present: refusing to send orders");
    if (c.arming_file.empty() || !std::filesystem::is_regular_file(c.arming_file))
        throw ConfigError("arming file missing: order sending is not armed");
    std::ifstream f(c.arming_file);
    int64_t acct = 0;
    std::string date;
    if (!(f >> acct >> date)) throw ConfigError("arming file must read '<account_index> <YYYY-MM-DD>'");
    if (acct != c.account_index) throw ConfigError("arming file is for another account");
    int y = 0;
    unsigned mo = 0, d = 0;
    if (date.size() != 10 || std::sscanf(date.c_str(), "%4d-%2u-%2u", &y, &mo, &d) != 3 || mo < 1 || mo > 12 || d < 1 || d > 31)
        throw ConfigError("arming file date is not YYYY-MM-DD");
    int64_t armed_day = days_from_civil(y, mo, d), today = now_unix_s / 86400;
    if (armed_day > today) throw ConfigError("arming file date is in the future");
    if (today - armed_day > 7) throw ConfigError("arming file is older than 7 days");
}

// ── config parse ──────────────────────────────────────────────────────────────────────────────────────────────
inline GatewayConfig parse_config(const nlohmann::json& j, Mode cli_mode, int64_t now_unix_s) {
    if (!j.is_object()) throw ConfigError("config must be a JSON object");
    static const std::set<std::string> top = {"mode", "host", "chain_id", "account_index", "api_key_index", "key_file",
        "arming_file", "no_deploy_file", "state_dir", "market", "max_txs_per_batch", "rate_budget_per_min", "risk", "engine"};
    static const std::set<std::string> riskk = {"max_markets", "quote_usd", "max_order_usd", "max_position_quotes",
        "max_gross_usd", "daily_stop_usd", "total_stop_usd", "leverage"};
    for (auto& [k, v] : j.items()) if (!top.count(k)) throw ConfigError("unknown config key '" + k + "'");
    GatewayConfig c;
    try {
        c.mode = parse_mode(j.at("mode").get<std::string>());
        c.host = j.at("host").get<std::string>();
        c.chain_id = j.at("chain_id").get<int>();
        c.account_index = j.at("account_index").get<int64_t>();
        c.api_key_index = j.at("api_key_index").get<int>();
        c.key_file = j.value("key_file", "");
        c.arming_file = j.value("arming_file", "");
        c.no_deploy_file = j.value("no_deploy_file", "");
        c.state_dir = j.at("state_dir").get<std::string>();
        c.market = j.at("market").get<std::string>();
        c.max_txs_per_batch = j.value("max_txs_per_batch", 15);
        c.rate_budget_per_min = j.value("rate_budget_per_min", 54);
        if (j.contains("engine")) { if (!j.at("engine").is_object()) throw ConfigError("engine must be an object"); c.engine = j.at("engine"); }
        if (j.contains("risk")) {
            const auto& r = j.at("risk");
            if (!r.is_object()) throw ConfigError("risk must be an object");
            for (auto& [k, v] : r.items()) if (!riskk.count(k)) throw ConfigError("unknown risk key '" + k + "'");
            auto usd = [&](const char* k, int64_t def) {
                return r.contains(k) ? int64_t(std::llround(r.at(k).get<double>() * kMicro)) : def;
            };
            c.risk.max_markets = r.value("max_markets", c.risk.max_markets);
            c.risk.quote_micro = usd("quote_usd", c.risk.quote_micro);
            c.risk.max_order_micro = usd("max_order_usd", c.risk.max_order_micro);
            c.risk.max_position_quotes = r.value("max_position_quotes", c.risk.max_position_quotes);
            c.risk.max_gross_micro = usd("max_gross_usd", c.risk.max_gross_micro);
            c.risk.daily_stop_micro = usd("daily_stop_usd", c.risk.daily_stop_micro);
            c.risk.total_stop_micro = usd("total_stop_usd", c.risk.total_stop_micro);
            c.risk.leverage = r.value("leverage", c.risk.leverage);
        }
    } catch (const nlohmann::json::exception& e) {
        throw ConfigError(std::string("config: ") + e.what());
    }
    if (c.mode != cli_mode)
        throw ConfigError(std::string("config mode '") + mode_name(c.mode) + "' != command-line mode '" + mode_name(cli_mode) + "'");
    network_guard(c.host, c.chain_id);
    if (c.account_index <= 0) throw ConfigError("account_index must be > 0");
    if (c.api_key_index < 4 || c.api_key_index > 254) throw ConfigError("api_key_index must be 4..254");
    if (c.max_txs_per_batch < 1 || c.max_txs_per_batch > 50) throw ConfigError("max_txs_per_batch must be 1..50");
    if (c.rate_budget_per_min < 1 || c.rate_budget_per_min > 60) throw ConfigError("rate_budget_per_min must be 1..60");
    if (c.risk.max_markets != 1) throw ConfigError("the pilot runs exactly 1 market");
    if (c.risk.daily_stop_micro >= 0 || c.risk.total_stop_micro >= 0) throw ConfigError("loss stops must be negative");
    if (c.risk.leverage < 1 || c.risk.leverage > 5) throw ConfigError("leverage must be 1..5");
    if (c.risk.quote_micro > c.risk.max_order_micro) throw ConfigError("quote_usd above max_order_usd");
    if (c.state_dir.empty()) throw ConfigError("state_dir required");
    if (sends_orders(c.mode)) {
        if (c.key_file.empty()) throw ConfigError("probe/live need a key_file");
        check_arming(c, now_unix_s);
    }
    return c;
}

// Re-checks the arming file (and NO_DEPLOY_MM) every time it is asked — called before every batch, so removing the
// arming file or dropping NO_DEPLOY_MM stops new orders at the next batch, not only at the next start.
class ArmingGate {
public:
    explicit ArmingGate(GatewayConfig c) : cfg_(std::move(c)) {}
    bool armed(int64_t now_unix_s) {
        if (!sends_orders(cfg_.mode)) { last_ = "offline mode"; return false; }
        try { check_arming(cfg_, now_unix_s); last_.clear(); return true; }
        catch (const ConfigError& e) { last_ = e.what(); return false; }
    }
    const std::string& last_reason() const { return last_; }

private:
    GatewayConfig cfg_;
    std::string last_;
};

}  // namespace lighter::live
