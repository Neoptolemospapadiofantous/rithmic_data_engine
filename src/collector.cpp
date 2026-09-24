#include "collector.hpp"
#include "log.hpp"
#include "validator.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <sstream>
#include <stdexcept>

// ── WAL line codecs ──────────────────────────────────────────────────
//
// One CSV line per row, '\n' terminated.  The tick format is unchanged
// from the original wal.hpp (existing ticks.wal files still replay).
// Empty fields denote absent values (one-sided BBO updates, missing
// depth prev-price).  Symbol/exchange/order-id are assumed comma-free.

static void append_f6(std::string& s, double v) {
    // Fixed precision — avoids locale-dependent decimal separator
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6f", v);
    s += buf;
}

static void append_opt_f6(std::string& s, const std::optional<double>& v) {
    if (v) append_f6(s, *v);
}

static void append_opt_i(std::string& s, bool present, int32_t v) {
    if (present) s += std::to_string(v);
}

static std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    std::istringstream ss(line);
    while (std::getline(ss, cur, ',')) out.push_back(cur);
    if (!line.empty() && line.back() == ',') out.emplace_back();  // trailing empty field
    return out;
}

static std::optional<double> parse_opt_f6(const std::string& t) {
    if (t.empty()) return std::nullopt;
    return std::stod(t);
}

static std::string tick_to_line(const TickRow& r) {
    std::string s = std::to_string(r.ts_micros);
    s += ','; append_f6(s, r.price);
    s += ','; s += std::to_string(r.size);
    s += ','; s += (r.is_buy ? '1' : '0');
    s += ','; s += r.symbol;
    s += ','; s += r.exchange;
    return s;
}

static bool tick_from_line(const std::string& line, TickRow& r) {
    auto f = split_csv(line);
    if (f.size() < 6) return false;
    try {
        r.ts_micros = std::stoll(f[0]);
        r.price     = std::stod(f[1]);
        r.size      = std::stoll(f[2]);
        r.is_buy    = (f[3] == "1");
        r.symbol    = f[4];
        r.exchange  = f[5];
    } catch (...) { return false; }
    return !r.symbol.empty() && !r.exchange.empty();
}

static std::string bbo_to_line(const BBORow& r) {
    std::string s = std::to_string(r.ts_micros);
    s += ','; append_opt_f6(s, r.bid_price);
    s += ','; append_opt_i(s, r.bid_price.has_value(), r.bid_size);
    s += ','; append_opt_i(s, r.bid_price.has_value(), r.bid_orders);
    s += ','; append_opt_f6(s, r.ask_price);
    s += ','; append_opt_i(s, r.ask_price.has_value(), r.ask_size);
    s += ','; append_opt_i(s, r.ask_price.has_value(), r.ask_orders);
    s += ','; s += r.symbol;
    s += ','; s += r.exchange;
    return s;
}

static bool bbo_from_line(const std::string& line, BBORow& r) {
    auto f = split_csv(line);
    if (f.size() < 9) return false;
    try {
        r.ts_micros  = std::stoll(f[0]);
        r.bid_price  = parse_opt_f6(f[1]);
        r.bid_size   = f[2].empty() ? 0 : std::stoi(f[2]);
        r.bid_orders = f[3].empty() ? 0 : std::stoi(f[3]);
        r.ask_price  = parse_opt_f6(f[4]);
        r.ask_size   = f[5].empty() ? 0 : std::stoi(f[5]);
        r.ask_orders = f[6].empty() ? 0 : std::stoi(f[6]);
        r.symbol     = f[7];
        r.exchange   = f[8];
    } catch (...) { return false; }
    return !r.symbol.empty() && !r.exchange.empty();
}

static std::string depth_to_line(const DepthRow& r) {
    std::string s = std::to_string(r.ts_micros);
    s += ','; s += std::to_string(r.source_ns);
    s += ','; s += std::to_string(r.sequence_number);
    s += ','; s += std::to_string(static_cast<int>(r.update_type));
    s += ','; s += std::to_string(static_cast<int>(r.transaction_type));
    s += ','; append_f6(s, r.depth_price);
    s += ','; append_opt_f6(s, r.prev_depth_price);
    s += ','; s += std::to_string(r.depth_size);
    s += ','; s += r.exchange_order_id;
    s += ','; s += r.symbol;
    s += ','; s += r.exchange;
    return s;
}

