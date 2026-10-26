#pragma once
/*  signer.hpp — offline wrapper around the pinned Lighter signer (lighter-python v1.1.6 shared library).

    Safety design (OFFLINE BY CONSTRUCTION):
      * The .so is read into memory, its sha256 checked against the pinned value, copied into a memfd and dlopen'ed
        from that memfd — the bytes that were verified are exactly the bytes that are loaded (no TOCTOU on the path).
      * GODEBUG=asyncpreemptoff=1 is set before dlopen (no SIGURG into C++ threads; see signer_abi.md §11).
        MEASURED 2026-10-26: a setenv() inside the process is NOT seen by the Go runtime — a c-shared Go runtime
        reads the process's INITIAL environment (strace: SIGURGs still delivered after setenv; none when GODEBUG was
        in the environment at exec). So load() also checks /proc/self/environ and refuses to load unless the
        variable was there at exec time; a binary calls reexec_with_godebug_if_needed(argv) first thing in main()
        (it re-execs itself once with the variable set), or the systemd unit sets Environment=GODEBUG=….
      * Only the symbols in kAllowedSymbols are ever passed to dlsym. Money-moving exports (SignWithdraw,
        SignTransfer, SignChangePubKey, SignCreateSubAccount, pool/stake/integrator/leverage/margin …) are never
        resolved, so no code path in this process can call them. GenerateAPIKey is resolved only when the caller asks
        for it (tests).
      * CreateClient is always called with an EMPTY url, so the signer has no HTTP client: every sign call must carry
        an explicit nonce (>= 0). With url "" a nonce of -1 would fail inside the signer instead of doing a network
        GET; we refuse -1 before the call anyway.
      * Every argument is range-checked BEFORE the call: the C ABI silently truncates (market → int16, price →
        uint32, api key index → uint8).
      * Every char* the library returns is released with the library's own Free().

    THREADING: one signature costs ~0.43 ms (Schnorr/Poseidon2). The signer is thread-safe (Go-side mutex), but the
    CALLER must run signing on its own dedicated thread — never on the market-data / Asio I/O thread. This header
    does no threading of its own.

    The Go runtime cannot be unloaded, so the library is loaded once per process and never dlclose'd. Loading a
    second time with the same path/hash returns the same handle; a different path/hash throws.          */

#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>

#include <openssl/evp.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace lighter::live {

// ── C ABI structs (layout measured in signer_abi.md §2; must match the v1.1.6 header) ─────────────────────────
extern "C" {
struct SignedTxResponseC { uint8_t txType; char* txInfo; char* txHash; char* messageToSign; char* err; };
struct StrOrErrC { char* str; char* err; };
struct ApiKeyResponseC { char* privateKey; char* publicKey; char* err; };
}
static_assert(sizeof(SignedTxResponseC) == 40 && offsetof(SignedTxResponseC, err) == 32);
static_assert(sizeof(StrOrErrC) == 16);
static_assert(sizeof(ApiKeyResponseC) == 24);

inline constexpr const char* kSignerPath =
    "/home/theone/Desktop/rithmic_engine/third_party/lighter_signer/v1.1.6/lighter-signer-linux-amd64.so";
inline constexpr const char* kSignerSha256 = "1022a277cbb8fb9647886a611bc02151ad28cdbc5a02f7ac6416faa2898be392";

// The ONLY names this process may pass to dlsym. GenerateAPIKey is resolved only with allow_generate_key (tests).
inline constexpr std::array<std::string_view, 10> kAllowedSymbols = {
    "CreateClient", "CheckClient", "SignCreateOrder", "SignModifyOrder", "SignCancelOrder",
    "SignCancelAllOrders", "SignCreateGroupedOrders", "CreateAuthToken", "Free", "GenerateAPIKey"};

inline bool symbol_allowed(std::string_view name) {
    for (auto s : kAllowedSymbols) if (s == name) return true;
    return false;
}

