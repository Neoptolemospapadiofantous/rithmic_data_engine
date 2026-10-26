#pragma once
/*  instance_lock.hpp — one mm_live per state directory (R-NET-8): an exclusive, non-blocking flock on
    <state_dir>/mm_live.lock held for the life of the process. A second copy gets "instance lock REFUSED".
    The kernel drops the lock when the process dies (kill -9 included), so a crash never leaves a stale lock.   */

#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>

#include <optional>
#include <string>
#include <utility>

namespace lighter::live {

class InstanceLock {
public:
    static std::optional<InstanceLock> acquire(const std::string& path) {
        int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (fd < 0) return std::nullopt;
        if (::flock(fd, LOCK_EX | LOCK_NB) != 0) { ::close(fd); return std::nullopt; }
        std::string pid = std::to_string(::getpid()) + "\n";
        if (::ftruncate(fd, 0) == 0) { ssize_t w = ::write(fd, pid.data(), pid.size()); (void)w; }
        return InstanceLock(fd);
    }
    InstanceLock(InstanceLock&& o) noexcept : fd_(std::exchange(o.fd_, -1)) {}
    InstanceLock& operator=(InstanceLock&& o) noexcept { if (this != &o) { release(); fd_ = std::exchange(o.fd_, -1); } return *this; }
    InstanceLock(const InstanceLock&) = delete;
    InstanceLock& operator=(const InstanceLock&) = delete;
    ~InstanceLock() { release(); }

private:
    explicit InstanceLock(int fd) : fd_(fd) {}
    void release() { if (fd_ >= 0) { ::flock(fd_, LOCK_UN); ::close(fd_); fd_ = -1; } }
    int fd_ = -1;
};

}  // namespace lighter::live
