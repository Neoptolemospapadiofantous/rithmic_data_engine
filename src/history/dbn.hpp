#pragma once
/*  ═══════════════════════════════════════════════════════════════════════════
    dbn.hpp — header-only reader for Databento Binary Encoding (DBN) files

    The owned historical data (/home/theone/Desktop/bot/data/raw, Databento
    GLBX.MDP3, NQ.c.0 / ES.c.0) is stored as zstd-compressed DBN v1. This reader
    decodes it in a streaming fashion (a 8 GB monthly MBO file never sits in
    memory): metadata header → fixed-layout records walked by their own length
    byte, so an unknown record type is skipped rather than misread.

    Supported: DBN versions 1, 2 and 3 (metadata layout differs between v1 and
    v2+, the record layouts used here are identical in all three), plain or
    zstd-compressed input. Record types decoded: MBO (0xA0), trades / MBP-0
    (0x00), OHLCV 1s/1m/1h/1d/eod (0x20–0x24). Everything else is exposed as a
    raw RecordHeader the caller can skip.

    Prices are fixed-point int64 × 1e-9 (kUndefPrice = INT64_MAX = no price).
    Timestamps are UNIX nanoseconds (UTC).

    Spec: https://databento.com/docs/standards-and-conventions/databento-binary-encoding
    ═══════════════════════════════════════════════════════════════════════════ */
#include <zstd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace dbn {

constexpr int64_t  kUndefPrice  = INT64_MAX;
constexpr double   kPriceScale  = 1e-9;
inline double px(int64_t p) { return p == kUndefPrice ? 0.0 : (double)p * kPriceScale; }

// Record types (rtype) used here.
enum RType : uint8_t {
    kMbp0 = 0x00,     // trades schema
    kOhlcv1s = 0x20, kOhlcv1m = 0x21, kOhlcv1h = 0x22, kOhlcv1d = 0x23, kOhlcvEod = 0x24,
    kMbo  = 0xA0,
};

// MBO / trade flags.
enum Flags : uint8_t {
    kFlagLast = 0x80, kFlagTob = 0x40, kFlagSnapshot = 0x20, kFlagMbp = 0x10,
    kFlagBadTsRecv = 0x08, kFlagMaybeBadBook = 0x04,
};

#pragma pack(push, 1)
struct RecordHeader {
    uint8_t  length;          // record length in 4-byte words (header included)
    uint8_t  rtype;
    uint16_t publisher_id;
    uint32_t instrument_id;
    uint64_t ts_event;        // ns since epoch
};
struct MboMsg {
    RecordHeader hd;
    uint64_t order_id;
    int64_t  price;
    uint32_t size;
    uint8_t  flags;
    uint8_t  channel_id;
    char     action;          // A add, C cancel, M modify, R clear, T trade, F fill, N none
    char     side;            // A ask, B bid, N none (for T: the aggressor's side)
    uint64_t ts_recv;
    int32_t  ts_in_delta;
    uint32_t sequence;
};
struct TradeMsg {             // MBP-0 / trades schema
    RecordHeader hd;
    int64_t  price;
    uint32_t size;
    char     action;
    char     side;
    uint8_t  flags;
    uint8_t  depth;
    uint64_t ts_recv;
    int32_t  ts_in_delta;
    uint32_t sequence;
};
struct OhlcvMsg {
    RecordHeader hd;
    int64_t  open, high, low, close;
    uint64_t volume;
};
#pragma pack(pop)
static_assert(sizeof(RecordHeader) == 16, "DBN record header is 16 bytes");
static_assert(sizeof(MboMsg)   == 56, "DBN MBO record is 56 bytes");
static_assert(sizeof(TradeMsg) == 48, "DBN trade record is 48 bytes");
static_assert(sizeof(OhlcvMsg) == 56, "DBN OHLCV record is 56 bytes");

struct MappingInterval { uint32_t start_date = 0, end_date = 0; std::string symbol; };  // YYYYMMDD, end exclusive
struct SymbolMapping   { std::string raw_symbol; std::vector<MappingInterval> intervals; };

struct Metadata {
    uint8_t     version = 0;
    std::string dataset;
    uint16_t    schema = 0;
    uint64_t    start = 0, end = 0, limit = 0;
    uint8_t     stype_in = 0, stype_out = 0;
    bool        ts_out = false;
    uint16_t    symbol_cstr_len = 22;
    std::vector<std::string>   symbols, partial, not_found;
    std::vector<SymbolMapping> mappings;
};

