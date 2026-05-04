// audit_daemon_main.cpp — C++ audit daemon for rithmic_engine
//
// Usage:
//   ./build/audit_daemon            # daemon: run every 300s
//   ./build/audit_daemon --once     # single pass then exit (Hermes CI)
//   ./build/audit_daemon --interval 60
//
// Exit code: 0 = all PASS/WARN, 1 = any FAIL

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <signal.h>
#include <sstream>
#include <string>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <thread>
#include <vector>

#include <libpq-fe.h>
#include <nlohmann/json.hpp>

#include "config.hpp"

namespace fs = std::filesystem;
using json   = nlohmann::json;

// ─────────────────────────────────────────────────────────────────────────────
// Signal handling
// ─────────────────────────────────────────────────────────────────────────────

static std::atomic<bool> g_stop{false};

static void handle_signal(int) noexcept { g_stop.store(true); }

// ─────────────────────────────────────────────────────────────────────────────
// CheckResult
// ─────────────────────────────────────────────────────────────────────────────

struct CheckResult {
    std::string name;
    std::string status;   // "PASS", "WARN", "FAIL", "INFO"
    std::string message;
};

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::string now_utc_iso() {
    auto t = std::time(nullptr);
    struct tm tm_utc{};
    gmtime_r(&t, &tm_utc);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
    return buf;
}

static std::string now_utc_display() {
    auto t = std::time(nullptr);
    struct tm tm_utc{};
    gmtime_r(&t, &tm_utc);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M UTC", &tm_utc);
    return buf;
}

// Returns UTC hour (0-23)
static int utc_hour() {
    auto t = std::time(nullptr);
    struct tm tm_utc{};
    gmtime_r(&t, &tm_utc);
    return tm_utc.tm_hour;
}

// True when inside RTH window: 13:00–21:00 UTC (09:00–17:00 ET)
static bool is_rth() {
    int h = utc_hour();
    return h >= 13 && h < 21;
}

// Execute a single-column single-row int64 query; returns -2 on table-missing,
// -1 on other error, the value on success.
static int64_t query_int64(PGconn* conn, const char* sql,
                            bool* table_missing = nullptr) {
    if (table_missing) *table_missing = false;
    PGresult* res = PQexec(conn, sql);
    if (!res) return -1;

    ExecStatusType st = PQresultStatus(res);
    if (st != PGRES_TUPLES_OK) {
        // Check for "undefined table" (SQLSTATE 42P01)
        const char* sqlstate = PQresultErrorField(res, PG_DIAG_SQLSTATE);
        bool missing = sqlstate && std::string(sqlstate) == "42P01";
        if (table_missing) *table_missing = missing;
        PQclear(res);
        // Roll back the aborted transaction so later queries succeed
        PGresult* rb = PQexec(conn, "ROLLBACK");
        if (rb) PQclear(rb);
        return missing ? -2 : -1;
    }

    int64_t val = -1;
    if (PQntuples(res) > 0 && !PQgetisnull(res, 0, 0))
        val = std::atoll(PQgetvalue(res, 0, 0));
    PQclear(res);
    return val;
}

static double query_double(PGconn* conn, const char* sql,
                            bool* is_null = nullptr) {
    if (is_null) *is_null = false;
    PGresult* res = PQexec(conn, sql);
    if (!res) { if (is_null) *is_null = true; return 0.0; }

    ExecStatusType st = PQresultStatus(res);
    if (st != PGRES_TUPLES_OK) {
        PQclear(res);
        PGresult* rb = PQexec(conn, "ROLLBACK");
        if (rb) PQclear(rb);
        if (is_null) *is_null = true;
        return 0.0;
    }

    double val = 0.0;
    bool null = true;
    if (PQntuples(res) > 0 && !PQgetisnull(res, 0, 0)) {
        val  = std::atof(PQgetvalue(res, 0, 0));
        null = false;
    }
    PQclear(res);
    if (is_null) *is_null = null;
    return val;
}

// ─────────────────────────────────────────────────────────────────────────────
// DB checks
// ─────────────────────────────────────────────────────────────────────────────

