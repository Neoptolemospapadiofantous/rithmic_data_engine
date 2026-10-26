#pragma once
/*  risk.hpp — risk limits for the $5 Lighter pilot (all configurable; defaults below).

      1 market · quote ≈ $11 · max position 1 quote each way (on WORST-CASE exposure: position + fillable orders) ·
      max gross exposure $12 (mm_live: worst-case net exposure, OrderCheck::exposure_after_micro) · daily stop: equity ≤ start-of-day − $1.50 · total stop: equity ≤ all-time
      high-water mark − $2.50 · leverage 5 (gross ≤ 5 × equity) · KILL file halts and cancels all, never auto-resumes.

    Persistence: the baselines (pilot start equity, all-time HWM, start-of-day equity, the UTC day) and every halt
    are written to <state_dir>/risk_state.json atomically (tmp + fsync + rename + fsync of the directory) BEFORE the
    governor reports them. A restart loads them; nothing re-seeds a baseline from current equity except the first
    start (Open::Init) and a new UTC day (SOD only). With Open::Existing (the normal start) a missing state file is
    REFUSED — deleting the file can never reset the stops. A write failure throws: the caller must halt.        */

#include "dex/live/gateway_config.hpp"
#include "dex/live/order_state.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

namespace lighter::live {

struct RiskError : std::runtime_error { using std::runtime_error::runtime_error; };

enum class RiskAction { None, CancelAllAndFlatten };

struct RiskState {
    bool initialized = false;
    int64_t start_equity_micro = 0;
    int64_t hwm_equity_micro = 0;     // all-time high-water mark (persisted)
    int64_t day = -1;                 // UTC day number of sod_equity
    int64_t sod_equity_micro = 0;
    int64_t halt_day = -1;            // daily stop hit on this UTC day
    bool halt_total = false;
    bool halt_kill = false;
    std::string reason;
};

class RiskGovernor {
public:
    enum class Open {
        Existing,   // normal start: risk_state.json must exist
        Init,       // explicit first start (operator flag): create the state on the first equity reading;
                    // an existing file is loaded, never overwritten
    };

    RiskGovernor(RiskConfig cfg, std::string state_dir, Open mode = Open::Existing)
        : cfg_(cfg), dir_(std::move(state_dir)) {
        if (mode == Open::Init) std::filesystem::create_directories(dir_);
        bool exists = std::filesystem::exists(state_path());
        if (!exists && mode == Open::Existing)
            throw RiskError("risk state missing: " + state_path() + " (refusing to start; first start needs the init flag)");
        if (exists) load();
    }

    const RiskState& state() const { return st_; }
    const RiskConfig& config() const { return cfg_; }
    std::string state_path() const { return dir_ + "/risk_state.json"; }
    std::string kill_path() const { return dir_ + "/KILL"; }

    bool halted(int64_t now_unix_s) const {
        return st_.halt_kill || st_.halt_total || st_.halt_day == now_unix_s / 86400;
    }

    // Feed the exchange equity (collateral + unrealised). Returns CancelAllAndFlatten when a stop fires.
    RiskAction on_equity(int64_t equity_micro, int64_t now_unix_s) {
        int64_t today = now_unix_s / 86400;
        bool dirty = false;
        if (!st_.initialized) {
            st_.initialized = true; st_.start_equity_micro = equity_micro; st_.hwm_equity_micro = equity_micro;
            st_.day = today; st_.sod_equity_micro = equity_micro; dirty = true;
        } else if (today > st_.day) {
            st_.day = today; st_.sod_equity_micro = equity_micro; dirty = true;   // new UTC day: SOD written once
        }
        if (equity_micro > st_.hwm_equity_micro) { st_.hwm_equity_micro = equity_micro; dirty = true; }
        RiskAction a = RiskAction::None;
        if (!st_.halt_total && equity_micro - st_.hwm_equity_micro <= cfg_.total_stop_micro) {
            st_.halt_total = true; st_.reason = "total stop (HWM)"; dirty = true; a = RiskAction::CancelAllAndFlatten;
        }
        if (st_.halt_day != today && equity_micro - st_.sod_equity_micro <= cfg_.daily_stop_micro) {
            st_.halt_day = today; st_.reason = "daily stop"; dirty = true; a = RiskAction::CancelAllAndFlatten;
        }
        if (dirty) persist();
        return a;
    }

    // KILL file: halt (persisted) and cancel all. Deleting the file does NOT resume.
    RiskAction check_kill() {
        if (std::filesystem::exists(kill_path())) {
            if (!st_.halt_kill) { st_.halt_kill = true; st_.reason = "kill file"; persist(); }
            return RiskAction::CancelAllAndFlatten;
        }
        return st_.halt_kill ? RiskAction::CancelAllAndFlatten : RiskAction::None;
    }
    // Explicit human resume (a separate, deliberate operator action). Refused while the KILL file still exists.
    void human_clear_kill() {
        if (std::filesystem::exists(kill_path())) throw RiskError("remove the KILL file first");
        st_.halt_kill = false; persist();
    }