static bool depth_from_line(const std::string& line, DepthRow& r) {
    auto f = split_csv(line);
    if (f.size() < 12) return false;
    try {
        r.ts_micros         = std::stoll(f[0]);
        r.source_ns         = std::stoll(f[1]);
        r.sequence_number   = std::stoll(f[2]);
        r.update_type       = static_cast<int8_t>(std::stoi(f[3]));
        r.transaction_type  = static_cast<int8_t>(std::stoi(f[4]));
        r.depth_price       = std::stod(f[5]);
        r.prev_depth_price  = parse_opt_f6(f[6]);
        r.depth_size        = std::stoi(f[7]);
        r.exchange_order_id = f[8];
        r.symbol            = f[9];
        r.exchange          = f[10];
    } catch (...) { return false; }
    return !r.symbol.empty() && !r.exchange.empty();
}

// ── Collector ──────────────────────────────────────────────────────

Collector::Collector(const Config& cfg) : cfg_(cfg) {
    db_        = std::make_unique<TickDB>(cfg_.pg_connstr());
    db_writer_ = std::make_unique<TickDB>(cfg_.pg_connstr());
    audit_     = std::make_unique<AuditLog>(db_writer_->conn());
    wal_       = std::make_unique<Wal<TickRow>>(cfg_.wal_path(),
                                                tick_to_line, tick_from_line);
    bbo_wal_   = std::make_unique<Wal<BBORow>>(cfg_.wal_path() + ".bbo",
                                               bbo_to_line, bbo_from_line);
    depth_wal_ = std::make_unique<Wal<DepthRow>>(cfg_.wal_path() + ".depth",
                                                 depth_to_line, depth_from_line);
    sentinel_  = std::make_unique<DataSentinel>();

    auto count = db_->row_count();
    LOG("PostgreSQL connected (%lld existing ticks)", (long long)count);

    // Start a new DB session
    session_id_ = db_->start_session("collect");
    LOG("Session started (id=%lld)", (long long)session_id_);

    audit_->info("collector.start",
                 "existing_ticks=" + std::to_string(count) +
                 " session_id=" + std::to_string(session_id_));

    // Replay rows that were written to a WAL but not flushed (crash
    // recovery) — one WAL per stream, replayed once from disk at open.
    auto recover = [this](auto& wal, auto write_fn, const char* label) {
        if (!wal->dirty()) return;
        LOG("WAL replay: %zu %s rows recovered from crash",
            wal->pending().size(), label);
        try {
            int n = write_fn(wal->pending());
            wal->commit();
            LOG("WAL replay: %d %s rows written to DB", n, label);
            audit_->info("wal.replay", std::string("stream=") + label +
                         " recovered=" + std::to_string(n));
        } catch (std::exception& e) {
            LOG("WAL replay DB write failed (%s): %s (rows kept in WAL)",
                label, e.what());
        }
    };
    recover(wal_,       [this](const std::vector<TickRow>& v)  { return db_writer_->write(v); },       "tick");
    recover(bbo_wal_,   [this](const std::vector<BBORow>& v)   { return db_writer_->write_bbo(v); },   "bbo");
    recover(depth_wal_, [this](const std::vector<DepthRow>& v) { return db_writer_->write_depth(v); }, "depth");

    client_ = std::make_unique<RithmicClient>(ioc_, cfg_);
    client_->set_on_tick([this](TickRow r)  { on_tick(std::move(r));  });
    client_->set_on_bbo([this](BBORow r)    { on_bbo(std::move(r));   });
    client_->set_on_depth([this](DepthRow r){ on_depth(std::move(r)); });

    last_flush_         = std::chrono::steady_clock::now();
    last_audit_flush_   = std::chrono::steady_clock::now();
    last_metrics_flush_ = std::chrono::steady_clock::now();
    last_bbo_flush_     = std::chrono::steady_clock::now();
    last_depth_flush_   = std::chrono::steady_clock::now();
}

Collector::~Collector() {
    stop();
    stop_writer();
}

// ── on_tick ────────────────────────────────────────────────────────