// 1. Data freshness
static CheckResult check_data_freshness(PGconn* conn) {
    bool missing = false;
    const char* sql =
        "SELECT EXTRACT(EPOCH FROM (NOW() - MAX(ts_event))) "
        "FROM ticks WHERE symbol='MNQ'";

    PGresult* res = PQexec(conn, sql);
    if (!res) return {"data_freshness", "WARN", "DB query failed"};

    ExecStatusType st = PQresultStatus(res);
    if (st != PGRES_TUPLES_OK) {
        const char* sqlstate = PQresultErrorField(res, PG_DIAG_SQLSTATE);
        missing = sqlstate && std::string(sqlstate) == "42P01";
        PQclear(res);
        PGresult* rb = PQexec(conn, "ROLLBACK");
        if (rb) PQclear(rb);
        if (missing)
            return {"data_freshness", "INFO", "ticks table not yet created (collector not started)"};
        return {"data_freshness", "WARN", "query error on ticks table"};
    }

    if (PQntuples(res) == 0 || PQgetisnull(res, 0, 0)) {
        PQclear(res);
        return {"data_freshness", "INFO", "no MNQ ticks recorded yet"};
    }

    double age_secs = std::atof(PQgetvalue(res, 0, 0));
    PQclear(res);

    char msg[128];
    double age_min = age_secs / 60.0;
    std::snprintf(msg, sizeof(msg), "newest MNQ tick %.1f min ago", age_min);

    if (age_secs > 1800 && is_rth())   // >30 min during RTH
        return {"data_freshness", "WARN", msg};
    return {"data_freshness", "PASS", msg};
}

// 2. Rejection rate
static CheckResult check_rejection_rate(PGconn* conn) {
    bool missing = false;
    const char* sql =
        "SELECT COUNT(*) FROM orders "
        "WHERE status='REJECTED' AND created_at > NOW()-INTERVAL '1 day'";

    int64_t cnt = query_int64(conn, sql, &missing);
    if (missing)
        return {"rejection_rate", "INFO", "orders table not present"};
    if (cnt < 0)
        return {"rejection_rate", "WARN", "query error on orders table"};

    char msg[64];
    std::snprintf(msg, sizeof(msg), "%lld rejections in last 24 h",
                  static_cast<long long>(cnt));
    if (cnt > 20) return {"rejection_rate", "FAIL", msg};
    if (cnt >  5) return {"rejection_rate", "WARN", msg};
    return {"rejection_rate", "PASS", msg};
}

// 3. Gap count (proxy: tick count in last day)
static CheckResult check_gap_count(PGconn* conn) {
    bool missing = false;
    const char* sql =
        "SELECT COUNT(*) FROM ticks WHERE ts_event > NOW()-INTERVAL '1 day'";

    int64_t cnt = query_int64(conn, sql, &missing);
    if (missing)
        return {"gap_count", "INFO", "ticks table not present"};
    if (cnt < 0)
        return {"gap_count", "WARN", "query error on ticks table"};

    char msg[64];
    std::snprintf(msg, sizeof(msg), "%lld ticks in last 24 h",
                  static_cast<long long>(cnt));

    if (cnt == 0 && is_rth())
        return {"gap_count", "WARN", "zero ticks during RTH — possible data gap"};
    return {"gap_count", "PASS", msg};
}

// 4. Session health
static CheckResult check_session_health(PGconn* conn) {
    bool missing = false;
    const char* sql =
        "SELECT COUNT(*) FROM sessions "
        "WHERE ended_at IS NULL AND started_at < NOW()-INTERVAL '2 hours'";

    int64_t cnt = query_int64(conn, sql, &missing);
    if (missing)
        return {"session_health", "INFO", "sessions table not present"};
    if (cnt < 0)
        return {"session_health", "WARN", "query error on sessions table"};

    char msg[64];
    std::snprintf(msg, sizeof(msg), "%lld stale open session(s) (>2 h)",
                  static_cast<long long>(cnt));

    if (cnt > 0)
        return {"session_health", "WARN", msg};
    return {"session_health", "PASS", "no stale sessions"};
}

