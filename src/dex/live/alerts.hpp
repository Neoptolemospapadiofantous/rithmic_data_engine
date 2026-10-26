#pragma once
/*  alerts.hpp — operator alerts through ~/.local/bin/grid-notify (Telegram), FAIL-OPEN and rate-limited.

    alert(key, text): the first line for a key goes out at once; repeats of the same key are suppressed for
    `min_interval_ms` (default 5 min) and the next one that goes out says how many were suppressed. A sink failure
    never reaches the caller. The real sink spawns grid-notify without waiting (posix_spawn, stdin/stdout/stderr to
    /dev/null, no shell, the environment passed through — it never holds key material) and reaps finished children
    on later calls. Tests and the offline soak use a capturing sink: they never message the founder.         */

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>

extern char** environ;

namespace lighter::live {

class Alerter {
public:
    using Sink = std::function<void(const std::string& line)>;

    explicit Alerter(Sink sink, int64_t min_interval_ms = 300'000) : sink_(std::move(sink)), min_(min_interval_ms) {}

    // Returns true when the line went to the sink.
    bool alert(const std::string& key, const std::string& text, int64_t now_ms) {
        auto& k = keys_[key];
        if (k.sent && now_ms - k.last_ms < min_) { ++k.suppressed; ++suppressed_total_; return false; }
        std::string line = text;
        if (k.suppressed) line += " (+" + std::to_string(k.suppressed) + " similar suppressed)";
        k.suppressed = 0;
        k.last_ms = now_ms;
        k.sent = true;
        ++sent_;
        try { if (sink_) sink_(line); } catch (...) {}
        return true;
    }
    int64_t sent() const { return sent_; }
    int64_t suppressed() const { return suppressed_total_; }

    // posix_spawn grid-notify "<line>", fire-and-forget. Missing binary → silently nothing (fail-open).
    static Sink grid_notify(std::string path = "") {
        if (path.empty()) {
            const char* home = std::getenv("HOME");
            path = std::string(home ? home : "") + "/.local/bin/grid-notify";
        }
        auto children = std::make_shared<std::vector<pid_t>>();
        return [path, children](const std::string& line) {
            for (auto it = children->begin(); it != children->end();) {   // reap finished ones
                int st = 0;
                it = ::waitpid(*it, &st, WNOHANG) != 0 ? children->erase(it) : std::next(it);
            }
            if (::access(path.c_str(), X_OK) != 0) return;
            posix_spawn_file_actions_t fa;
            posix_spawn_file_actions_init(&fa);
            for (int fd : {0, 1, 2}) posix_spawn_file_actions_addopen(&fa, fd, "/dev/null", fd == 0 ? O_RDONLY : O_WRONLY, 0);
            std::string arg = "[mm_live] " + line;
            char* argv[] = {const_cast<char*>(path.c_str()), const_cast<char*>(arg.c_str()), nullptr};
            pid_t pid = 0;
            if (posix_spawn(&pid, path.c_str(), &fa, nullptr, argv, environ) == 0) children->push_back(pid);
            posix_spawn_file_actions_destroy(&fa);
        };
    }

private:
    struct Key { int64_t last_ms = 0; int64_t suppressed = 0; bool sent = false; };
    Sink sink_;
    int64_t min_;
    std::map<std::string, Key> keys_;
    int64_t sent_ = 0, suppressed_total_ = 0;
};

}  // namespace lighter::live
