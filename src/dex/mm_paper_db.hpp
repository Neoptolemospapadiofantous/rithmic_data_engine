#pragma once
// mm_paper_db.hpp — the paper market maker's Postgres write path, minus libpq (audit 2026-10-26: the old writer ran a
// whole batch in one BEGIN/COMMIT, so the first failing statement aborted every row after it, nothing was retried and a
// connection lost mid-batch lost the batch — all silently). Pure, unit-tested by tests/dex/test_lighter.cpp;
// mm_paper_main.cpp supplies the statement executor.
//
//   * run_batch: every statement on its own (autocommit). A statement that fails on a live connection is logged by the
//     caller with its error and counted as a DROP; the rest still apply. A lost connection stops the batch at that
//     statement and reports where, so the caller reconnects and retries from there. The statement in flight when the
//     connection died may or may not have committed, so every insert must be idempotent: fills and events carry a
//     unique `uid` (ON CONFLICT (uid) DO NOTHING), equity rows their primary key, daily rows an upsert.
//   * DropLedger: drops per UTC day + total, persisted to a JSON file (data/mm_paper/db_drops_<label>.json) so a
//     restart never resets the evidence; `since` = the first day the ledger covered (a day before it is unknown, and
//     scripts/mm_gates.sh does not count it as complete).

#include <nlohmann/json.hpp>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace mmpaper {

// day = the row's own UTC day (from its timestamp); a drop is counted against it. Empty = the wall-clock day of the drop.
struct Stmt { std::string sql; std::vector<std::string> params; std::string day; };
inline std::string drop_day(const Stmt& s, const std::string& wall_day) { return s.day.empty() ? wall_day : s.day; }

// A multi-statement script split at ';' into single statements (no ';' inside a statement), blanks dropped — the
// writer runs its DDL one statement at a time under a short lock_timeout.
inline std::vector<std::string> split_sql(const std::string& script) {
    std::vector<std::string> out;
    size_t b = 0;
    while (b < script.size()) {
        size_t e = script.find(';', b);
        if (e == std::string::npos) e = script.size();
        std::string st = script.substr(b, e - b);
        const auto f = st.find_first_not_of(" \t\r\n");
        if (f != std::string::npos) out.push_back(st.substr(f));
        b = e + 1;
    }
    return out;
}

enum class ExecStatus { Ok, Error, ConnLost };
struct ExecResult { ExecStatus status = ExecStatus::Ok; std::string error; int64_t rows = -1; };   // rows: rows affected (-1 = unknown)
using ExecFn = std::function<ExecResult(const std::string& sql, const std::vector<std::string>& params)>;

struct BatchOutcome {
    size_t applied = 0;
    std::vector<size_t> no_rows;                           // applied but affected 0 rows (e.g. ON CONFLICT DO NOTHING hit an existing row)
    std::vector<std::pair<size_t, std::string>> failed;   // (statement index, error) — dropped
    bool conn_lost = false;
    size_t resume_at = 0;                                  // with conn_lost: retry the batch from this statement
    std::string conn_error;
};

inline BatchOutcome run_batch(const std::vector<Stmt>& batch, const ExecFn& exec) {
    BatchOutcome o;
    for (size_t i = 0; i < batch.size(); ++i) {
        const ExecResult r = exec(batch[i].sql, batch[i].params);
        if (r.status == ExecStatus::Ok) { ++o.applied; if (r.rows == 0) o.no_rows.push_back(i); continue; }
        if (r.status == ExecStatus::ConnLost) { o.conn_lost = true; o.resume_at = i; o.conn_error = r.error; return o; }
        o.failed.emplace_back(i, r.error);
    }
    return o;
}

class DropLedger;