// 5. PnL sanity
static CheckResult check_pnl_sanity(PGconn* conn) {
    bool missing = false;
    const char* sql =
        "SELECT COALESCE(SUM(pnl),0) FROM trades WHERE session_date=CURRENT_DATE";

    // Detect missing table first with a cheap count
    {
        bool miss2 = false;
        query_int64(conn,
            "SELECT COUNT(*) FROM trades WHERE session_date=CURRENT_DATE",
            &miss2);
        if (miss2)
            return {"pnl_sanity", "INFO", "trades table not present"};
    }

    bool is_null = false;
    double pnl = query_double(conn, sql, &is_null);
    if (is_null && missing)
        return {"pnl_sanity", "INFO", "trades table not present"};

    char msg[128];
    std::snprintf(msg, sizeof(msg), "daily PnL = %.2f", pnl);
    if (pnl < -2000.0)
        return {"pnl_sanity", "FAIL",
                std::string(msg) + " (threshold -2000)"};
    if (pnl < -1000.0)
        return {"pnl_sanity", "WARN",
                std::string(msg) + " (threshold -1000)"};
    return {"pnl_sanity", "PASS", msg};
}

// 6. Slippage sanity
static CheckResult check_slippage_sanity(PGconn* conn) {
    const char* sql =
        "SELECT AVG(ABS(entry_price - stop_loss)) FROM trades "
        "WHERE session_date=CURRENT_DATE AND stop_loss IS NOT NULL";

    // Detect missing table
    {
        bool miss2 = false;
        query_int64(conn,
            "SELECT COUNT(*) FROM trades WHERE session_date=CURRENT_DATE",
            &miss2);
        if (miss2)
            return {"slippage_sanity", "INFO", "trades table not present"};
    }

    bool is_null = false;
    double avg_slip = query_double(conn, sql, &is_null);
    if (is_null)
        return {"slippage_sanity", "INFO", "no trades with stop_loss today"};

    char msg[128];
    std::snprintf(msg, sizeof(msg), "avg entry-to-stop distance = %.2f pts", avg_slip);
    if (avg_slip > 10.0)
        return {"slippage_sanity", "WARN", msg};
    return {"slippage_sanity", "PASS", msg};
}

// 7. Trade table consistency
static CheckResult check_trade_table_consistency(PGconn* conn) {
    bool missing = false;
    const char* sql =
        "SELECT COUNT(*) FROM trades "
        "WHERE exit_time IS NULL AND entry_time < NOW()-INTERVAL '8 hours'";

    int64_t cnt = query_int64(conn, sql, &missing);
    if (missing)
        return {"trade_table_consistency", "INFO", "trades table not present"};
    if (cnt < 0)
        return {"trade_table_consistency", "WARN", "query error on trades table"};

    char msg[64];
    std::snprintf(msg, sizeof(msg), "%lld open trade(s) older than 8 h",
                  static_cast<long long>(cnt));

    if (cnt > 0)
        return {"trade_table_consistency", "WARN", msg};
    return {"trade_table_consistency", "PASS", "no stale open trades"};
}

// ─────────────────────────────────────────────────────────────────────────────
// System checks (no DB)
// ─────────────────────────────────────────────────────────────────────────────

// 8. Disk space
static CheckResult check_disk_space() {
    struct statvfs st{};
    if (statvfs(".", &st) != 0) {
        char msg[64];
        std::snprintf(msg, sizeof(msg), "statvfs failed: %s", std::strerror(errno));
        return {"disk_space", "WARN", msg};
    }

    double free_gb = static_cast<double>(st.f_bavail) *
                     static_cast<double>(st.f_frsize) / (1024.0 * 1024.0 * 1024.0);
    char msg[64];
    std::snprintf(msg, sizeof(msg), "%.1f GB free", free_gb);

    if (free_gb < 5.0)  return {"disk_space", "FAIL", msg};
    if (free_gb < 10.0) return {"disk_space", "WARN", msg};
    return {"disk_space", "PASS", msg};
}

