#pragma once
// recorder_status.hpp — a small health file per market-data recorder (lighter_recorder, hl_recorder,
// dex_recorder), read by scripts/recorder_watch.sh. Pure bookkeeping + one atomic file write; no network.
//
// File (default data/recorder_status/<name>.json, rewritten every few seconds via tmp + rename):
//   {"name","pid","started_us","updated_us","connected","reconnects","gaps",
//    "write_errors","dropped_lines","frames",            ← summed over the archive writers (zstd_daily.hpp)
//    "busiest","busiest_msgs","busiest_last_us","busiest_age_s",
//    "markets":{"<SYM>":{"last_us","last_data_us","books","trades","stats","gaps","silence_max_s"}},
//    "extra":{…recorder-specific…}}
// "busiest" = the market with the most book + trade messages since start; busiest_age_s = seconds since its
// last book or trade message at the time of the write. The watch alerts on busiest_age_s + the file's own age,
// so a hung process (no writes) and a silent feed (writes, but no data) both trip it.

#include <nlohmann/json.hpp>

#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

namespace rec {

struct MarketHealth {
    int64_t last_us = 0;        // any message (incl. stats)
    int64_t last_data_us = 0;   // book or trade message
    int64_t books = 0, trades = 0, stats = 0, gaps = 0;
    double silence_max_s = 0;   // longest wall-clock gap between two data messages since start (reconnect pauses included)
};

class Status {
public:
    enum class Kind { Book, Trade, Stats, Other };

    explicit Status(std::string name, int64_t started_us) : name_(std::move(name)), started_us_(started_us) {}

    void add_market(const std::string& m) { markets_[m]; }
    void on_msg(const std::string& m, Kind k, int64_t recv_us) {
        auto& h = markets_[m];
        h.last_us = std::max(h.last_us, recv_us);
        if (k == Kind::Book || k == Kind::Trade) {
            if (h.last_data_us > 0) h.silence_max_s = std::max(h.silence_max_s, (recv_us - h.last_data_us) / 1e6);
            h.last_data_us = std::max(h.last_data_us, recv_us);
            ++(k == Kind::Book ? h.books : h.trades);
        } else if (k == Kind::Stats) {
            ++h.stats;
        }
    }
    void on_gap(const std::string& m) { ++markets_[m].gaps; ++gaps_; }
    void on_reconnect() { ++reconnects_; connected_ = false; }
    void set_connected(bool c) { connected_ = c; }
    void set_writer_totals(int64_t write_errors, int64_t dropped_lines, int64_t frames) {
        write_errors_ = write_errors; dropped_lines_ = dropped_lines; frames_ = frames;
    }
    nlohmann::json& extra() { return extra_; }
    const std::map<std::string, MarketHealth>& markets() const { return markets_; }

    // The market with the most book + trade messages (ties → first by name); empty when none has data yet.
    std::string busiest() const {
        std::string best;
        int64_t n = 0;
        for (const auto& [m, h] : markets_)
            if (h.books + h.trades > n) { n = h.books + h.trades; best = m; }
        return best;
    }

    nlohmann::json to_json(int64_t now_us) const {
        nlohmann::json mk = nlohmann::json::object();
        for (const auto& [m, h] : markets_)
            mk[m] = {{"last_us", h.last_us}, {"last_data_us", h.last_data_us}, {"books", h.books}, {"trades", h.trades},
                     {"stats", h.stats}, {"gaps", h.gaps}, {"silence_max_s", h.silence_max_s}};
        const std::string b = busiest();
        nlohmann::json j = {{"name", name_}, {"pid", static_cast<int64_t>(::getpid())}, {"started_us", started_us_},
                            {"updated_us", now_us}, {"connected", connected_}, {"reconnects", reconnects_},
                            {"gaps", gaps_}, {"write_errors", write_errors_}, {"dropped_lines", dropped_lines_},
                            {"frames", frames_}, {"busiest", b}, {"markets", mk}, {"extra", extra_}};
        if (!b.empty()) {
            const auto& h = markets_.at(b);
            j["busiest_msgs"] = h.books + h.trades;
            j["busiest_last_us"] = h.last_data_us;
            j["busiest_age_s"] = (now_us - h.last_data_us) / 1e6;
        } else {
            j["busiest_msgs"] = 0;
            j["busiest_last_us"] = 0;
            j["busiest_age_s"] = (now_us - started_us_) / 1e6;   // nothing yet: age since start
        }
        return j;
    }

    // tmp + rename, so a reader never sees half a file. Returns false (and leaves the old file) on any error.
    bool write(const std::filesystem::path& file, int64_t now_us) const {
        std::error_code ec;
        if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
        const auto tmp = file.string() + ".tmp";
        {
            std::ofstream o(tmp, std::ios::trunc);
            if (!o) return false;
            o << to_json(now_us).dump() << '\n';
            if (!o.good()) return false;
        }
        std::filesystem::rename(tmp, file, ec);
        return !ec;
    }

private:
    std::string name_;
    int64_t started_us_ = 0;
    std::map<std::string, MarketHealth> markets_;
    int64_t reconnects_ = 0, gaps_ = 0, write_errors_ = 0, dropped_lines_ = 0, frames_ = 0;
    bool connected_ = false;
    nlohmann::json extra_ = nlohmann::json::object();
};

}  // namespace rec
