#pragma once
// wal.hpp — Write-Ahead Log for crash-safe batch recovery.
//
// Lifecycle:
//   1. Construct: opens the file (fd kept open for the object's lifetime)
//      and replays any uncommitted rows into memory, once.
//   2. Before every DB flush: write_batch(rows) — appends + fdatasyncs and
//      adds the rows to the in-memory pending set.
//   3. DB flush writes pending() — ALL uncommitted rows, not just the last
//      batch — so batches from earlier failed flushes drain too.
//   4. After a successful DB write: commit() — truncates + fdatasyncs and
//      clears pending().
//
// If the process crashes between steps 2 and 4, pending rows are replayed
// at the next start.  Duplicates are handled by the DB's ON CONFLICT
// clauses.
//
// Size cap: size_bytes() tracks the file size; while the DB is unreachable
// the WAL grows without bound, so callers should alert when over_cap()
// flips true (data is still written — the cap only drives the alert).
//
// Uses POSIX open/write/fdatasync for true crash safety.
// std::ofstream flush() only reaches the kernel buffer; fdatasync() forces
// the data to stable storage so a kernel crash cannot lose a committed WAL.

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

template <typename Row>
class Wal {
public:
    // ToLine serializes one row to a single line (no '\n').
    // FromLine parses one line back; returns false for malformed/partial
    // lines (tail of file at a crash boundary).
    using ToLine   = std::function<std::string(const Row&)>;
    using FromLine = std::function<bool(const std::string&, Row&)>;

    // Alert threshold — the WAL holding this many bytes means the DB has
    // been down for a long time.
    static constexpr int64_t DEFAULT_MAX_BYTES = 64LL * 1024 * 1024;  // 64 MiB

    Wal(std::string path, ToLine to_line, FromLine from_line,
        int64_t max_bytes = DEFAULT_MAX_BYTES)
        : path_(std::move(path)),
          to_line_(std::move(to_line)),
          from_line_(std::move(from_line)),
          max_bytes_(max_bytes)
    {
        fd_ = ::open(path_.c_str(), O_RDWR | O_CREAT | O_APPEND, 0644);
        if (fd_ < 0)
            throw std::runtime_error("WAL open failed: " + path_ +
                                     " (" + std::strerror(errno) + ")");
        replay_once();
    }

    ~Wal() { if (fd_ >= 0) ::close(fd_); }

    Wal(const Wal&)            = delete;
    Wal& operator=(const Wal&) = delete;

    // Append a batch to the WAL file (called before DB write).
    // Guarantees the data reaches stable storage (fdatasync) before returning.
    // On failure nothing is added to pending() — the caller re-queues.
    void write_batch(const std::vector<Row>& rows) {
        if (rows.empty()) return;

        // Build the entire block in memory first
        std::string buf;
        buf.reserve(rows.size() * 96);
        for (auto& r : rows) {
            buf += to_line_(r);
            buf += '\n';
        }

        const char* p   = buf.data();
        std::size_t rem = buf.size();
        while (rem > 0) {
            ssize_t n = ::write(fd_, p, rem);
            if (n < 0)
                throw std::runtime_error(std::string("WAL write failed: ") +
                                         std::strerror(errno));
            p   += n;
            rem -= static_cast<std::size_t>(n);
        }
        if (::fdatasync(fd_) != 0)   // force to stable storage
            throw std::runtime_error(std::string("WAL fdatasync failed: ") +
                                     std::strerror(errno));

        pending_.insert(pending_.end(), rows.begin(), rows.end());
        size_bytes_ += static_cast<int64_t>(buf.size());
    }

    // Truncate the WAL to zero after a confirmed DB write.
    // Throws on failure — pending rows stay tracked and are retried
    // (the DB dedups them via ON CONFLICT).
    void commit() {
        if (::ftruncate(fd_, 0) != 0)
            throw std::runtime_error(std::string("WAL truncate failed: ") +
                                     std::strerror(errno));
        ::fdatasync(fd_);
        pending_.clear();
        size_bytes_ = 0;
    }

    // All rows written to the WAL but not yet committed.  Replayed from
    // disk once at construction, then tracked in memory — no per-flush
    // file reads (previously O(n²) while the DB was down).
    const std::vector<Row>& pending() const { return pending_; }

    // True if there are uncommitted rows (data to flush/replay)
    bool dirty() const { return !pending_.empty(); }

    bool exists() const { return ::access(path_.c_str(), F_OK) == 0; }

    int64_t size_bytes() const { return size_bytes_; }
    bool    over_cap()   const { return size_bytes_ > max_bytes_; }

    const std::string& path() const { return path_; }

private:
    // Read the file once at startup; the write fd stays open.
    void replay_once() {
        struct stat st{};
        if (::fstat(fd_, &st) == 0) size_bytes_ = st.st_size;
        if (size_bytes_ == 0) return;

        FILE* f = ::fopen(path_.c_str(), "r");
        if (!f) return;
        char*  line = nullptr;
        size_t cap  = 0;
        ssize_t len;
        while ((len = ::getline(&line, &cap, f)) > 0) {
            std::string s(line, static_cast<size_t>(len));
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
                s.pop_back();
            if (s.empty()) continue;
            Row r;
            if (from_line_(s, r))
                pending_.push_back(std::move(r));
            // malformed/partial lines (crash-boundary tail) are skipped
        }
        std::free(line);
        std::fclose(f);
    }

    std::string      path_;
    ToLine           to_line_;
    FromLine         from_line_;
    int64_t          max_bytes_;
    int              fd_         = -1;
    int64_t          size_bytes_ = 0;
    std::vector<Row> pending_;
};