// ── Byte source: plain file or zstd stream, read sequentially ───────────────
class ByteSource {
public:
    virtual ~ByteSource() = default;
    // Fill exactly n bytes into dst; returns false at clean EOF before any byte (throws on a short tail).
    virtual bool read(void* dst, size_t n) = 0;
};

// Streams a (possibly zstd-compressed) buffer provider. The provider is a FILE* or memory.
class ZstdReader : public ByteSource {
public:
    // Takes ownership of `f` (closed in the destructor). `compressed` false = plain passthrough.
    ZstdReader(FILE* f, bool compressed) : f_(f), compressed_(compressed) {
        in_.resize(ZSTD_DStreamInSize());
        out_.resize(ZSTD_DStreamOutSize() * 4);
        if (compressed_) {
            ds_ = ZSTD_createDStream();
            if (!ds_) throw std::runtime_error("ZSTD_createDStream failed");
            ZSTD_initDStream(ds_);
        }
        inb_ = {in_.data(), 0, 0};
    }
    // Memory source (tests): copies the buffer.
    ZstdReader(const std::vector<uint8_t>& mem, bool compressed) : ZstdReader(nullptr, compressed) {
        mem_ = mem;
    }
    ~ZstdReader() override {
        if (ds_) ZSTD_freeDStream(ds_);
        if (f_) std::fclose(f_);
    }
    ZstdReader(const ZstdReader&) = delete;
    ZstdReader& operator=(const ZstdReader&) = delete;

    bool read(void* dst, size_t n) override {
        uint8_t* d = static_cast<uint8_t*>(dst);
        size_t got = 0;
        while (got < n) {
            if (pos_ == len_ && !refill()) {
                if (got == 0) return false;
                throw std::runtime_error("DBN: truncated input (short record at end of stream)");
            }
            const size_t k = std::min(n - got, len_ - pos_);
            std::memcpy(d + got, out_.data() + pos_, k);
            pos_ += k; got += k;
        }
        return true;
    }
    uint64_t compressed_bytes_read() const { return raw_read_; }

private:
    size_t raw(void* dst, size_t cap) {   // next chunk of the underlying bytes
        if (f_) { size_t k = std::fread(dst, 1, cap, f_); raw_read_ += k; return k; }
        const size_t k = std::min(cap, mem_.size() - mem_pos_);
        std::memcpy(dst, mem_.data() + mem_pos_, k); mem_pos_ += k; raw_read_ += k;
        return k;
    }
    bool refill() {
        pos_ = len_ = 0;
        if (!compressed_) {
            len_ = raw(out_.data(), out_.size());
            return len_ > 0;
        }
        for (;;) {
            if (inb_.pos == inb_.size) {
                inb_.size = raw(in_.data(), in_.size()); inb_.pos = 0;
                if (inb_.size == 0) {
                    if (!frame_done_) throw std::runtime_error("DBN: truncated zstd stream");
                    return false;
                }
            }
            ZSTD_outBuffer ob{out_.data(), out_.size(), 0};
            const size_t r = ZSTD_decompressStream(ds_, &ob, &inb_);
            if (ZSTD_isError(r)) throw std::runtime_error(std::string("zstd: ") + ZSTD_getErrorName(r));
            frame_done_ = (r == 0);
            if (ob.pos > 0) { len_ = ob.pos; return true; }
        }
    }
    FILE* f_ = nullptr;
    bool  compressed_ = true;
    ZSTD_DStream* ds_ = nullptr;
    std::vector<uint8_t> in_, out_, mem_;
    size_t mem_pos_ = 0;
    ZSTD_inBuffer inb_{};
    size_t pos_ = 0, len_ = 0;
    bool   frame_done_ = true;
    uint64_t raw_read_ = 0;
};

