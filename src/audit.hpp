#pragma once
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#include <libpq-fe.h>

// Async audit logger — writes structured events to the audit_log table.
//
// Events are buffered in memory and flushed in batches to avoid blocking
// the collector hot path. Call flush() periodically (e.g., every minute).
//
// Standard events:
//   collector.start / collector.stop
//   connection.established / connection.lost
//   error           details: "<message>"
// (ticks.written — one row per write batch — was retired 2026-09-26: it was 99.98 % of the
//  table. Tick counts are in quality_metrics.)
class AuditLog {
public:
    enum class Severity { INFO, WARN, ERROR };

    explicit AuditLog(PGconn* conn);

    // Non-blocking — appends to in-memory buffer
    void log(const std::string& event,
             const std::string& details  = "",
             Severity           severity = Severity::INFO);

    void info (const std::string& event, const std::string& details = "");
    void warn (const std::string& event, const std::string& details = "");
    void error(const std::string& event, const std::string& details = "");

    // Flush pending events to PostgreSQL (call from collector loop)
    void flush();

    // Number of events pending in buffer
    int pending() const;

private:
    struct Event {
        std::string ts;         // ISO 8601
        std::string event;
        std::string severity;
        std::string details;
    };

    static std::string now_iso();
    static std::string sev_str(Severity s);

    static constexpr size_t MAX_BUF = 10000;

    // flush() repairs a dead connection itself (PQreset, at most once per 5 s) — the
    // executor hands it a standalone PGconn* nobody else resets, so a Postgres restart
    // used to leave audit_log dark until the process was restarted (2026-09-26).
    bool ensure_connected();

    PGconn*           conn_;
    mutable std::mutex mu_;
    std::vector<Event> buf_;
    std::chrono::steady_clock::time_point last_reset_{};
    bool              reset_logged_ = false;
};