// 9. RAM usage
static CheckResult check_ram_usage() {
    std::ifstream f("/proc/meminfo");
    if (!f.is_open())
        return {"ram_usage", "WARN", "cannot open /proc/meminfo"};

    int64_t avail_kb = -1;
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("MemAvailable:", 0) == 0) {
            std::istringstream ss(line);
            std::string key;
            int64_t val;
            ss >> key >> val;
            avail_kb = val;
            break;
        }
    }

    if (avail_kb < 0)
        return {"ram_usage", "WARN", "MemAvailable not found in /proc/meminfo"};

    double avail_gb = static_cast<double>(avail_kb) / (1024.0 * 1024.0);
    char msg[64];
    std::snprintf(msg, sizeof(msg), "%.2f GB available", avail_gb);

    if (avail_gb < 2.0) return {"ram_usage", "FAIL", msg};
    if (avail_gb < 4.0) return {"ram_usage", "WARN", msg};
    return {"ram_usage", "PASS", msg};
}

// 10. WAL health
static CheckResult check_wal_health() {
    fs::path wal("ticks.wal");
    std::error_code ec;
    bool exists = fs::exists(wal, ec);

    if (ec || !exists)
        return {"wal_health", "INFO", "no WAL file (clean start)"};

    auto sz = fs::file_size(wal, ec);
    if (ec)
        return {"wal_health", "WARN", "cannot stat ticks.wal"};

    double sz_mb = static_cast<double>(sz) / (1024.0 * 1024.0);
    char msg[64];
    std::snprintf(msg, sizeof(msg), "WAL size: %.1f MB", sz_mb);

    if (sz_mb > 100.0) return {"wal_health", "WARN", msg};
    return {"wal_health", "PASS", msg};
}

// 11. Drift halt
static CheckResult check_drift_halt() {
    fs::path halt_file("data/DRIFT_HALT");
    std::error_code ec;
    if (!fs::exists(halt_file, ec))
        return {"drift_halt", "PASS", "no DRIFT_HALT file"};

    // Read first line of the file as reason
    std::string reason;
    std::ifstream f(halt_file);
    if (f.is_open()) std::getline(f, reason);
    if (reason.empty()) reason = "(empty)";

    return {"drift_halt", "FAIL",
            "DRIFT_HALT present: " + reason};
}

// 12. Log file sizes
static CheckResult check_log_file_sizes() {
    fs::path log_dir("data/logs");
    std::error_code ec;

    if (!fs::exists(log_dir, ec) || !fs::is_directory(log_dir, ec))
        return {"log_file_sizes", "INFO", "data/logs directory not found"};

    double total_mb = 0.0;
    int    count    = 0;

    for (auto& entry : fs::directory_iterator(log_dir, ec)) {
        if (ec) break;
        if (entry.path().extension() != ".log") continue;
        auto sz = fs::file_size(entry.path(), ec);
        if (!ec) {
            total_mb += static_cast<double>(sz) / (1024.0 * 1024.0);
            ++count;
        }
    }

    if (count == 0)
        return {"log_file_sizes", "INFO", "no .log files in data/logs"};

    char msg[96];
    std::snprintf(msg, sizeof(msg), "%.1f MB total across %d log file(s)",
                  total_mb, count);

    if (total_mb > 500.0) return {"log_file_sizes", "WARN", msg};
    return {"log_file_sizes", "PASS", msg};
}

// ─────────────────────────────────────────────────────────────────────────────
// Process checks
// ─────────────────────────────────────────────────────────────────────────────

struct ProcInfo {
    std::string comm;
    std::string state;
};