void Collector::on_tick(TickRow row) {
    // Validate before buffering — reject garbage data early
    std::string reason;
    if (!TickValidator::valid(row, &reason)) {
        LOG("  Tick rejected [%s/%s price=%.2f size=%lld]: %s",
            row.symbol.c_str(), row.exchange.c_str(),
            row.price, (long long)row.size, reason.c_str());
        ++rejected_total_;
        return;
    }

    // Economic plausibility checks (stateful — price jumps, gaps, volume spikes)
    sentinel_->observe_tick(row.price, row.size, row.ts_micros);

    std::vector<TickRow> batch;
    {
        std::lock_guard lock(buf_mu_);
        buf_.push_back(std::move(row));
        ++session_total_;
        double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - last_flush_).count();
        if (static_cast<int>(buf_.size()) >= FLUSH_EVERY_N ||
            elapsed >= FLUSH_EVERY_SEC) {
            batch.swap(buf_);
            last_flush_ = std::chrono::steady_clock::now();
        }
    }
    if (!batch.empty() && !enqueue_job(BatchJob::make_ticks(std::move(batch)))) {
        int64_t d = ++queue_dropped_;
        if (d == 1 || d % 100 == 0) {
            LOG("  Writer queue full — dropped tick batch (total=%lld)", (long long)d);
            audit_->error("writer.queue_full",
                          "stream=tick dropped_batches=" + std::to_string(d));
        }
    }
}

// ── on_bbo ─────────────────────────────────────────────────────────

void Collector::on_bbo(BBORow row) {
    // Missing ssboe would land as ts_micros=0 → 1970-01-01 in the DB
    if (row.ts_micros <= 0) {
        int64_t d = ++bbo_dropped_ts_;
        if (d == 1 || d % 100 == 0)
            LOG("  BBO rows dropped (missing timestamp): %lld", (long long)d);
        return;
    }

    // BBO sentinel checks (bid-ask inversion, wide spread) — both sides needed
    if (row.bid_price && row.ask_price)
        sentinel_->observe_bbo(*row.bid_price, *row.ask_price);

    std::vector<BBORow> batch;
    {
        std::lock_guard lock(bbo_mu_);
        bbo_buf_.push_back(std::move(row));
        ++bbo_total_;
        double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - last_bbo_flush_).count();
        if (static_cast<int>(bbo_buf_.size()) >= BBO_FLUSH_EVERY_N ||
            elapsed >= BBO_FLUSH_EVERY_SEC) {
            batch.swap(bbo_buf_);
            last_bbo_flush_ = std::chrono::steady_clock::now();
        }
    }
    if (!batch.empty() && !enqueue_job(BatchJob::make_bbo(std::move(batch)))) {
        int64_t d = ++queue_dropped_;
        if (d == 1 || d % 100 == 0) {
            LOG("  Writer queue full — dropped BBO batch (total=%lld)", (long long)d);
            audit_->error("writer.queue_full",
                          "stream=bbo dropped_batches=" + std::to_string(d));
        }
    }
}

// ── on_depth ───────────────────────────────────────────────────────

void Collector::on_depth(DepthRow row) {
    // Missing ssboe would land as ts_micros=0 → 1970-01-01 in the DB
    if (row.ts_micros <= 0) {
        int64_t d = ++depth_dropped_ts_;
        if (d == 1 || d % 100 == 0)
            LOG("  Depth rows dropped (missing timestamp): %lld", (long long)d);
        return;
    }

    std::vector<DepthRow> batch;
    {
        std::lock_guard lock(depth_mu_);
        depth_buf_.push_back(std::move(row));
        ++depth_total_;
        double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - last_depth_flush_).count();
        if (static_cast<int>(depth_buf_.size()) >= DEPTH_FLUSH_EVERY_N ||
            elapsed >= DEPTH_FLUSH_EVERY_SEC) {
            batch.swap(depth_buf_);
            last_depth_flush_ = std::chrono::steady_clock::now();
        }
    }
    if (!batch.empty() && !enqueue_job(BatchJob::make_depth(std::move(batch)))) {
        int64_t d = ++queue_dropped_;
        if (d == 1 || d % 100 == 0) {
            LOG("  Writer queue full — dropped depth batch (total=%lld)", (long long)d);
            audit_->error("writer.queue_full",
                          "stream=depth dropped_batches=" + std::to_string(d));
        }
    }
}

// ── writer queue ─────────────────────────────────────────────────────

bool Collector::enqueue_job(BatchJob job, bool enforce_cap) {
    {
        std::lock_guard lock(queue_mu_);
        if (enforce_cap && queue_.size() >= WRITER_QUEUE_MAX) return false;
        queue_.push_back(std::move(job));
    }
    queue_cv_.notify_one();
    return true;
}

