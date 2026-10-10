#pragma once
// tick_fanout.hpp — localhost fan-out of every trade / quote a collector parses (founder 2026-10-10:
// "has to be tick", "do the same for NQ"). Used by crypto_collector and the Rithmic tick collector.
//
// A TCP server bound to 127.0.0.1 only. Every connected client receives one JSON object per line:
//   {"n":<line no>,"k":"hello","src":"crypto"}                       on connect (n = next line number − 1)
//   {"n":N,"k":"t","s":SYM,"ts":<trade time µs>,"p":price,"q":size,"b":1|0,"seq":seq,"E":<event ms>,"r":<recv µs>}
//   {"n":N,"k":"q","s":SYM,"ts":<µs>,"bp":bid,"bq":bid size,"ap":ask,"aq":ask size,"E":<ms>,"r":<µs>}
//   {"n":N,"k":"m","s":PAIR,"v":VENUE,"minute":<µs>,"complete":true|false,"r":<µs>}   one mbo_1m row closed
// q / bq / aq are the collector's STORED size units (crypto 0.001-coin lots, CME contracts); ts, seq and q
// match the row written to Postgres, so a consumer can de-duplicate fan-out and DB rows by (ts, seq).
// `n` increases by exactly 1 per published line: a jump means lines were dropped (queue contended or full)
// and the consumer must re-read that span from Postgres.
//
// NEVER BLOCKS THE FEED (the DB write path is untouched and shares nothing with this):
//   publish()  — the feed thread: one relaxed atomic load when nobody is connected; otherwise format the
//                line, try_lock (never lock) a mutex owned by this class alone, push into a bounded queue
//                and poke a non-blocking pipe. Contended lock or full queue → the line is dropped, counted.
//   own thread — swaps the queue out, appends lines to each client's bounded output buffer, writes with
//                non-blocking sockets. A client whose buffer passes its cap is disconnected (its loss only).
// Port 0 = disabled. Bind failure is logged and retried every 10 s; it never stops the collector.

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fanout {

// ── pure formatting (unit-tested) ───────────────────────────────────────────────────────────────────

inline void append_num(std::string& o, double v) {
    if (!std::isfinite(v)) { o += "null"; return; }
    char b[40];
    std::snprintf(b, sizeof b, "%.10g", v);
    o += b;
}
inline void append_int(std::string& o, int64_t v) { o += std::to_string(v); }
inline void append_str(std::string& o, const std::string& s) {
    o += '"';
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (static_cast<unsigned char>(c) < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += c;
    }
    o += '"';
}

// Body of a trade line (everything after {"n":N,). event_ms <= 0 → "E":null.
inline std::string trade_body(const std::string& sym, int64_t ts_us, double price, int64_t size, bool is_buy,
                              int64_t seq, int64_t event_ms, int64_t recv_us) {
    std::string o;
    o.reserve(128);
    o += "\"k\":\"t\",\"s\":"; append_str(o, sym);
    o += ",\"ts\":"; append_int(o, ts_us);
    o += ",\"p\":"; append_num(o, price);
    o += ",\"q\":"; append_int(o, size);
    o += ",\"b\":"; o += is_buy ? '1' : '0';
    o += ",\"seq\":"; append_int(o, seq);
    o += ",\"E\":"; if (event_ms > 0) append_int(o, event_ms); else o += "null";
    o += ",\"r\":"; append_int(o, recv_us);
    return o;
}

// Body of a quote line. A missing side (NaN price) is written as null.
inline std::string quote_body(const std::string& sym, int64_t ts_us, double bid, int64_t bid_size, double ask,
                              int64_t ask_size, int64_t event_ms, int64_t recv_us) {
    std::string o;
    o.reserve(128);
    o += "\"k\":\"q\",\"s\":"; append_str(o, sym);
    o += ",\"ts\":"; append_int(o, ts_us);
    o += ",\"bp\":"; append_num(o, bid);
    o += ",\"bq\":"; append_int(o, bid_size);
    o += ",\"ap\":"; append_num(o, ask);
    o += ",\"aq\":"; append_int(o, ask_size);
    o += ",\"E\":"; if (event_ms > 0) append_int(o, event_ms); else o += "null";
    o += ",\"r\":"; append_int(o, recv_us);
    return o;
}

inline std::string minute_body(const std::string& pair, const std::string& venue, int64_t minute_us,
                               bool complete, int64_t recv_us) {
    std::string o;
    o += "\"k\":\"m\",\"s\":"; append_str(o, pair);
    o += ",\"v\":"; append_str(o, venue);
    o += ",\"minute\":"; append_int(o, minute_us);
    o += ",\"complete\":"; o += complete ? "true" : "false";
    o += ",\"r\":"; append_int(o, recv_us);
    return o;
}

// Rithmic ticks carry no exchange sequence: the DB writer (db.cpp TickDB::write) sets seq = rank of the row
// among identical (symbol, exchange, ts, price, size) rows in its batch. The fan-out reproduces that for
// consecutive identical trades (which land in one batch): same key as the previous trade → rank + 1, else 0.
inline int next_rank(bool same_as_previous, int previous_rank) { return same_as_previous ? previous_rank + 1 : 0; }

inline std::string make_line(uint64_t n, const std::string& body) {
    std::string o;
    o.reserve(body.size() + 24);
    o += "{\"n\":"; o += std::to_string(n); o += ','; o += body; o += "}\n";
    return o;
}

// ── bounded line queue: producer never blocks ─────────────────────────────────────────────────────────
class LineQueue {
public:
    explicit LineQueue(size_t cap) : cap_(cap) {}
    // false = dropped (lock held by the consumer right now, or full). *was_empty tells the caller to wake.
    bool try_push(std::string&& line, bool* was_empty = nullptr) {
        std::unique_lock lk(mu_, std::try_to_lock);
        if (!lk.owns_lock() || q_.size() >= cap_) return false;
        if (was_empty) *was_empty = q_.empty();
        q_.push_back(std::move(line));
        return true;
    }
    void take(std::vector<std::string>& out) {      // consumer only; blocks only against a producer's push
        std::lock_guard lk(mu_);
        out.swap(q_);
        q_.clear();
    }
    size_t cap() const { return cap_; }
private:
    std::mutex mu_;
    std::vector<std::string> q_;
    size_t cap_;
};

// Per-client output buffer with a hard cap (bytes). append() false = over cap → drop the client.
class ClientBuf {
public:
    explicit ClientBuf(size_t cap) : cap_(cap) {}
    bool append(const std::string& s) {
        if (pending() + s.size() > cap_) return false;
        if (off_ > 0 && off_ >= buf_.size() / 2) { buf_.erase(0, off_); off_ = 0; }
        buf_ += s;
        return true;
    }
    size_t pending() const { return buf_.size() - off_; }
    const char* data() const { return buf_.data() + off_; }
    void consume(size_t n) { off_ += n; if (off_ >= buf_.size()) { buf_.clear(); off_ = 0; } }
private:
    std::string buf_;
    size_t off_ = 0, cap_;
};

struct Stats {
    std::atomic<uint64_t> published{0};      // lines accepted into the queue
    std::atomic<uint64_t> dropped{0};        // lines not queued (contention / full)
    std::atomic<uint64_t> clients{0};        // connected now
    std::atomic<uint64_t> clients_total{0};
    std::atomic<uint64_t> clients_dropped{0};// disconnected for falling behind
};

// ── the server ───────────────────────────────────────────────────────────────────────────────────────
class Server {
public:
    using LogFn = std::function<void(const std::string&)>;
    static constexpr size_t QUEUE_CAP = 65536;           // lines between two consumer passes
    static constexpr size_t CLIENT_CAP = 8u << 20;       // bytes buffered per client

    Server(std::string source, int port, LogFn log = {}, size_t queue_cap = QUEUE_CAP,
           size_t client_cap = CLIENT_CAP)
        : source_(std::move(source)), port_(port), log_(std::move(log)), q_(queue_cap), client_cap_(client_cap) {}
    ~Server() { stop(); }
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool enabled() const { return port_ > 0; }
    int bound_port() const { return bound_.load(); }    // 0 until listening (port 0 requests are tests only)
    const Stats& stats() const { return st_; }

    // Start the fan-out thread. `port_` 0 = disabled (no thread) unless `ephemeral` (tests: bind any port).
    void start(bool ephemeral = false) {
        if ((port_ <= 0 && !ephemeral) || th_.joinable()) return;
        if (::pipe2(wake_, O_NONBLOCK | O_CLOEXEC) != 0) { say("fanout: pipe2 failed — disabled"); return; }
        stop_.store(false);
        th_ = std::thread([this] { loop(); });
    }
    void stop() {
        if (!th_.joinable()) return;
        stop_.store(true);
        poke();
        th_.join();
        for (int i = 0; i < 2; ++i) if (wake_[i] >= 0) { ::close(wake_[i]); wake_[i] = -1; }
    }

    // Feed thread. Never blocks. Returns true if queued.
    bool publish(const std::string& body) {
        if (st_.clients.load(std::memory_order_relaxed) == 0) return false;   // nobody listening: no work
        const uint64_t n = next_n_.fetch_add(1, std::memory_order_relaxed) + 1;
        bool was_empty = false;
        if (!q_.try_push(make_line(n, body), &was_empty)) { st_.dropped.fetch_add(1, std::memory_order_relaxed); return false; }
        st_.published.fetch_add(1, std::memory_order_relaxed);
        if (was_empty) poke();
        return true;
    }
    bool listening() const { return st_.clients.load() > 0; }

private:
    struct Client { int fd; ClientBuf buf; };

    void say(const std::string& s) { if (log_) log_(s); }
    void poke() { if (wake_[1] >= 0) { char c = 1; ssize_t r = ::write(wake_[1], &c, 1); (void)r; } }

    int open_listener() {
        int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd < 0) return -1;
        int one = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(static_cast<uint16_t>(port_ > 0 ? port_ : 0));
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);                    // 127.0.0.1 only
        if (::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(fd, 16) != 0) {
            say("fanout: cannot listen on 127.0.0.1:" + std::to_string(port_) + " (" + std::strerror(errno) +
                ") — retrying in 10 s; the collector is unaffected");
            ::close(fd);
            return -1;
        }
        socklen_t len = sizeof a;
        ::getsockname(fd, reinterpret_cast<sockaddr*>(&a), &len);
        bound_.store(ntohs(a.sin_port));
        say("fanout: listening on 127.0.0.1:" + std::to_string(bound_.load()) + " (" + source_ + ")");
        return fd;
    }

    void drop_client(size_t i, bool behind) {
        ::close(clients_[i].fd);
        clients_.erase(clients_.begin() + static_cast<long>(i));
        st_.clients.store(clients_.size());
        if (behind) {
            st_.clients_dropped.fetch_add(1);
            say("fanout: client dropped — fell more than " + std::to_string(client_cap_ >> 20) + " MB behind");
        }
    }

    void loop() {
        int lfd = -1;
        auto next_bind = std::chrono::steady_clock::now();
        std::vector<std::string> lines;
        std::vector<pollfd> pfds;
        char drain[256];
        while (!stop_.load()) {
            if (lfd < 0 && std::chrono::steady_clock::now() >= next_bind) {
                lfd = open_listener();
                if (lfd < 0) next_bind = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            }
            pfds.clear();
            pfds.push_back({wake_[0], POLLIN, 0});
            pfds.push_back({lfd, static_cast<short>(lfd >= 0 ? POLLIN : 0), 0});
            for (auto& c : clients_)
                pfds.push_back({c.fd, static_cast<short>(POLLIN | (c.buf.pending() ? POLLOUT : 0)), 0});
            ::poll(pfds.data(), pfds.size(), 200);
            if (pfds[0].revents & POLLIN) while (::read(wake_[0], drain, sizeof drain) > 0) {}

            // clients: hang-ups / input (ignored) — walk backwards so erase keeps indexes valid
            for (size_t i = clients_.size(); i-- > 0;) {
                const short re = pfds[2 + i].revents;
                if (re & (POLLHUP | POLLERR | POLLNVAL)) { drop_client(i, false); continue; }
                if (re & POLLIN) {
                    ssize_t r = ::recv(clients_[i].fd, drain, sizeof drain, 0);
                    if (r == 0 || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) drop_client(i, false);
                }
            }
            // new clients (hello carries the last line number issued)
            if (lfd >= 0 && (pfds[1].revents & POLLIN)) {
                while (true) {
                    int fd = ::accept4(lfd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
                    if (fd < 0) break;
                    int one = 1;
                    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
                    Client c{fd, ClientBuf(client_cap_)};
                    c.buf.append(make_line(next_n_.load(), "\"k\":\"hello\",\"src\":\"" + source_ + "\""));
                    clients_.push_back(std::move(c));
                    st_.clients.store(clients_.size());
                    st_.clients_total.fetch_add(1);
                }
            }
            // lines → every client's buffer
            lines.clear();
            q_.take(lines);
            if (!lines.empty()) {
                for (size_t i = clients_.size(); i-- > 0;)
                    for (const auto& ln : lines)
                        if (!clients_[i].buf.append(ln)) { drop_client(i, true); break; }
            }
            // write what each socket takes now
            for (size_t i = clients_.size(); i-- > 0;) {
                auto& c = clients_[i];
                while (c.buf.pending()) {
                    ssize_t w = ::send(c.fd, c.buf.data(), c.buf.pending(), MSG_NOSIGNAL);
                    if (w > 0) { c.buf.consume(static_cast<size_t>(w)); continue; }
                    if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
                    drop_client(i, false);
                    break;
                }
            }
        }
        for (auto& c : clients_) ::close(c.fd);
        clients_.clear();
        st_.clients.store(0);
        if (lfd >= 0) ::close(lfd);
    }

    std::string source_;
    int port_;
    LogFn log_;
    LineQueue q_;
    size_t client_cap_;
    Stats st_;
    std::atomic<uint64_t> next_n_{0};
    std::atomic<int> bound_{0};
    std::atomic<bool> stop_{false};
    int wake_[2] = {-1, -1};
    std::thread th_;
    std::vector<Client> clients_;          // fan-out thread only
};

// Port from an environment variable: unset → default, "0"/"off" → disabled.
inline int port_from_env(const char* var, int dflt) {
    const char* v = std::getenv(var);
    if (!v || !*v) return dflt;
    if (std::strcmp(v, "off") == 0) return 0;
    int p = std::atoi(v);
    return (p >= 0 && p < 65536) ? p : dflt;
}

}  // namespace fanout