// Returns comm strings for all running processes
static std::vector<ProcInfo> scan_processes() {
    std::vector<ProcInfo> out;
    DIR* dp = opendir("/proc");
    if (!dp) return out;

    struct dirent* de;
    while ((de = readdir(dp)) != nullptr) {
        // Only numeric directories = PIDs
        bool is_pid = true;
        for (const char* p = de->d_name; *p; ++p)
            if (*p < '0' || *p > '9') { is_pid = false; break; }
        if (!is_pid) continue;

        ProcInfo info;

        // comm
        std::string comm_path = std::string("/proc/") + de->d_name + "/comm";
        std::ifstream fc(comm_path);
        if (fc.is_open()) {
            std::getline(fc, info.comm);
            // comm is limited to 15 chars by the kernel
        }

        // state (for zombie detection)
        std::string status_path = std::string("/proc/") + de->d_name + "/status";
        std::ifstream fs(status_path);
        if (fs.is_open()) {
            std::string line;
            while (std::getline(fs, line)) {
                if (line.rfind("State:", 0) == 0) {
                    // "State:\tZ (zombie)"
                    if (line.find('Z') != std::string::npos)
                        info.state = "Z";
                    break;
                }
            }
        }

        out.push_back(std::move(info));
    }
    closedir(dp);
    return out;
}

// 13. Process liveness
static CheckResult check_process_liveness() {
    auto procs = scan_processes();

    bool has_executor = false;
    bool has_engine   = false;

    for (auto& p : procs) {
        // kernel truncates comm to 15 chars
        if (p.comm == "nq_executor")    has_executor = true;
        if (p.comm == "rithmic_engine") has_engine   = true;
    }

    if (is_rth()) {
        // Both must be running during RTH
        if (!has_executor && !has_engine)
            return {"process_liveness", "FAIL",
                    "RTH: neither nq_executor nor rithmic_engine is running"};
        if (!has_executor)
            return {"process_liveness", "FAIL",
                    "RTH: nq_executor is not running"};
        if (!has_engine)
            return {"process_liveness", "FAIL",
                    "RTH: rithmic_engine is not running"};
        return {"process_liveness", "PASS",
                "both nq_executor and rithmic_engine running"};
    }

    // Outside RTH — informational only
    std::string running;
    if (has_executor) running += "nq_executor ";
    if (has_engine)   running += "rithmic_engine";
    if (running.empty()) running = "none (outside RTH)";

    return {"process_liveness", "INFO",
            "outside RTH — processes running: " + running};
}

// 14. Zombie trader
static CheckResult check_zombie_trader() {
    auto procs = scan_processes();

    std::vector<std::string> zombies;
    for (auto& p : procs) {
        if (p.state == "Z" &&
            (p.comm == "nq_executor" || p.comm == "rithmic_engine"))
            zombies.push_back(p.comm);
    }

    if (!zombies.empty()) {
        std::string names;
        for (auto& z : zombies) names += z + " ";
        return {"zombie_trader", "WARN",
                "zombie process(es) detected: " + names};
    }
    return {"zombie_trader", "PASS", "no zombie trader processes"};
}

// ─────────────────────────────────────────────────────────────────────────────
// Config checks
// ─────────────────────────────────────────────────────────────────────────────