void Collector::enqueue_remaining() {
    std::vector<TickRow>  ticks;
    std::vector<BBORow>   bbo;
    std::vector<DepthRow> depth;
    {
        std::lock_guard lock(buf_mu_);
        ticks.swap(buf_);
    }
    {
        std::lock_guard lock(bbo_mu_);
        bbo.swap(bbo_buf_);
    }
    {
        std::lock_guard lock(depth_mu_);
        depth.swap(depth_buf_);
    }
    if (!ticks.empty()) enqueue_job(BatchJob::make_ticks(std::move(ticks)), false);
    if (!bbo.empty())   enqueue_job(BatchJob::make_bbo(std::move(bbo)), false);
    if (!depth.empty()) enqueue_job(BatchJob::make_depth(std::move(depth)), false);
}

// ── writer thread ────────────────────────────────────────────────────

void Collector::writer_loop() {
    while (true) {
        BatchJob job;
        {
            std::unique_lock lock(queue_mu_);
            queue_cv_.wait(lock, [&] { return writer_stop_ || !queue_.empty(); });
            if (queue_.empty()) break;  // stop requested and queue drained
            job = std::move(queue_.front());
            queue_.pop_front();
        }

        try {
            switch (job.kind) {
                case BatchJob::Kind::Tick:  write_tick_batch(std::move(job.ticks));  break;
                case BatchJob::Kind::Bbo:   write_bbo_batch(std::move(job.bbo));     break;
                case BatchJob::Kind::Depth: write_depth_batch(std::move(job.depth)); break;
            }
        } catch (std::exception& e) {
            LOG("  Writer job failed: %s", e.what());
            audit_->error("writer.job_error", e.what());
        }

        // Periodic flushes piggyback on writer activity (previously in flush())
        auto now = std::chrono::steady_clock::now();

        double ae = std::chrono::duration<double>(now - last_audit_flush_).count();
        if (ae >= 60.0) {
            audit_->flush();
            last_audit_flush_ = now;
        }

        double me = std::chrono::duration<double>(now - last_metrics_flush_).count();
        if (me >= METRICS_FLUSH_SEC) {
            flush_sentinel();
            flush_metrics();
            last_metrics_flush_ = now;
        }
    }
}

void Collector::stop_writer() {
    if (!writer_thread_.joinable()) return;
    {
        std::lock_guard lock(queue_mu_);
        writer_stop_ = true;
    }
    queue_cv_.notify_one();
    writer_thread_.join();
}

// ── ensure_db_connected ────────────────────────────────────────────

void Collector::ensure_db_connected() {
    if (!db_writer_->is_connected()) {
        LOG("  DB disconnected — attempting reconnect...");
        db_writer_->reconnect();
    }
}

// ── write_*_batch — writer-thread drains (WAL → DB → commit) ──────
//
// Semantics preserved from the old flush(): the batch is appended to the
// WAL (fdatasync) BEFORE the DB write, and the DB write drains the WAL's
// full pending set so batches from earlier failed flushes are retried.
// On WAL failure the batch is re-queued into the stream buffer instead of
// being dropped (previously it lived only in the swapped-out local).

int Collector::write_tick_batch(std::vector<TickRow> batch) {
    try {
        wal_->write_batch(batch);
    } catch (std::exception& e) {
        LOG("  WAL write failed: %s — %zu ticks re-queued", e.what(), batch.size());
        audit_->error("wal.write_error", std::string("stream=tick ") + e.what());
        std::lock_guard lock(buf_mu_);
        buf_.insert(buf_.begin(),
                    std::make_move_iterator(batch.begin()),
                    std::make_move_iterator(batch.end()));
        return 0;
    }

    if (wal_->over_cap() && !tick_wal_cap_alerted_) {
        tick_wal_cap_alerted_ = true;
        LOG("  WAL over cap (%lld bytes) — DB unreachable?",
            (long long)wal_->size_bytes());
        audit_->error("wal.oversize", "path=" + wal_->path() +
                      " bytes=" + std::to_string(wal_->size_bytes()));
    }

    try {
        ensure_db_connected();
        int n = db_writer_->write(wal_->pending());
        wal_->commit();
        tick_wal_cap_alerted_ = false;

        LOG("  Wrote %d ticks (session=%lld rejected=%lld)",
            n, (long long)session_total_.load(),
               (long long)rejected_total_.load());
        audit_->info("ticks.written",
                     "count=" + std::to_string(n) +
                     " batch=" + std::to_string(batch.size()));
        return n;

    } catch (std::exception& e) {
        LOG("  DB write failed: %s — %zu ticks held in WAL",
            e.what(), wal_->pending().size());
        audit_->error("ticks.write_error", e.what());
        return 0;
    }
}