// ── Lighter constants (lighter-go types/txtypes/constants.go, see lighter_tx_spec.md) ────────────────────────
inline constexpr int64_t kMaxClientOrderIndex = (int64_t{1} << 48) - 1;        // 1 .. 2^48-1
inline constexpr int64_t kMinExchangeOrderIndex = int64_t{1} << 48;            // 2^48 .. 2^60-1
inline constexpr int64_t kMaxExchangeOrderIndex = (int64_t{1} << 60) - 1;
inline constexpr int64_t kMaxBaseAmount = (int64_t{1} << 48) - 1;
inline constexpr int64_t kMaxNonce = (int64_t{1} << 48) - 1;
inline constexpr int64_t kMaxOrderVersion = (int64_t{1} << 48) - 1;
inline constexpr int kNilMarketIndex = 255;                                    // "all markets" in cancel-all only
inline constexpr int kMainnetChainId = 304;
inline constexpr int kMinApiKeyIndex = 4, kMaxApiKeyIndex = 254;               // 0-3 reserved by Lighter, 255 sentinel

inline constexpr uint8_t kTxCreateOrder = 14, kTxCancelOrder = 15, kTxCancelAll = 16, kTxModifyOrder = 17;
enum class OrderType : int { Limit = 0 };                                      // the gateway only ever sends limits
enum class TimeInForce : int { IOC = 0, GTT = 1, PostOnly = 2 };
enum class CancelAllTif : int { Immediate = 0, Scheduled = 1, Abort = 2 };

struct SignerError : std::runtime_error { using std::runtime_error::runtime_error; };
struct RangeError : SignerError { using SignerError::SignerError; };   // thrown BEFORE any call into the library

// ── range checks (public so the gateway can pre-validate intents) ────────────────────────────────────────────
inline void check_market_index(int64_t m) {
    if (m < 0 || m > 32767) throw RangeError("market index " + std::to_string(m) + " does not fit int16");
    if (m == kNilMarketIndex) throw RangeError("market index 255 is the nil/all-markets sentinel");
}
inline void check_price(int64_t p) {
    if (p <= 0 || p > int64_t{UINT32_MAX}) throw RangeError("price " + std::to_string(p) + " outside 1..2^32-1");
}
inline void check_api_key_index(int64_t k) {
    if (k < kMinApiKeyIndex || k > kMaxApiKeyIndex)
        throw RangeError("api key index " + std::to_string(k) + " outside 4..254");
}
inline void check_account_index(int64_t a) {
    if (a <= 0) throw RangeError("account index must be > 0");
}
inline void check_base_amount(int64_t b) {
    if (b <= 0 || b > kMaxBaseAmount) throw RangeError("base amount " + std::to_string(b) + " outside 1..2^48-1");
}
inline void check_client_order_index(int64_t c) {
    if (c < 1 || c > kMaxClientOrderIndex) throw RangeError("client order index " + std::to_string(c) + " outside 1..2^48-1");
}
inline void check_order_ref(int64_t idx) {   // modify/cancel: client index OR exchange order index (disjoint ranges)
    bool client = idx >= 1 && idx <= kMaxClientOrderIndex;
    bool exch = idx >= kMinExchangeOrderIndex && idx <= kMaxExchangeOrderIndex;
    if (!client && !exch) throw RangeError("order index " + std::to_string(idx) + " is neither a client nor an exchange index");
}
inline void check_nonce(int64_t n) {
    if (n < 0 || n > kMaxNonce) throw RangeError("nonce " + std::to_string(n) + " outside 0..2^48-1 (explicit nonce required)");
}

// True when GODEBUG with asyncpreemptoff=1 was in the environment this process was EXEC'ed with (what Go reads).
inline bool godebug_in_initial_environment() {
    std::ifstream f("/proc/self/environ", std::ios::binary);
    std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    size_t pos = 0;
    while (pos < all.size()) {
        size_t end = all.find('\0', pos);
        if (end == std::string::npos) end = all.size();
        std::string_view kv(all.data() + pos, end - pos);
        if (kv.rfind("GODEBUG=", 0) == 0 && kv.find("asyncpreemptoff=1") != std::string_view::npos) return true;
        pos = end + 1;
    }
    return false;
}

// Call first thing in main(): if the initial environment lacks GODEBUG=asyncpreemptoff=1, set it and exec this
// same binary again with the same argv (once — the re-exec'ed process has it). Returns only when it is in place.
inline void reexec_with_godebug_if_needed(char** argv) {
    if (godebug_in_initial_environment()) return;
    const char* gd = std::getenv("GODEBUG");
    std::string v = gd && *gd ? std::string(gd) + ",asyncpreemptoff=1" : "asyncpreemptoff=1";
    ::setenv("GODEBUG", v.c_str(), 1);
    ::execv("/proc/self/exe", argv);
    throw std::runtime_error("re-exec with GODEBUG failed");
}