// 15. Trading constants
static CheckResult check_trading_constants() {
    const fs::path cfg_path("config/live_config.json");
    std::error_code ec;
    if (!fs::exists(cfg_path, ec))
        return {"trading_constants", "FAIL",
                "config/live_config.json not found"};

    json j;
    try {
        std::ifstream f(cfg_path);
        j = json::parse(f);
    } catch (const std::exception& ex) {
        return {"trading_constants", "FAIL",
                std::string("JSON parse error: ") + ex.what()};
    }

    std::vector<std::string> issues;

    // symbol (string)
    if (!j.contains("symbol") || !j["symbol"].is_string())
        issues.push_back("missing/invalid 'symbol'");

    // point_value (2.0 for MNQ or 20.0 for NQ)
    if (!j.contains("point_value") || !j["point_value"].is_number()) {
        issues.push_back("missing/invalid 'point_value'");
    } else {
        double pv = j["point_value"].get<double>();
        if (pv != 2.0 && pv != 20.0)
            issues.push_back("point_value must be 2.0 (MNQ) or 20.0 (NQ), got " +
                             std::to_string(pv));
    }

    // tick_size (number)
    if (!j.contains("tick_size") && !j.contains("orb")) {
        // fall back to orb.tick_size
        issues.push_back("missing 'tick_size'");
    } else if (j.contains("tick_size") && !j["tick_size"].is_number()) {
        issues.push_back("'tick_size' is not a number");
    } else if (!j.contains("tick_size") && j.contains("orb")) {
        auto& orb = j["orb"];
        if (!orb.contains("tick_size") || !orb["tick_size"].is_number())
            issues.push_back("missing 'tick_size' (checked orb.tick_size too)");
    }

    // max_contracts (int ≥ 1) — accept qty as alias
    if (j.contains("max_contracts")) {
        if (!j["max_contracts"].is_number_integer() ||
            j["max_contracts"].get<int>() < 1)
            issues.push_back("'max_contracts' must be integer ≥ 1");
    } else if (j.contains("qty")) {
        if (!j["qty"].is_number_integer() || j["qty"].get<int>() < 1)
            issues.push_back("'qty' (max_contracts alias) must be integer ≥ 1");
    } else {
        issues.push_back("missing 'max_contracts' (or 'qty')");
    }

    // stop_loss_points (number > 0) — accept sl_points as alias
    double slp = 0.0;
    if (j.contains("stop_loss_points")) {
        if (!j["stop_loss_points"].is_number() ||
            (slp = j["stop_loss_points"].get<double>()) <= 0.0)
            issues.push_back("'stop_loss_points' must be > 0");
    } else if (j.contains("sl_points")) {
        if (!j["sl_points"].is_number() ||
            (slp = j["sl_points"].get<double>()) <= 0.0)
            issues.push_back("'sl_points' (stop_loss_points alias) must be > 0");
    } else {
        issues.push_back("missing 'stop_loss_points' (or 'sl_points')");
    }

    if (!issues.empty()) {
        std::string msg = "validation issues: ";
        for (size_t i = 0; i < issues.size(); ++i) {
            if (i) msg += "; ";
            msg += issues[i];
        }
        return {"trading_constants", "WARN", msg};
    }
    return {"trading_constants", "PASS", "all trading constants valid"};
}

// 16. Config schema
static CheckResult check_config_schema() {
    const fs::path cfg_path("config/live_config.json");
    std::error_code ec;
    if (!fs::exists(cfg_path, ec))
        return {"config_schema", "FAIL", "config/live_config.json not found"};

    json j;
    try {
        std::ifstream f(cfg_path);
        j = json::parse(f);
    } catch (const std::exception& ex) {
        return {"config_schema", "FAIL",
                std::string("JSON parse error: ") + ex.what()};
    }

    std::vector<std::string> issues;

    // dry_run (bool)
    if (!j.contains("dry_run") || !j["dry_run"].is_boolean())
        issues.push_back("missing/invalid 'dry_run' (must be bool)");

    // account_id (non-empty string)
    if (!j.contains("account_id") || !j["account_id"].is_string() ||
        j["account_id"].get<std::string>().empty())
        issues.push_back("missing/empty 'account_id'");

    // trade_route (string, must not be "simulator")
    if (!j.contains("trade_route") || !j["trade_route"].is_string()) {
        issues.push_back("missing/invalid 'trade_route'");
    } else {
        std::string tr = j["trade_route"].get<std::string>();
        if (tr == "simulator")
            issues.push_back("'trade_route' is set to \"simulator\" — dangerous in live");
    }

    if (!issues.empty()) {
        std::string msg;
        for (size_t i = 0; i < issues.size(); ++i) {
            if (i) msg += "; ";
            msg += issues[i];
        }
        return {"config_schema", "WARN", msg};
    }
    return {"config_schema", "PASS", "config schema valid"};
}

// ─────────────────────────────────────────────────────────────────────────────
// Build / test check
// ─────────────────────────────────────────────────────────────────────────────