int Collector::write_bbo_batch(std::vector<BBORow> batch) {
    try {
        bbo_wal_->write_batch(batch);
    } catch (std::exception& e) {
        LOG("  BBO WAL write failed: %s — %zu rows re-queued", e.what(), batch.size());
        audit_->error("wal.write_error", std::string("stream=bbo ") + e.what());
        std::lock_guard lock(bbo_mu_);
        bbo_buf_.insert(bbo_buf_.begin(),
                        std::make_move_iterator(batch.begin()),
                        std::make_move_iterator(batch.end()));
        return 0;
    }

    if (bbo_wal_->over_cap() && !bbo_wal_cap_alerted_) {
        bbo_wal_cap_alerted_ = true;
        LOG("  BBO WAL over cap (%lld bytes) — DB unreachable?",
            (long long)bbo_wal_->size_bytes());
        audit_->error("wal.oversize", "path=" + bbo_wal_->path() +
                      " bytes=" + std::to_string(bbo_wal_->size_bytes()));
    }

    try {
        ensure_db_connected();
        int n = db_writer_->write_bbo(bbo_wal_->pending());
        bbo_wal_->commit();
        bbo_wal_cap_alerted_ = false;
        LOG("  Wrote %d BBO rows", n);
        return n;
    } catch (std::exception& e) {
        LOG("  BBO DB write failed: %s — %zu rows held in WAL",
            e.what(), bbo_wal_->pending().size());
        audit_->error("bbo.write_error", e.what());
        return 0;
    }
}

int Collector::write_depth_batch(std::vector<DepthRow> batch) {
    try {
        depth_wal_->write_batch(batch);
    } catch (std::exception& e) {
        LOG("  Depth WAL write failed: %s — %zu rows re-queued", e.what(), batch.size());
        audit_->error("wal.write_error", std::string("stream=depth ") + e.what());
        std::lock_guard lock(depth_mu_);
        depth_buf_.insert(depth_buf_.begin(),
                          std::make_move_iterator(batch.begin()),
                          std::make_move_iterator(batch.end()));
        return 0;
    }

    if (depth_wal_->over_cap() && !depth_wal_cap_alerted_) {
        depth_wal_cap_alerted_ = true;
        LOG("  Depth WAL over cap (%lld bytes) — DB unreachable?",
            (long long)depth_wal_->size_bytes());
        audit_->error("wal.oversize", "path=" + depth_wal_->path() +
                      " bytes=" + std::to_string(depth_wal_->size_bytes()));
    }

    try {
        ensure_db_connected();
        int n = db_writer_->write_depth(depth_wal_->pending());
        depth_wal_->commit();
        depth_wal_cap_alerted_ = false;
        LOG("  Wrote %d depth rows", n);
        return n;
    } catch (std::exception& e) {
        LOG("  depth DB write failed: %s — %zu rows held in WAL",
            e.what(), depth_wal_->pending().size());
        audit_->error("depth.write_error", e.what());
        return 0;
    }
}

// ── flush_sentinel — drain alerts from DataSentinel to DB ─────────

void Collector::flush_sentinel() {
    auto alerts = sentinel_->drain_alerts();
    if (alerts.empty()) return;

    std::vector<SentinelAlertRow> rows;
    rows.reserve(alerts.size());
    for (auto& a : alerts) {
        rows.push_back({session_id_, a.check, a.severity, a.message, a.value});
    }

    try {
        db_writer_->write_sentinel_alerts(rows);
        LOG("  Flushed %zu sentinel alerts", alerts.size());
    } catch (std::exception& e) {
        LOG("  Sentinel alert flush failed: %s", e.what());
    }
}

// ── flush_metrics — write quality metrics snapshot ─────────────────