inline std::string sha256_hex(const std::vector<unsigned char>& bytes) {
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    if (EVP_Digest(bytes.data(), bytes.size(), md, &len, EVP_sha256(), nullptr) != 1) throw SignerError("sha256 failed");
    static const char* hx = "0123456789abcdef";
    std::string out;
    for (unsigned i = 0; i < len; ++i) { out += hx[md[i] >> 4]; out += hx[md[i] & 15]; }
    return out;
}

inline int64_t wall_now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// Lighter: GTT / PostOnly need an order expiry 5 min .. 30 days ahead (MinOrderExpiryPeriod / MaxOrderExpiryPeriod);
// IOC needs 0. The signer's -1 (= now + 28 days) is refused: expiry is always explicit.
inline constexpr int64_t kMinOrderExpiryAheadMs = 5 * 60'000;
inline constexpr int64_t kMaxOrderExpiryAheadMs = 30LL * 86'400'000;
inline void check_order_expiry(TimeInForce tif, int64_t expiry_ms, int64_t now_ms) {
    if (tif == TimeInForce::IOC) {
        if (expiry_ms != 0) throw RangeError("IOC orders need order_expiry 0");
    } else if (tif == TimeInForce::GTT || tif == TimeInForce::PostOnly) {
        if (expiry_ms <= 0) throw RangeError("GTT/PostOnly orders need an explicit order_expiry > 0 (ms)");
        if (expiry_ms - now_ms < kMinOrderExpiryAheadMs) throw RangeError("order expiry less than 5 min ahead");
        if (expiry_ms - now_ms > kMaxOrderExpiryAheadMs) throw RangeError("order expiry more than 30 days ahead");
    } else {
        throw RangeError("unknown time in force");
    }
}

struct SignedTx {
    uint8_t tx_type = 0;
    std::string tx_info;   // the signer's JSON string — the exact bytes to send as tx_info
    std::string tx_hash;   // hex, no 0x
    int64_t nonce = -1;
};

struct ApiKeyPair { std::string private_key, public_key; };

struct CreateOrderParams {
    int64_t market_index = -1;
    int64_t client_order_index = 0;
    int64_t base_amount = 0;
    int64_t price = 0;
    bool is_ask = false;
    TimeInForce tif = TimeInForce::PostOnly;
    bool reduce_only = false;
    int64_t order_expiry_ms = 0;          // explicit; >0 for GTT/PostOnly, 0 for IOC. -1 (28-day default) refused.
    uint8_t self_trade_mode = 0;          // explicit STP on every create (0 ExpireMaker .. 3 Reduce)
    int64_t nonce = -1;
    int64_t now_ms = 0;                   // clock for the expiry-window check; 0 = system clock
};

struct ModifyOrderParams {
    int64_t market_index = -1;
    int64_t order_ref = 0;                // client order index or exchange order index
    int64_t base_amount = 0;              // new total size; 0 refused (meaning unconfirmed on Lighter)
    int64_t price = 0;
    int64_t order_version = 0;            // 0 = no version guard
    uint8_t self_trade_mode = 0;
    int64_t nonce = -1;
};

class Signer {
public:
    struct Options {
        std::string path = kSignerPath;
        std::string sha256 = kSignerSha256;
        bool allow_generate_key = false;  // tests only
    };

    Signer() : Signer(Options{}) {}
    explicit Signer(Options o) : lib_(load(o)), allow_gen_(o.allow_generate_key) {}

    // Names actually resolved by dlsym in this process (asserted by the allowlist test).
    static std::vector<std::string> resolved_symbols() {
        std::lock_guard lk(mu());
        return state().resolved;
    }
    // Number of calls made INTO the library (lets tests prove a range check fired before the call).
    static uint64_t library_calls() { return calls().load(); }