    void register_market(int64_t market) {
        if (markets_.count(market)) return;
        if (int(markets_.size()) >= cfg_.max_markets) throw RiskError("market limit reached");
        markets_.insert(market);
    }

    struct OrderCheck {
        int64_t market = -1;
        bool is_ask = false;
        int64_t qty = 0;                  // base units
        int64_t notional_micro = 0;
        bool reduce_only = false;
        int64_t max_position_base = 0;    // max_position_quotes × quote size (base units)
        SideExposure exposure;
        int64_t gross_micro = 0;          // Σ|position| notional + Σ fillable order notional, all markets
        int64_t exposure_after_micro = -1; // ≥ 0: the WORST-CASE exposure if this order is added (mm_live:
                                           // max(pos + Σ bids, −pos + Σ asks) × price) — used instead of gross + notional,
                                           // so an order that reduces the worst case (closing a position) is allowed
        int64_t equity_micro = 0;
        int64_t now_unix_s = 0;
    };
    // Empty = allowed. Reduce-only orders that shrink the position are allowed even while halted (flatten path) —
    // but together with the reduce-side orders already live they may not exceed the position (no flip).
    std::string check_order(const OrderCheck& c) const {
        if (!markets_.count(c.market)) return "market not registered";
        if (c.reduce_only) {
            int64_t pos = c.exposure.position;
            if (c.qty <= 0) return "reduce-only qty must be > 0";
            if (c.is_ask) {
                if (pos <= 0) return "reduce-only sell with no long position";
                if (c.qty + c.exposure.ask_fillable > pos) return "reduce-only sells would exceed the long position";
            } else {
                if (pos >= 0) return "reduce-only buy with no short position";
                if (c.qty + c.exposure.bid_fillable > -pos) return "reduce-only buys would exceed the short position";
            }
            return {};
        }
        if (halted(c.now_unix_s)) return "halted: " + st_.reason;
        if (c.notional_micro > cfg_.max_order_micro) return "order notional above cap";
        int64_t worst = c.is_ask ? c.exposure.worst_short() : c.exposure.worst_long();
        if (worst + c.qty > c.max_position_base) return "worst-case position above cap";
        const int64_t after = c.exposure_after_micro >= 0 ? c.exposure_after_micro : c.gross_micro + c.notional_micro;
        if (after > cfg_.max_gross_micro) return "gross exposure above cap";
        if (__int128(after) > __int128(c.equity_micro) * cfg_.leverage) return "leverage above cap";
        return {};
    }

private:
    void load() {
        std::ifstream f(state_path());
        if (!f) throw RiskError("cannot read risk state: " + state_path());
        nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
        if (j.is_discarded() || !j.is_object()) throw RiskError("risk state file is corrupt: " + state_path());
        try {
            st_.initialized = true;
            st_.start_equity_micro = j.at("start_equity_micro").get<int64_t>();
            st_.hwm_equity_micro = j.at("hwm_equity_micro").get<int64_t>();
            st_.day = j.at("day").get<int64_t>();
            st_.sod_equity_micro = j.at("sod_equity_micro").get<int64_t>();
            st_.halt_day = j.at("halt_day").get<int64_t>();
            st_.halt_total = j.at("halt_total").get<bool>();
            st_.halt_kill = j.at("halt_kill").get<bool>();
            st_.reason = j.value("reason", "");
        } catch (const nlohmann::json::exception& e) {
            throw RiskError(std::string("risk state file is incomplete: ") + e.what());
        }
    }
    void persist() {
        nlohmann::json j = {{"start_equity_micro", st_.start_equity_micro}, {"hwm_equity_micro", st_.hwm_equity_micro},
                            {"day", st_.day}, {"sod_equity_micro", st_.sod_equity_micro}, {"halt_day", st_.halt_day},
                            {"halt_total", st_.halt_total}, {"halt_kill", st_.halt_kill}, {"reason", st_.reason}};
        std::string tmp = state_path() + ".tmp", body = j.dump() + "\n";
        int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if (fd < 0) throw RiskError("cannot write risk state");
        bool ok = ::write(fd, body.data(), body.size()) == ssize_t(body.size()) && ::fsync(fd) == 0;
        ok = (::close(fd) == 0) && ok;
        if (!ok || std::rename(tmp.c_str(), state_path().c_str()) != 0) throw RiskError("cannot persist risk state");
        int dfd = ::open(dir_.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);   // make the rename itself durable
        if (dfd < 0) throw RiskError("cannot open state dir for fsync");
        bool dok = ::fsync(dfd) == 0;
        ::close(dfd);
        if (!dok) throw RiskError("cannot fsync state dir");
    }

    RiskConfig cfg_;
    std::string dir_;
    RiskState st_;
    std::set<int64_t> markets_;
};

}  // namespace lighter::live