void Collector::flush_metrics() {
    try {
        std::vector<QualityMetric> ms;
        ms.push_back({"session_ticks",    static_cast<double>(session_total_.load()), ""});
        ms.push_back({"session_rejected", static_cast<double>(rejected_total_.load()), ""});
        ms.push_back({"session_bbo",      static_cast<double>(bbo_total_.load()), ""});
        ms.push_back({"session_depth",    static_cast<double>(depth_total_.load()), ""});
        ms.push_back({"sentinel_alerts",  static_cast<double>(sentinel_->alert_count()), ""});
        ms.push_back({"sentinel_gaps",    static_cast<double>(sentinel_->gap_count()), ""});
        ms.push_back({"writer_queue_dropped", static_cast<double>(queue_dropped_.load()), ""});
        ms.push_back({"bbo_dropped_ts",   static_cast<double>(bbo_dropped_ts_.load()), ""});
        ms.push_back({"depth_dropped_ts", static_cast<double>(depth_dropped_ts_.load()), ""});

        double reject_rate = session_total_.load() > 0
            ? static_cast<double>(rejected_total_.load()) / static_cast<double>(session_total_.load() + rejected_total_.load()) * 100.0
            : 0.0;
        ms.push_back({"rejection_rate_pct", reject_rate, ""});

        db_writer_->write_metrics(ms);
    } catch (std::exception& e) {
        LOG("  Metrics flush failed: %s", e.what());
    }
}

// ── status logging ─────────────────────────────────────────────────
// Runs on the io_context thread; db_ is used read-only here (all writes
// go through the writer thread's own connection).

asio::awaitable<void> Collector::status_log_coro() {
    auto ex = co_await asio::this_coro::executor;
    asio::steady_timer t(ex);
    while (running_) {
        t.expires_after(std::chrono::seconds(60));
        boost::system::error_code ec;
        co_await t.async_wait(asio::redirect_error(use_awaitable, ec));
        if (ec || !running_) co_return;
        try {
            auto s = db_->summary();
            LOG("  ticks=%lld  session=%lld  rejected=%lld  bbo=%lld  depth=%lld  alerts=%lld  latest=%s  price=%s",
                (long long)s.tick_count,
                (long long)session_total_.load(),
                (long long)rejected_total_.load(),
                (long long)bbo_total_.load(),
                (long long)depth_total_.load(),
                (long long)sentinel_->alert_count(),
                s.latest.c_str(),
                s.price ? std::to_string(*s.price).c_str() : "n/a");
            LOG("  frames by template: %s", client_->template_counts().c_str());
        } catch (...) {}
    }
}

void Collector::status_log() {
    asio::co_spawn(ioc_, status_log_coro(), asio::detached);
}

// ── run / stop ─────────────────────────────────────────────────────

void Collector::run() {
    auto errs = cfg_.validate();
    if (!errs.empty()) {
        for (auto& e : errs) LOG("Config error: %s", e.c_str());
        throw std::runtime_error("Invalid config — check .env");
    }

    // DB writer thread: all PostgreSQL writes happen here, off the
    // io_context thread, so a slow/blocked flush never stalls heartbeats.
    writer_thread_ = std::thread([this] { writer_loop(); });

    status_log();

    asio::co_spawn(ioc_, client_->run(), [this](std::exception_ptr ep) {
        if (ep) {
            try { std::rethrow_exception(ep); }
            catch (std::exception& e) {
                LOG("Client error: %s", e.what());
                audit_->error("connection.lost", e.what());
            }
        }
        ioc_.stop();
    });

    ioc_.run();

    // Hand any remaining buffered rows to the writer, then let it drain
    enqueue_remaining();
    stop_writer();

    // Final flushes after the writer thread has joined
    flush_sentinel();
    flush_metrics();

    // Close session in DB
    try {
        db_->end_session(session_id_,
                         session_total_.load(), bbo_total_.load(), depth_total_.load(),
                         rejected_total_.load(), sentinel_->gap_count(),
                         sentinel_->alert_count());
        LOG("Session %lld closed", (long long)session_id_);
    } catch (std::exception& e) {
        LOG("Failed to close session: %s", e.what());
    }

    audit_->info("collector.stop",
                 "session_id=" + std::to_string(session_id_) +
                 " ticks=" + std::to_string(session_total_.load()) +
                 " rejected=" + std::to_string(rejected_total_.load()));
    audit_->flush();
    LOG("Collector stopped.");
}

void Collector::stop() {
    if (running_.exchange(false)) {
        client_->stop();
        ioc_.stop();
    }
}