// 17. Run C++ tests
static CheckResult run_cpp_tests() {
    const char* binaries[] = {
        "./build/test_orb_strategy",
        "./build/test_risk_manager",
        "./build/test_validator",
    };

    int failed  = 0;
    int missing  = 0;
    int ran      = 0;
    std::string details;

    for (const char* bin : binaries) {
        fs::path p(bin);
        std::error_code ec;
        if (!fs::exists(p, ec)) {
            ++missing;
            details += std::string(bin) + " (not built); ";
            continue;
        }

        // Run the binary, capture exit code only
        std::string cmd = std::string(bin) + " > /dev/null 2>&1";
        int rc = std::system(cmd.c_str());   // NOLINT(cert-env33-c)
        ++ran;
        if (rc != 0) {
            ++failed;
            details += std::string(bin) + " FAILED (exit " +
                       std::to_string(WEXITSTATUS(rc)) + "); ";
        }
    }

    if (failed > 0)
        return {"cpp_tests", "FAIL",
                std::to_string(failed) + " test binary(ies) failed: " + details};
    if (missing > 0 && ran == 0)
        return {"cpp_tests", "WARN", "no test binaries built yet: " + details};
    if (missing > 0)
        return {"cpp_tests", "WARN",
                std::to_string(ran) + " passed, " +
                std::to_string(missing) + " missing: " + details};
    return {"cpp_tests", "PASS",
            std::to_string(ran) + " test binary(ies) all passed"};
}

// ─────────────────────────────────────────────────────────────────────────────
// Run all checks
// ─────────────────────────────────────────────────────────────────────────────

static std::vector<CheckResult> run_all_checks(PGconn* conn) {
    std::vector<CheckResult> results;
    results.reserve(17);

    // DB checks
    results.push_back(check_data_freshness(conn));
    results.push_back(check_rejection_rate(conn));
    results.push_back(check_gap_count(conn));
    results.push_back(check_session_health(conn));
    results.push_back(check_pnl_sanity(conn));
    results.push_back(check_slippage_sanity(conn));
    results.push_back(check_trade_table_consistency(conn));

    // System checks
    results.push_back(check_disk_space());
    results.push_back(check_ram_usage());
    results.push_back(check_wal_health());
    results.push_back(check_drift_halt());
    results.push_back(check_log_file_sizes());

    // Process checks
    results.push_back(check_process_liveness());
    results.push_back(check_zombie_trader());

    // Config checks
    results.push_back(check_trading_constants());
    results.push_back(check_config_schema());

    // Build check
    results.push_back(run_cpp_tests());

    return results;
}

// ─────────────────────────────────────────────────────────────────────────────
// Output: console table
// ─────────────────────────────────────────────────────────────────────────────

static void print_table(const std::vector<CheckResult>& results) {
    std::cout << "\n=== Audit Daemon " << now_utc_display() << " ===\n\n";

    // Column widths
    constexpr int NAME_W = 28;

    for (auto& r : results) {
        // Pad name
        std::string padded = r.name;
        if ((int)padded.size() < NAME_W)
            padded.append(NAME_W - padded.size(), ' ');

        std::printf("  [%-4s] %s  %s\n",
                    r.status.c_str(),
                    padded.c_str(),
                    r.message.c_str());
    }

    int passed = 0, failed = 0, warned = 0, info = 0;
    for (auto& r : results) {
        if      (r.status == "PASS") ++passed;
        else if (r.status == "FAIL") ++failed;
        else if (r.status == "WARN") ++warned;
        else                          ++info;
    }

    std::cout << "  " << std::string(60, '\xe2' == 0xe2 ? '-' : '-') << '\n';
    std::printf("  TOTAL: %d passed, %d failed, %d warned, %d info\n\n",
                passed, failed, warned, info);
}

// ─────────────────────────────────────────────────────────────────────────────
// Output: write data/audit_status.json
// ─────────────────────────────────────────────────────────────────────────────

static void write_status_json(const std::vector<CheckResult>& results,
                               const std::string& ts) {
    int passed = 0, failed = 0, warned = 0;
    for (auto& r : results) {
        if      (r.status == "PASS") ++passed;
        else if (r.status == "FAIL") ++failed;
        else if (r.status == "WARN") ++warned;
    }

    std::string overall = (failed > 0) ? "FAIL" :
                          (warned > 0) ? "WARN" : "PASS";

    json checks = json::array();
    for (auto& r : results) {
        checks.push_back({
            {"name",    r.name},
            {"status",  r.status},
            {"message", r.message}
        });
    }

    json doc = {
        {"timestamp", ts},
        {"overall",   overall},
        {"passed",    passed},
        {"failed",    failed},
        {"warned",    warned},
        {"checks",    checks}
    };

    // Ensure directory exists
    std::error_code ec;
    fs::create_directories("data", ec);

    std::ofstream f("data/audit_status.json");
    if (f.is_open())
        f << doc.dump(2) << '\n';
    else
        std::fprintf(stderr, "audit_daemon: cannot write data/audit_status.json\n");
}

