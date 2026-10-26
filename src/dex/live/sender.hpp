#pragma once
/*  sender.hpp — POST /api/v1/sendTx and /api/v1/sendTxBatch (lighter_tx_spec.md §4) and the reading of the answer.

    Wire format (application/x-www-form-urlencoded):
      sendTx       tx_type=<uint8>&tx_info=<the signer's JSON string>
      sendTxBatch  tx_types=<JSON array, e.g. [16,16]>&tx_infos=<JSON array of the signer's JSON STRINGS>
    Answer: JSON {code, message?, tx_hash | tx_hash[], …}. HTTP 200 alone proves nothing — body code 200 is required.

    Classification of one request (applies to every tx in it):
      NotSent       the request bytes were never written (connect/TLS failure): nothing consumed.
      Accepted      HTTP 200 + code 200: queued for the sequencer → Accepted-pending (nonce.hpp), NOT executed.
      RateLimited   HTTP 429 / 405, or code 23000 / 21506: nothing consumed; the limiter backs off.
      InvalidNonce  code 21104 / 21105: nothing consumed; our nonce view is wrong → freeze + resync.
      StandardLimit code 52002 ("reached the limit of Standard trading; only reduce"): nothing consumed; quoting must
                    stop (the engine halts and alerts).
      ApiRejected   any other 4xx on a SINGLE tx: rejected at the API, nothing consumed.
      Unknown       no answer after the write (timeout, cut connection), 5xx, an unparseable body, or a 4xx on a
                    BATCH (which prefix of a batch was applied after one bad tx is UNCONFIRMED) — never "failed":
                    freeze + resync from nextNonce decides what was consumed.                                    */

#include "dex/live/signer.hpp"
#include "dex/live/transport.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace lighter::live {

struct SendTxResult {
    enum class Kind { NotSent, Accepted, RateLimited, InvalidNonce, StandardLimit, ApiRejected, Unknown };
    Kind kind = Kind::Unknown;
    int http = 0, code = 0;
    std::string message;
    std::vector<std::string> tx_hashes;
};

inline const char* send_kind_name(SendTxResult::Kind k) {
    switch (k) {
        case SendTxResult::Kind::NotSent: return "not-sent";
        case SendTxResult::Kind::Accepted: return "accepted";
        case SendTxResult::Kind::RateLimited: return "rate-limited";
        case SendTxResult::Kind::InvalidNonce: return "invalid-nonce";
        case SendTxResult::Kind::StandardLimit: return "standard-limit";
        case SendTxResult::Kind::ApiRejected: return "api-rejected";
        case SendTxResult::Kind::Unknown: return "unknown";
    }
    return "?";
}

class Sender {
public:
    explicit Sender(HttpClient& http, int timeout_ms = 5'000) : http_(http), timeout_ms_(timeout_ms) {}

    static std::string form_send_tx(const SignedTx& t) {
        return form_encode({{"tx_type", std::to_string(int(t.tx_type))}, {"tx_info", t.tx_info}});
    }
    static std::string form_send_batch(const std::vector<SignedTx>& txs) {
        nlohmann::json types = nlohmann::json::array(), infos = nlohmann::json::array();
        for (const auto& t : txs) { types.push_back(int(t.tx_type)); infos.push_back(t.tx_info); }
        return form_encode({{"tx_types", types.dump()}, {"tx_infos", infos.dump()}});
    }

    static SendTxResult classify(const HttpResponse& r, size_t n_txs) {
        using K = SendTxResult::Kind;
        SendTxResult out;
        out.http = r.status;
        if (!r.sent) { out.kind = K::NotSent; out.message = r.error; return out; }
        if (r.status == 0) { out.kind = K::Unknown; out.message = r.error.empty() ? "no answer" : r.error; return out; }
        auto j = nlohmann::json::parse(r.body, nullptr, false);
        if (j.is_object()) {
            out.code = j.contains("code") && j["code"].is_number_integer() ? j["code"].get<int>() : 0;
            out.message = j.value("message", "");
            if (j.contains("tx_hash")) {
                if (j["tx_hash"].is_string()) out.tx_hashes.push_back(j["tx_hash"].get<std::string>());
                else if (j["tx_hash"].is_array()) for (auto& h : j["tx_hash"]) if (h.is_string()) out.tx_hashes.push_back(h.get<std::string>());
            }
        }
        if (r.status == 429 || r.status == 405 || out.code == 23000 || out.code == 21506) { out.kind = K::RateLimited; return out; }
        if (out.code == 21104 || out.code == 21105) { out.kind = K::InvalidNonce; return out; }
        if (out.code == 52002) { out.kind = K::StandardLimit; return out; }
        if (r.status == 200 && out.code == 200) { out.kind = K::Accepted; return out; }
        if (r.status >= 500 || !j.is_object()) { out.kind = K::Unknown; return out; }
        if (r.status >= 400 && r.status < 500) { out.kind = n_txs > 1 ? K::Unknown : K::ApiRejected; return out; }
        out.kind = K::Unknown;   // e.g. HTTP 200 with a body code other than 200 on an unlisted path
        return out;
    }

    SendTxResult send(const std::vector<SignedTx>& txs) {
        if (txs.empty()) throw std::invalid_argument("send of an empty batch");
        HttpRequest r;
        r.method = "POST";
        r.content_type = "application/x-www-form-urlencoded";
        r.timeout_ms = timeout_ms_;
        if (txs.size() == 1) { r.target = "/api/v1/sendTx"; r.body = form_send_tx(txs[0]); }
        else { r.target = "/api/v1/sendTxBatch"; r.body = form_send_batch(txs); }
        ++requests_;
        return classify(http_.request(r), txs.size());
    }
    int64_t requests() const { return requests_; }

private:
    HttpClient& http_;
    int timeout_ms_;
    int64_t requests_ = 0;
};

}  // namespace lighter::live