    // url is always "" — the signer gets no HTTP client. chain id must be mainnet 304.
    void create_client(const std::string& private_key_hex, int chain_id, int64_t api_key_index, int64_t account_index) {
        if (chain_id != kMainnetChainId) throw RangeError("chain id " + std::to_string(chain_id) + " refused (mainnet 304 only)");
        check_api_key_index(api_key_index);
        check_account_index(account_index);
        if (private_key_hex.empty()) throw RangeError("empty private key");
        std::string url;              // EMPTY: offline by construction
        std::vector<char> key(private_key_hex.begin(), private_key_hex.end());
        key.push_back('\0');
        ++calls();
        char* err = lib_->CreateClient(url.data(), key.data(), chain_id, int(api_key_index), (long long)account_index);
        std::fill(key.begin(), key.end(), '\0');
        take_err(err, "CreateClient");
    }

    // CheckClient would GET /apikeys through the signer's HTTP client. With url "" there is none, so this can only
    // fail locally. Kept for R-NET-3 once a separate, read-only REST path exists; it never opens a socket here.
    std::string check_client(int64_t api_key_index, int64_t account_index) {
        check_api_key_index(api_key_index);
        check_account_index(account_index);
        ++calls();
        char* err = lib_->CheckClient(int(api_key_index), (long long)account_index);
        std::string e = err ? err : "";
        if (err) lib_->Free(err);
        return e;   // empty = ok
    }

    SignedTx sign_create_order(const CreateOrderParams& p, int64_t api_key_index, int64_t account_index) {
        check_market_index(p.market_index);
        check_client_order_index(p.client_order_index);
        check_base_amount(p.base_amount);
        check_price(p.price);
        check_nonce(p.nonce);
        check_api_key_index(api_key_index);
        check_account_index(account_index);
        if (p.self_trade_mode > 3) throw RangeError("self-trade mode outside 0..3");
        check_order_expiry(p.tif, p.order_expiry_ms, p.now_ms ? p.now_ms : wall_now_ms());
        int price_bits;
        uint32_t pu = uint32_t(p.price);
        std::memcpy(&price_bits, &pu, sizeof price_bits);   // bit-cast: the C ABI takes int, Go casts to uint32
        ++calls();
        SignedTxResponseC r = lib_->SignCreateOrder(int(p.market_index), (long long)p.client_order_index,
            (long long)p.base_amount, price_bits, p.is_ask ? 1 : 0, int(OrderType::Limit), int(p.tif),
            p.reduce_only ? 1 : 0, /*trigger*/ 0, (long long)p.order_expiry_ms, /*integrator*/ 0, 0, 0,
            p.self_trade_mode, /*equality*/ 0, /*skip nonce*/ 0, (long long)p.nonce, int(api_key_index),
            (long long)account_index);
        return take_tx(r, p.nonce, "SignCreateOrder");
    }

    SignedTx sign_modify_order(const ModifyOrderParams& p, int64_t api_key_index, int64_t account_index) {
        check_market_index(p.market_index);
        check_order_ref(p.order_ref);
        check_base_amount(p.base_amount);
        check_price(p.price);
        check_nonce(p.nonce);
        check_api_key_index(api_key_index);
        check_account_index(account_index);
        if (p.order_version < 0 || p.order_version > kMaxOrderVersion) throw RangeError("order version outside 0..2^48-1");
        if (p.self_trade_mode > 3) throw RangeError("self-trade mode outside 0..3");
        ++calls();
        SignedTxResponseC r = lib_->SignModifyOrder(int(p.market_index), (long long)p.order_ref, (long long)p.base_amount,
            (long long)p.price, /*trigger*/ 0, /*integrator*/ 0, 0, 0, p.self_trade_mode, 0, /*skip*/ 0,
            (long long)p.nonce, (long long)p.order_version, int(api_key_index), (long long)account_index);
        return take_tx(r, p.nonce, "SignModifyOrder");
    }

    SignedTx sign_cancel_order(int64_t market_index, int64_t order_ref, int64_t nonce, int64_t api_key_index,
                               int64_t account_index) {
        check_market_index(market_index);
        check_order_ref(order_ref);
        check_nonce(nonce);
        check_api_key_index(api_key_index);
        check_account_index(account_index);
        ++calls();
        SignedTxResponseC r = lib_->SignCancelOrder(int(market_index), (long long)order_ref, 0, (long long)nonce,
                                                    int(api_key_index), (long long)account_index);
        return take_tx(r, nonce, "SignCancelOrder");
    }