// ── Reader ──────────────────────────────────────────────────────────────────
class Reader {
public:
    explicit Reader(const std::string& path) : path_(path) {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) throw std::runtime_error("DBN: cannot open " + path);
        uint8_t magic[4] = {0, 0, 0, 0};
        const size_t k = std::fread(magic, 1, 4, f);
        std::rewind(f);
        const bool zst = k == 4 && magic[0] == 0x28 && magic[1] == 0xB5 && magic[2] == 0x2F && magic[3] == 0xFD;
        src_ = std::make_unique<ZstdReader>(f, zst);
        parse_metadata();
    }
    // In-memory buffer (tests). `compressed` = the buffer is a zstd frame.
    Reader(const std::vector<uint8_t>& buf, bool compressed) : path_("<memory>") {
        src_ = std::make_unique<ZstdReader>(buf, compressed);
        parse_metadata();
    }

    const Metadata& metadata() const { return meta_; }
    const std::string& path() const { return path_; }
    uint64_t records_read() const { return n_; }

    // Next record (valid until the following call), nullptr at EOF.
    const RecordHeader* next() {
        uint8_t len_words = 0;
        if (!src_->read(&len_words, 1)) return nullptr;
        const size_t len = (size_t)len_words * 4;
        if (len < sizeof(RecordHeader)) throw std::runtime_error("DBN: record length < header in " + path_);
        if (rec_.size() < len) rec_.resize(len < 64 ? 64 : len);
        rec_[0] = len_words;
        if (!src_->read(rec_.data() + 1, len - 1)) throw std::runtime_error("DBN: truncated record in " + path_);
        ++n_;
        cur_len_ = len;
        return reinterpret_cast<const RecordHeader*>(rec_.data());
    }
    size_t current_length() const { return cur_len_; }

    // Typed views (nullptr when the current record is not that type / too short).
    static const MboMsg*   as_mbo(const RecordHeader* h)   { return h && h->rtype == kMbo && h->length * 4u >= sizeof(MboMsg) ? reinterpret_cast<const MboMsg*>(h) : nullptr; }
    static const TradeMsg* as_trade(const RecordHeader* h) { return h && h->rtype == kMbp0 && h->length * 4u >= sizeof(TradeMsg) ? reinterpret_cast<const TradeMsg*>(h) : nullptr; }
    static const OhlcvMsg* as_ohlcv(const RecordHeader* h) {
        return h && h->rtype >= kOhlcv1s && h->rtype <= kOhlcvEod && h->length * 4u >= sizeof(OhlcvMsg)
                   ? reinterpret_cast<const OhlcvMsg*>(h) : nullptr;
    }

private:
    template <class T> static T rd(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof(T)); return v; }
    static std::string cstr(const uint8_t* p, size_t n) {
        size_t k = 0; while (k < n && p[k]) ++k;
        return std::string(reinterpret_cast<const char*>(p), k);
    }
    void parse_metadata() {
        uint8_t pre[8];
        if (!src_->read(pre, 8)) throw std::runtime_error("DBN: empty input " + path_);
        if (pre[0] != 'D' || pre[1] != 'B' || pre[2] != 'N') throw std::runtime_error("DBN: bad magic in " + path_);
        meta_.version = pre[3];
        if (meta_.version < 1 || meta_.version > 3)
            throw std::runtime_error("DBN: unsupported version " + std::to_string(meta_.version) + " in " + path_);
        const uint32_t mlen = rd<uint32_t>(pre + 4);
        std::vector<uint8_t> m(mlen);
        if (mlen && !src_->read(m.data(), mlen)) throw std::runtime_error("DBN: truncated metadata in " + path_);
        // Offsets below are relative to the end of the 8-byte prelude.
        size_t o = 0;
        auto need = [&](size_t n) { if (o + n > m.size()) throw std::runtime_error("DBN: metadata overrun in " + path_); };
        need(16 + 2 + 24);
        meta_.dataset = cstr(&m[o], 16); o += 16;
        meta_.schema  = rd<uint16_t>(&m[o]); o += 2;
        meta_.start   = rd<uint64_t>(&m[o]); o += 8;
        meta_.end     = rd<uint64_t>(&m[o]); o += 8;
        meta_.limit   = rd<uint64_t>(&m[o]); o += 8;
        if (meta_.version == 1) { need(8); o += 8; }            // v1: record_count (unused)
        need(3);
        meta_.stype_in  = m[o++];
        meta_.stype_out = m[o++];
        meta_.ts_out    = m[o++] != 0;
        if (meta_.version == 1) { meta_.symbol_cstr_len = 22; o += 47; }      // reserved
        else { need(2); meta_.symbol_cstr_len = rd<uint16_t>(&m[o]); o += 2; o += 53; }
        need(4);
        const uint32_t schema_def_len = rd<uint32_t>(&m[o]); o += 4;
        need(schema_def_len); o += schema_def_len;
        const size_t L = meta_.symbol_cstr_len;
        auto read_list = [&](std::vector<std::string>& v) {
            need(4); const uint32_t c = rd<uint32_t>(&m[o]); o += 4;
            for (uint32_t i = 0; i < c; ++i) { need(L); v.push_back(cstr(&m[o], L)); o += L; }
        };
        read_list(meta_.symbols);
        read_list(meta_.partial);
        read_list(meta_.not_found);
        need(4); const uint32_t nm = rd<uint32_t>(&m[o]); o += 4;
        for (uint32_t i = 0; i < nm; ++i) {
            SymbolMapping sm; need(L + 4); sm.raw_symbol = cstr(&m[o], L); o += L;
            const uint32_t ni = rd<uint32_t>(&m[o]); o += 4;
            for (uint32_t j = 0; j < ni; ++j) {
                MappingInterval iv; need(8 + L);
                iv.start_date = rd<uint32_t>(&m[o]); o += 4;
                iv.end_date   = rd<uint32_t>(&m[o]); o += 4;
                iv.symbol = cstr(&m[o], L); o += L;
                sm.intervals.push_back(iv);
            }
            meta_.mappings.push_back(sm);
        }
    }

    std::string path_;
    std::unique_ptr<ZstdReader> src_;
    Metadata meta_;
    std::vector<uint8_t> rec_;
    size_t cur_len_ = 0;
    uint64_t n_ = 0;
};

