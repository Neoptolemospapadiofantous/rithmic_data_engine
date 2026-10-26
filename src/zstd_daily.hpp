#pragma once
// zstd_daily.hpp — append-only raw archive: one zstd stream per directory, rolled at UTC midnight.
// Shared by crypto_collector (Bitstamp MBO) and hl_recorder (Hyperliquid books/trades).
//
#include <zstd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

// Appending to an existing day file after a restart starts a new zstd frame; concatenated
// frames decode as one stream (zstdcat). Flushed every 2 s → a crash loses ~2 s at most.
// Retention: forever — nothing here ever deletes.
class ZstdDaily {
public:
    using LogFn = std::function<void(const std::string&)>;
    // level 9 suits a few streams; many parallel streams want a lower level (level 9 costs ~10 MB each).
    explicit ZstdDaily(std::filesystem::path dir, LogFn log = {}, int level = 9)
        : dir_(std::move(dir)), log_(std::move(log)), level_(level) {}
    ~ZstdDaily() { close(); }
    ZstdDaily(const ZstdDaily&) = delete;
    ZstdDaily& operator=(const ZstdDaily&) = delete;

    void write_line(const std::string& line, int64_t at_us) {
        const std::string day = utc_date(at_us);
        if (day != day_) { close(); open(day); }
        if (!fp_) return;
        ZSTD_inBuffer in{line.data(), line.size(), 0};
        while (in.pos < in.size) drive(in, ZSTD_e_continue);
        static const char nl = '\n';
        ZSTD_inBuffer in2{&nl, 1, 0};
        drive(in2, ZSTD_e_continue);
    }
    void flush() {
        if (!fp_) return;
        ZSTD_inBuffer in{nullptr, 0, 0};
        while (drive(in, ZSTD_e_flush) != 0) {}
        std::fflush(fp_);
    }
    void close() {
        if (fp_) {
            ZSTD_inBuffer in{nullptr, 0, 0};
            while (drive(in, ZSTD_e_end) != 0) {}
            std::fclose(fp_); fp_ = nullptr;
        }
        if (cctx_) { ZSTD_freeCCtx(cctx_); cctx_ = nullptr; }
        day_.clear();
    }
    std::filesystem::path current() const { return dir_ / (day_ + ".jsonl.zst"); }

    static std::string utc_date(int64_t us) {
        time_t s = static_cast<time_t>(us / 1'000'000); struct tm t; gmtime_r(&s, &t);
        char b[16]; std::strftime(b, sizeof b, "%Y-%m-%d", &t); return b;
    }

private:
    void open(const std::string& day) {
        std::error_code ec;
        std::filesystem::create_directories(dir_, ec);
        day_ = day;
        fp_ = std::fopen(current().c_str(), "ab");
        if (!fp_) { say("raw archive: cannot open " + current().string()); day_.clear(); return; }
        cctx_ = ZSTD_createCCtx();
        ZSTD_CCtx_setParameter(cctx_, ZSTD_c_compressionLevel, level_);
        ZSTD_CCtx_setParameter(cctx_, ZSTD_c_checksumFlag, 1);
        out_.resize(ZSTD_CStreamOutSize());
        say("raw archive: writing " + current().string());
    }
    size_t drive(ZSTD_inBuffer& in, ZSTD_EndDirective mode) {
        ZSTD_outBuffer o{out_.data(), out_.size(), 0};
        size_t rem = ZSTD_compressStream2(cctx_, &o, &in, mode);
        if (ZSTD_isError(rem)) { say(std::string("zstd error: ") + ZSTD_getErrorName(rem)); return 0; }
        if (o.pos) std::fwrite(out_.data(), 1, o.pos, fp_);
        return rem;
    }
    void say(const std::string& s) const { if (log_) log_(s); }
    std::filesystem::path dir_;
    LogFn log_;
    int level_ = 9;
    std::string day_;
    std::FILE* fp_ = nullptr;
    ZSTD_CCtx* cctx_ = nullptr;
    std::vector<char> out_;
};