    // market_index 255 = all markets. A single-market cancel-all must be Immediate (lighter-go Validate).
    SignedTx sign_cancel_all(CancelAllTif tif, int64_t time_ms, int64_t market_index, int64_t nonce,
                             int64_t api_key_index, int64_t account_index) {
        if (market_index != kNilMarketIndex) {
            check_market_index(market_index);
            if (tif != CancelAllTif::Immediate) throw RangeError("per-market cancel-all must be immediate");
        }
        if (tif == CancelAllTif::Scheduled) {
            if (time_ms <= 0) throw RangeError("scheduled cancel-all needs time_ms > 0");
        } else if (time_ms != 0) {
            throw RangeError("immediate/abort cancel-all needs time 0");
        }
        check_nonce(nonce);
        check_api_key_index(api_key_index);
        check_account_index(account_index);
        ++calls();
        SignedTxResponseC r = lib_->SignCancelAllOrders(int(tif), (long long)time_ms, int(market_index), 0,
                                                        (long long)nonce, int(api_key_index), (long long)account_index);
        return take_tx(r, nonce, "SignCancelAllOrders");
    }

    // deadline = absolute unix seconds, explicit (the 0 → +7h default is unconfirmed for this .so). Lighter caps 8 h.
    std::string create_auth_token(int64_t deadline_unix_s, int64_t now_unix_s, int64_t api_key_index, int64_t account_index) {
        if (deadline_unix_s <= now_unix_s || deadline_unix_s > now_unix_s + 8 * 3600)
            throw RangeError("auth token deadline must be in (now, now+8h]");
        check_api_key_index(api_key_index);
        check_account_index(account_index);
        ++calls();
        StrOrErrC r = lib_->CreateAuthToken((long long)deadline_unix_s, int(api_key_index), (long long)account_index);
        std::string s = r.str ? r.str : "", e = r.err ? r.err : "";
        if (r.str) lib_->Free(r.str);
        if (r.err) lib_->Free(r.err);
        if (!e.empty()) throw SignerError("CreateAuthToken: " + e);
        return s;
    }

    // TESTS ONLY: a throwaway key pair, kept in memory.
    ApiKeyPair generate_api_key() {
        if (!allow_gen_ || !lib_->GenerateAPIKey) throw SignerError("GenerateAPIKey is test-only and was not resolved");
        ++calls();
        ApiKeyResponseC r = lib_->GenerateAPIKey();
        ApiKeyPair k{r.privateKey ? r.privateKey : "", r.publicKey ? r.publicKey : ""};
        std::string e = r.err ? r.err : "";
        if (r.privateKey) { std::memset(r.privateKey, 0, std::strlen(r.privateKey)); lib_->Free(r.privateKey); }
        if (r.publicKey) lib_->Free(r.publicKey);
        if (r.err) lib_->Free(r.err);
        if (!e.empty()) throw SignerError("GenerateAPIKey: " + e);
        return k;
    }

private:
    struct Lib {
        void* handle = nullptr;
        std::string path, sha;
        char* (*CreateClient)(char*, char*, int, int, long long) = nullptr;
        char* (*CheckClient)(int, long long) = nullptr;
        SignedTxResponseC (*SignCreateOrder)(int, long long, long long, int, int, int, int, int, int, long long,
                                             long long, int, int, uint8_t, uint8_t, uint8_t, long long, int, long long) = nullptr;
        SignedTxResponseC (*SignModifyOrder)(int, long long, long long, long long, long long, long long, int, int,
                                             uint8_t, uint8_t, uint8_t, long long, long long, int, long long) = nullptr;
        SignedTxResponseC (*SignCancelOrder)(int, long long, uint8_t, long long, int, long long) = nullptr;
        SignedTxResponseC (*SignCancelAllOrders)(int, long long, int, uint8_t, long long, int, long long) = nullptr;
        void* SignCreateGroupedOrders = nullptr;   // resolved (allowlisted), no wrapper: not used for quoting
        StrOrErrC (*CreateAuthToken)(long long, int, long long) = nullptr;
        void (*Free)(void*) = nullptr;
        ApiKeyResponseC (*GenerateAPIKey)() = nullptr;
    };
    struct State { Lib lib; bool loaded = false; std::vector<std::string> resolved; };
    static std::mutex& mu() { static std::mutex m; return m; }
    static State& state() { static State s; return s; }
    static std::atomic<uint64_t>& calls() { static std::atomic<uint64_t> c{0}; return c; }

