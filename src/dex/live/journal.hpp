#pragma once
/*  journal.hpp — the live engine's record of truth: append-only JSONL, one object per line, never rewritten.

    <dir>/<label>-<UTC day>.jsonl (a new file each UTC day), opened O_APPEND|O_CREAT, mode 0600. Every record is
    written with write(2) at once; records that change exposure or state (fill, halt, reconcile decision, position,
    signed tx, send result) are fsync'ed before write() returns, everything else at least once a second (flush_due).
    ANY failure — open, short write, ENOSPC, EIO, fsync — throws JournalError and latches failed(): the engine then
    halts and cancels everything (it never trades without a record). Nothing in this file ever truncates, rewrites
    or deletes a journal.

    The journal never receives key material: callers pass tx hashes, nonces and our own fields only.          */

#include <nlohmann/json.hpp>

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace lighter::live {

struct JournalError : std::runtime_error { using std::runtime_error::runtime_error; };

inline std::string utc_day_of_ms(int64_t ms) {
    std::time_t s = std::time_t(ms / 1000);
    std::tm t{};
    gmtime_r(&s, &t);
    char b[16];
    std::strftime(b, sizeof b, "%Y-%m-%d", &t);
    return b;
}

class Journal {
public:
    Journal(std::string dir, std::string label) : dir_(std::move(dir)), label_(std::move(label)) {}
    ~Journal() { if (fd_ >= 0) { ::fsync(fd_); ::close(fd_); } }
    Journal(const Journal&) = delete;
    Journal& operator=(const Journal&) = delete;

    // Opens (or rolls to) the file of `now_ms`'s UTC day. Throws JournalError.
    void open_for(int64_t now_ms) {
        std::string day = utc_day_of_ms(now_ms);
        if (fd_ >= 0 && day == day_) return;
        std::error_code ec;
        std::filesystem::create_directories(dir_, ec);
        std::string p = dir_ + "/" + label_ + "-" + day + ".jsonl";
        open_path(p);
        day_ = day;
    }
    // Opens an explicit path (the day files go through open_for). Used to point the journal at a device for fault
    // tests (/dev/full gives ENOSPC on every write).
    void open_path(const std::string& p) {
        int fd = ::open(p.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0600);
        if (fd < 0) fail("journal open " + p + ": " + std::strerror(errno));
        if (fd_ >= 0) { ::fsync(fd_); ::close(fd_); }
        fd_ = fd;
        path_ = p;
    }

    // Append one record. `durable` → fsync before returning. Throws JournalError (and latches failed()).
    void write(nlohmann::json rec, int64_t now_ms, bool durable) {
        if (failed_) throw JournalError("journal already failed: " + why_);
        if (fd_ < 0 || utc_day_of_ms(now_ms) != day_) {
            if (path_.empty() || !day_.empty()) open_for(now_ms);
        }
        rec["t"] = now_ms;
        std::string line = rec.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "\n";
        size_t off = 0;
        while (off < line.size()) {
            ssize_t w = ::write(fd_, line.data() + off, line.size() - off);
            if (w < 0 && errno == EINTR) continue;
            if (w <= 0) fail(std::string("journal write: ") + (w < 0 ? std::strerror(errno) : "short write"));
            off += size_t(w);
        }
        bytes_ += line.size();
        ++records_;
        dirty_ = true;
        if (durable) sync(now_ms);
    }
    void sync(int64_t now_ms) {
        if (fd_ < 0 || !dirty_) { last_sync_ms_ = now_ms; return; }
        if (::fsync(fd_) != 0 && errno != EINVAL) fail(std::string("journal fsync: ") + std::strerror(errno));
        dirty_ = false;
        last_sync_ms_ = now_ms;
    }
    void flush_due(int64_t now_ms) { if (dirty_ && now_ms - last_sync_ms_ >= 1'000) sync(now_ms); }

    bool failed() const { return failed_; }
    const std::string& failure() const { return why_; }
    uint64_t records() const { return records_; }
    uint64_t bytes() const { return bytes_; }
    const std::string& path() const { return path_; }

private:
    [[noreturn]] void fail(const std::string& why) {
        failed_ = true;
        why_ = why;
        throw JournalError(why);
    }
    std::string dir_, label_, day_, path_;
    int fd_ = -1;
    bool failed_ = false, dirty_ = false;
    std::string why_;
    uint64_t records_ = 0, bytes_ = 0;
    int64_t last_sync_ms_ = 0;
};

}  // namespace lighter::live