// ─────────────────────────────────────────────────────────────────────────────
// Output: write quality_metrics table
// ─────────────────────────────────────────────────────────────────────────────

static void write_metrics(PGconn* conn, const std::vector<CheckResult>& results) {
    if (!conn || PQstatus(conn) != CONNECTION_OK) return;

    const char* sql =
        "INSERT INTO quality_metrics (metric, value, labels, recorded_at) "
        "VALUES ($1, $2, $3, NOW()) "
        "ON CONFLICT DO NOTHING";

    for (auto& r : results) {
        double val = 0.0;
        if      (r.status == "PASS") val = 1.0;
        else if (r.status == "WARN") val = 0.5;
        else if (r.status == "INFO") val = 0.75;
        // FAIL stays 0.0

        // Build labels JSON
        json labels = {{"status", r.status}, {"message", r.message}};
        std::string labels_str = labels.dump();
        std::string val_str    = std::to_string(val);

        const char* params[3] = {
            r.name.c_str(),
            val_str.c_str(),
            labels_str.c_str()
        };

        PGresult* res = PQexecParams(conn, sql, 3, nullptr, params,
                                     nullptr, nullptr, 0);
        if (res) PQclear(res);
        // Ignore individual errors — move on to next metric
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// One full audit pass
// ─────────────────────────────────────────────────────────────────────────────

static bool run_once(PGconn* conn) {
    std::string ts = now_utc_iso();

    auto results = run_all_checks(conn);

    print_table(results);
    write_status_json(results, ts);
    write_metrics(conn, results);

    // Return true = all OK (no FAIL)
    for (auto& r : results)
        if (r.status == "FAIL") return false;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    // ── Parse arguments ───────────────────────────────────────────────
    bool once     = false;
    int  interval = 300;   // seconds

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--once") {
            once = true;
        } else if (arg == "--interval" && i + 1 < argc) {
            interval = std::atoi(argv[++i]);
            if (interval <= 0) interval = 300;
        }
    }

    // ── Signal handling ───────────────────────────────────────────────
    struct sigaction sa{};
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT,  &sa, nullptr);

    // ── Load config and connect to PostgreSQL ─────────────────────────
    Config cfg = Config::from_env(".env");

    std::string connstr = cfg.pg_connstr();
    PGconn* conn = PQconnectdb(connstr.c_str());

    if (!conn || PQstatus(conn) != CONNECTION_OK) {
        std::fprintf(stderr, "audit_daemon: DB connection failed: %s\n",
                     conn ? PQerrorMessage(conn) : "PQconnectdb returned null");
        if (conn) PQfinish(conn);
        return 1;
    }

    std::fprintf(stderr, "audit_daemon: connected to PostgreSQL (%s/%s)\n",
                 cfg.pg_host.c_str(), cfg.pg_db.c_str());

    // ── Run ───────────────────────────────────────────────────────────
    bool all_ok = true;

    if (once) {
        all_ok = run_once(conn);
    } else {
        // Daemon loop
        while (!g_stop.load()) {
            all_ok = run_once(conn);

            // Reconnect if connection dropped mid-run
            if (PQstatus(conn) != CONNECTION_OK) {
                std::fprintf(stderr, "audit_daemon: reconnecting to DB...\n");
                PQreset(conn);
            }

            // Sleep in 1-second increments so SIGTERM is handled promptly
            for (int elapsed = 0; elapsed < interval && !g_stop.load(); ++elapsed)
                std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        std::fprintf(stderr, "audit_daemon: received signal, exiting cleanly.\n");
    }

    PQfinish(conn);
    return all_ok ? 0 : 1;
}