    template <class F>
    static void resolve(State& st, const char* name, F& out) {
        if (!symbol_allowed(name)) throw SignerError(std::string("symbol not on the allowlist: ") + name);
        void* p = dlsym(st.lib.handle, name);
        if (!p) throw SignerError(std::string("dlsym failed: ") + name);
        out = reinterpret_cast<F>(p);
        st.resolved.emplace_back(name);
    }

    static Lib* load(const Options& o) {
        std::lock_guard lk(mu());
        State& st = state();
        if (st.loaded) {
            if (st.lib.path != o.path || st.lib.sha != o.sha256)
                throw SignerError("signer already loaded from a different path/hash (the Go runtime cannot be reloaded)");
            if (o.allow_generate_key && !st.lib.GenerateAPIKey) resolve(st, "GenerateAPIKey", st.lib.GenerateAPIKey);
            return &st.lib;
        }
        if (!godebug_in_initial_environment())
            throw SignerError("GODEBUG=asyncpreemptoff=1 must be in the environment at exec "
                              "(call reexec_with_godebug_if_needed in main or set it in the unit)");
        std::ifstream f(o.path, std::ios::binary);
        if (!f) throw SignerError("cannot read signer " + o.path);
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        std::string got = sha256_hex(bytes);
        if (got != o.sha256) throw SignerError("signer sha256 mismatch: " + got);

        // Load exactly the verified bytes from an anonymous memfd.
        int fd = memfd_create("lighter-signer", MFD_CLOEXEC);
        if (fd < 0) throw SignerError("memfd_create failed");
        size_t off = 0;
        while (off < bytes.size()) {
            ssize_t w = ::write(fd, bytes.data() + off, bytes.size() - off);
            if (w <= 0) { ::close(fd); throw SignerError("memfd write failed"); }
            off += size_t(w);
        }
        const char* gd = std::getenv("GODEBUG");
        std::string godebug = gd ? gd : "";
        if (godebug.find("asyncpreemptoff=1") == std::string::npos)
            godebug = godebug.empty() ? "asyncpreemptoff=1" : godebug + ",asyncpreemptoff=1";
        ::setenv("GODEBUG", godebug.c_str(), 1);
        std::string proc = "/proc/self/fd/" + std::to_string(fd);
        void* h = dlopen(proc.c_str(), RTLD_NOW | RTLD_LOCAL);
        ::close(fd);
        if (!h) throw SignerError(std::string("dlopen failed: ") + (dlerror() ? dlerror() : "?"));
        st.lib.handle = h;
        st.lib.path = o.path;
        st.lib.sha = o.sha256;
        resolve(st, "CreateClient", st.lib.CreateClient);
        resolve(st, "CheckClient", st.lib.CheckClient);
        resolve(st, "SignCreateOrder", st.lib.SignCreateOrder);
        resolve(st, "SignModifyOrder", st.lib.SignModifyOrder);
        resolve(st, "SignCancelOrder", st.lib.SignCancelOrder);
        resolve(st, "SignCancelAllOrders", st.lib.SignCancelAllOrders);
        resolve(st, "SignCreateGroupedOrders", st.lib.SignCreateGroupedOrders);
        resolve(st, "CreateAuthToken", st.lib.CreateAuthToken);
        resolve(st, "Free", st.lib.Free);
        if (o.allow_generate_key) resolve(st, "GenerateAPIKey", st.lib.GenerateAPIKey);
        st.loaded = true;
        return &st.lib;
    }

    void take_err(char* err, const char* what) {
        if (!err) return;
        std::string e = err;
        lib_->Free(err);
        throw SignerError(std::string(what) + ": " + e);
    }

    SignedTx take_tx(SignedTxResponseC& r, int64_t nonce, const char* what) {
        SignedTx t;
        t.tx_type = r.txType;
        t.nonce = nonce;
        std::string e = r.err ? r.err : "";
        if (r.txInfo) t.tx_info = r.txInfo;
        if (r.txHash) t.tx_hash = r.txHash;
        for (char* p : {r.txInfo, r.txHash, r.messageToSign, r.err}) if (p) lib_->Free(p);
        if (!e.empty()) throw SignerError(std::string(what) + ": " + e);
        if (t.tx_info.empty() || t.tx_hash.empty()) throw SignerError(std::string(what) + ": empty result");
        return t;
    }

    Lib* lib_;
    bool allow_gen_;
};

}  // namespace lighter::live