// Reads a ZstdDaily file back line by line. A file still being written ends in an unfinished
// frame; that is a normal end of data here (everything flushed so far is returned), not an error.
class ZstdLines {
public:
    explicit ZstdLines(const std::filesystem::path& p) : fp_(std::fopen(p.c_str(), "rb")) {
        if (fp_) { ds_ = ZSTD_createDStream(); ZSTD_initDStream(ds_); }
        in_.resize(ZSTD_DStreamInSize());
        out_.resize(ZSTD_DStreamOutSize());
    }
    ~ZstdLines() { if (ds_) ZSTD_freeDStream(ds_); if (fp_) std::fclose(fp_); }
    ZstdLines(const ZstdLines&) = delete;
    ZstdLines& operator=(const ZstdLines&) = delete;
    bool ok() const { return fp_ != nullptr; }
    bool corrupt() const { return corrupt_; }       // at least one damaged frame was skipped
    int  skipped_frames() const { return skipped_; }

    bool next(std::string& line) {
        for (;;) {
            const size_t nl = pending_.find('\n', scan_);
            if (nl != std::string::npos) {
                line.assign(pending_, 0, nl);
                pending_.erase(0, nl + 1);
                scan_ = 0;
                return true;
            }
            scan_ = pending_.size();
            if (!fill()) return false;           // a trailing partial line is dropped
        }
    }

private:
    bool fill() {
        if (!fp_ || done_) return false;
        for (;;) {
            if (inb_.pos == inb_.size) {
                inb_ = {in_.data(), std::fread(in_.data(), 1, in_.size(), fp_), 0};
                if (inb_.size == 0) { done_ = true; return false; }
            }
            ZSTD_outBuffer ob{out_.data(), out_.size(), 0};
            const size_t r = ZSTD_decompressStream(ds_, &ob, &inb_);
            if (ZSTD_isError(r)) {
                // A writer killed mid-frame (recorder restart, OOM) leaves a truncated frame and the next run appends
                // a new one after it. Until 2026-10-26 this stopped the whole file there — a quarter of Lighter BNB
                // 2026-10-25 was silently never replayed. Keep every complete line already decoded, drop the cut
                // line, and resume at the next frame's magic number.
                corrupt_ = true;
                ++skipped_;
                const size_t keep = pending_.rfind('\n', pending_.size());
                pending_.erase(keep == std::string::npos ? 0 : keep + 1);
                scan_ = 0;
                if (!resync()) { done_ = true; return !pending_.empty(); }
                continue;
            }
            if (ob.pos) { pending_.append(out_.data(), ob.pos); return true; }
        }
    }
    // Advance the input to the next zstd frame header (28 B5 2F FD), reading more of the file as needed, and reset the
    // decoder. The search starts one byte after the current position so the damaged frame's own header is skipped.
    bool resync() {
        static constexpr unsigned char MAGIC[4] = {0x28, 0xB5, 0x2F, 0xFD};
        size_t from = inb_.pos + 1;
        for (;;) {
            const auto* b = static_cast<const unsigned char*>(inb_.src);
            for (size_t i = from; i + 4 <= inb_.size; ++i)
                if (b[i] == MAGIC[0] && b[i + 1] == MAGIC[1] && b[i + 2] == MAGIC[2] && b[i + 3] == MAGIC[3]) {
                    inb_.pos = i;
                    ZSTD_DCtx_reset(ds_, ZSTD_reset_session_only);
                    return true;
                }
            // keep the last 3 bytes (a header may straddle two reads), then read on
            const size_t tail = inb_.size >= 3 ? 3 : inb_.size;
            std::memmove(in_.data(), static_cast<const char*>(inb_.src) + inb_.size - tail, tail);
            const size_t got = std::fread(in_.data() + tail, 1, in_.size() - tail, fp_);
            if (got == 0) return false;
            inb_ = {in_.data(), tail + got, 0};
            from = 0;
        }
    }
    std::FILE* fp_ = nullptr;
    ZSTD_DStream* ds_ = nullptr;
    std::vector<char> in_, out_;
    ZSTD_inBuffer inb_{nullptr, 0, 0};
    std::string pending_;
    size_t scan_ = 0;
    bool done_ = false, corrupt_ = false;
    int skipped_ = 0;
};
