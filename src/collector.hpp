#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <boost/asio.hpp>

#include "audit.hpp"
#include "client.hpp"
#include "config.hpp"
#include "db.hpp"
#include "validator.hpp"
#include "wal.hpp"

namespace asio = boost::asio;

class Collector {
public:
    // Flush thresholds — tuned for sub-100ms tick-to-PG latency.
    static constexpr int    FLUSH_EVERY_N         = 5;
    static constexpr double FLUSH_EVERY_SEC       = 0.1;
    static constexpr int    BBO_FLUSH_EVERY_N     = 5;
    static constexpr double BBO_FLUSH_EVERY_SEC   = 0.1;
    static constexpr int    DEPTH_FLUSH_EVERY_N   = 10;
    static constexpr double DEPTH_FLUSH_EVERY_SEC = 0.1;
    static constexpr double METRICS_FLUSH_SEC     = 60.0;

    // Bounded hand-off queue to the DB writer thread.  When full, batches
    // are dropped (counted + alerted) rather than blocking the WS event
    // loop — heartbeats and reads must never stall on PostgreSQL.
    static constexpr size_t WRITER_QUEUE_MAX = 8192;   // was 512: at 512 the paper fleet's position flush starved the writer and ticks were DROPPED (2,144 batches 09-29, 888 on 09-30 → 30–137 s feed gaps in RTH)

    explicit Collector(const Config& cfg);
    ~Collector();

    void run();
    void stop();

private:
    // A swapped-out stream batch handed to the writer thread.
    struct BatchJob {
        enum class Kind { Tick, Bbo, Depth };
        Kind                kind;
        std::vector<TickRow>  ticks;
        std::vector<BBORow>   bbo;
        std::vector<DepthRow> depth;

        static BatchJob make_ticks(std::vector<TickRow> v) {
            BatchJob j; j.kind = Kind::Tick;  j.ticks = std::move(v); return j;
        }
        static BatchJob make_bbo(std::vector<BBORow> v) {
            BatchJob j; j.kind = Kind::Bbo;   j.bbo   = std::move(v); return j;
        }
        static BatchJob make_depth(std::vector<DepthRow> v) {
            BatchJob j; j.kind = Kind::Depth; j.depth = std::move(v); return j;
        }
    };

    // Producer side (io_context thread — read-only on the DB)
    void on_tick(TickRow row);
    void on_bbo(BBORow row);
    void on_depth(DepthRow row);
    bool enqueue_job(BatchJob job, bool enforce_cap = true);
    void enqueue_remaining();  // shutdown: push leftover buffers (uncapped)

    // Writer thread — owns all DB writes and the WALs
    void writer_loop();
    int  write_tick_batch(std::vector<TickRow> batch);
    int  write_bbo_batch(std::vector<BBORow> batch);
    int  write_depth_batch(std::vector<DepthRow> batch);
    void flush_sentinel();
    void flush_metrics();
    void ensure_db_connected();
    void stop_writer();

    void status_log();
    asio::awaitable<void> status_log_coro();

    Config                         cfg_;
    std::unique_ptr<TickDB>        db_;         // io thread — reads only
    std::unique_ptr<TickDB>        db_writer_;  // writer thread — all writes
    std::unique_ptr<AuditLog>      audit_;      // buffered; flushed by writer thread
    std::unique_ptr<Wal<TickRow>>  wal_;
    std::unique_ptr<Wal<BBORow>>   bbo_wal_;
    std::unique_ptr<Wal<DepthRow>> depth_wal_;
    std::unique_ptr<DataSentinel>  sentinel_;
    std::vector<SentinelAlertRow>  sentinel_pending_;  // writer thread — alerts held across a dead DB connection
    asio::io_context               ioc_;
    std::unique_ptr<RithmicClient> client_;

    int64_t session_id_ = 0;  // DB session row id

    std::mutex           buf_mu_;
    std::vector<TickRow> buf_;
    std::chrono::steady_clock::time_point last_flush_;
    std::chrono::steady_clock::time_point last_audit_flush_;
    std::chrono::steady_clock::time_point last_metrics_flush_;

    std::mutex            bbo_mu_;
    std::vector<BBORow>   bbo_buf_;
    std::chrono::steady_clock::time_point last_bbo_flush_;

    std::mutex              depth_mu_;
    std::vector<DepthRow>   depth_buf_;
    std::chrono::steady_clock::time_point last_depth_flush_;

    // Writer thread hand-off
    std::thread             writer_thread_;
    std::mutex              queue_mu_;
    std::condition_variable queue_cv_;
    std::deque<BatchJob>    queue_;
    bool                    writer_stop_ = false;

    // Writer-thread-only WAL cap alert latches (reset after commit)
    bool tick_wal_cap_alerted_  = false;
    bool bbo_wal_cap_alerted_   = false;
    bool depth_wal_cap_alerted_ = false;

    std::atomic<int64_t> session_total_{0};
    std::atomic<int64_t> bbo_total_{0};
    std::atomic<int64_t> depth_total_{0};
    std::atomic<int64_t> rejected_total_{0};
    std::atomic<int64_t> queue_dropped_{0};      // batches dropped (queue full)
    std::atomic<int64_t> bbo_dropped_ts_{0};     // rows rejected: ts_micros <= 0
    std::atomic<int64_t> depth_dropped_ts_{0};
    std::atomic<bool>    running_{true};
};