// Quote rows' own queue (observability, 2026-10-26): mm_paper_quotes rows must never cost a fill / event / equity / daily row
// its place, nor count against the drop ledger the G2 gate reads. Bounded at `cap`; when full, the OLDEST quote rows are shed
// and counted in the quote ledger (a separate DropLedger, file db_quote_drops_<label>.json, which scripts/mm_gates.sh never reads).
class ShedQueue {
public:
    explicit ShedQueue(size_t cap) : cap_(cap) {}
    // Returns how many rows were shed to make room.
    size_t push(Stmt s, DropLedger& led, const std::string& wall_day);
    // Put rows [from, end) of a batch back at the FRONT (they keep their order, they are older than anything queued since).
    size_t requeue(std::vector<Stmt>& v, size_t from, DropLedger& led, const std::string& wall_day);
    std::vector<Stmt> take_all() {
        std::vector<Stmt> v(std::make_move_iterator(q_.begin()), std::make_move_iterator(q_.end()));
        q_.clear();
        return v;
    }
    size_t size() const { return q_.size(); }
    bool empty() const { return q_.empty(); }
    size_t cap() const { return cap_; }
    const std::deque<Stmt>& rows() const { return q_; }

private:
    size_t trim(DropLedger& led, const std::string& wall_day);
    size_t cap_;
    std::deque<Stmt> q_;
};

class DropLedger {
public:
    int64_t total() const { return total_; }
    int64_t on(const std::string& day) const { auto it = days_.find(day); return it == days_.end() ? 0 : it->second; }
    const std::string& since() const { return since_; }
    void add(const std::string& day, int64_t n = 1) {
        if (n <= 0) return;
        total_ += n;
        days_[day] += n;
        dirty_ = true;
    }
    // Start covering from `today` if this ledger is new (no file, or a file that could not be read).
    void start(const std::string& today) { if (since_.empty()) { since_ = today; dirty_ = true; } }
    bool dirty() const { return dirty_; }

    nlohmann::json to_json() const {
        nlohmann::json d = nlohmann::json::object();
        for (const auto& [k, v] : days_) d[k] = v;
        return {{"since", since_}, {"total", total_}, {"days", d}};
    }
    // false = no file or unreadable (the ledger then starts empty; start() stamps a new `since`).
    bool load(const std::filesystem::path& f) {
        std::ifstream in(f);
        if (!in) return false;
        try {
            nlohmann::json j = nlohmann::json::parse(in);
            DropLedger l;
            l.since_ = j.value("since", "");
            l.total_ = j.value("total", int64_t{0});
            if (j.contains("days") && j["days"].is_object())
                for (const auto& [k, v] : j["days"].items()) l.days_[k] = v.get<int64_t>();
            *this = std::move(l);
            return !since_.empty();
        } catch (const std::exception&) {
            return false;
        }
    }
    bool save(const std::filesystem::path& f) {
        std::error_code ec;
        if (f.has_parent_path()) std::filesystem::create_directories(f.parent_path(), ec);
        const std::filesystem::path tmp = f.string() + ".tmp";
        {
            std::ofstream out(tmp);
            if (!out) return false;
            out << to_json().dump(1);
            if (!out) return false;
        }
        std::filesystem::rename(tmp, f, ec);
        if (ec) return false;
        dirty_ = false;
        return true;
    }

private:
    std::string since_;
    int64_t total_ = 0;
    std::map<std::string, int64_t> days_;
    bool dirty_ = false;
};

inline size_t ShedQueue::trim(DropLedger& led, const std::string& wall_day) {
    size_t n = 0;
    while (q_.size() > cap_) { led.add(drop_day(q_.front(), wall_day), 1); q_.pop_front(); ++n; }
    return n;
}
inline size_t ShedQueue::push(Stmt s, DropLedger& led, const std::string& wall_day) {
    q_.push_back(std::move(s));
    return trim(led, wall_day);
}
inline size_t ShedQueue::requeue(std::vector<Stmt>& v, size_t from, DropLedger& led, const std::string& wall_day) {
    for (size_t i = v.size(); i-- > from;) q_.push_front(std::move(v[i]));
    return trim(led, wall_day);
}

}  // namespace mmpaper