// ── Writer helpers (tests / synthetic fixtures) ─────────────────────────────
// Builds an uncompressed DBN stream: metadata (v1 or v2) + raw records.
inline std::vector<uint8_t> build_dbn(uint8_t version, const std::string& dataset, uint16_t schema,
                                      const std::vector<std::string>& symbols,
                                      const std::vector<SymbolMapping>& mappings,
                                      const std::vector<std::vector<uint8_t>>& records) {
    const size_t L = version == 1 ? 22 : 71;
    std::vector<uint8_t> m;
    auto put = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; m.insert(m.end(), b, b + n); };
    auto put_str = [&](const std::string& s, size_t n) { std::vector<uint8_t> z(n, 0); std::memcpy(z.data(), s.data(), std::min(n - 1, s.size())); put(z.data(), n); };
    put_str(dataset, 16);
    put(&schema, 2);
    uint64_t start = 0, end = UINT64_MAX, limit = 0, count = 0; put(&start, 8); put(&end, 8); put(&limit, 8);
    if (version == 1) put(&count, 8);
    uint8_t st_in = 3, st_out = 0, ts_out = 0; put(&st_in, 1); put(&st_out, 1); put(&ts_out, 1);
    if (version == 1) { std::vector<uint8_t> r(47, 0); put(r.data(), 47); }
    else { uint16_t l = (uint16_t)L; put(&l, 2); std::vector<uint8_t> r(53, 0); put(r.data(), 53); }
    uint32_t zero = 0; put(&zero, 4);                   // schema_definition_length
    uint32_t ns = (uint32_t)symbols.size(); put(&ns, 4); for (auto& s : symbols) put_str(s, L);
    put(&zero, 4); put(&zero, 4);                        // partial, not_found
    uint32_t nm = (uint32_t)mappings.size(); put(&nm, 4);
    for (auto& sm : mappings) {
        put_str(sm.raw_symbol, L); uint32_t ni = (uint32_t)sm.intervals.size(); put(&ni, 4);
        for (auto& iv : sm.intervals) { put(&iv.start_date, 4); put(&iv.end_date, 4); put_str(iv.symbol, L); }
    }
    while (m.size() % 8) m.push_back(0);                 // Databento pads metadata to 8 bytes
    std::vector<uint8_t> out = {'D', 'B', 'N', version};
    uint32_t mlen = (uint32_t)m.size();
    out.insert(out.end(), (uint8_t*)&mlen, (uint8_t*)&mlen + 4);
    out.insert(out.end(), m.begin(), m.end());
    for (auto& r : records) out.insert(out.end(), r.begin(), r.end());
    return out;
}
template <class T> std::vector<uint8_t> record_bytes(const T& rec) {
    std::vector<uint8_t> v(sizeof(T)); std::memcpy(v.data(), &rec, sizeof(T)); return v;
}
inline std::vector<uint8_t> zstd_compress(const std::vector<uint8_t>& in) {
    std::vector<uint8_t> out(ZSTD_compressBound(in.size()));
    const size_t r = ZSTD_compress(out.data(), out.size(), in.data(), in.size(), 3);
    if (ZSTD_isError(r)) throw std::runtime_error("zstd compress failed");
    out.resize(r); return out;
}

} // namespace dbn
