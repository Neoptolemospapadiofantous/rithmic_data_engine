#pragma once
/*  gateway.hpp — glue between the batcher, the nonce manager and the signer (still OFFLINE: it produces signed
    tx_info strings; sending them is a separate, not-yet-written component).

    sign_batch reserves consecutive nonces for the whole batch first (batches must carry consecutive nonces on one
    API key), then signs in batch order. Intents are validated when the batcher accepts them, so a signing failure
    is exceptional; if one happens, the signed prefix is kept (a bad create never sinks the cancels before it), the
    remaining nonces are released unsent, and the rest of the batch goes back to the batcher (see sign_batch).

    Call this on the dedicated signing thread (≈ 0.43 ms per signature).                                      */

#include "dex/live/nonce.hpp"
#include "dex/live/signer.hpp"
#include "dex/live/tx_scheduler.hpp"

#include <functional>
#include <string>
#include <vector>

namespace lighter::live {

struct ApiCreds {
    int64_t api_key_index = 0;
    int64_t account_index = 0;
};

inline SignedTx sign_intent(Signer& s, const Intent& i, int64_t nonce, const ApiCreds& c, int64_t now_ms = 0) {
    switch (i.kind) {
        case IntentKind::Create: {
            CreateOrderParams p;
            p.market_index = i.market; p.client_order_index = i.order_ref; p.base_amount = i.base; p.price = i.price;
            p.is_ask = i.is_ask; p.tif = i.tif; p.reduce_only = i.reduce_only; p.order_expiry_ms = i.expiry_ms;
            p.self_trade_mode = 0; p.nonce = nonce; p.now_ms = now_ms;   // now_ms: the clock the expiry window is checked on (0 = wall)
            return s.sign_create_order(p, c.api_key_index, c.account_index);
        }
        case IntentKind::Modify: {
            ModifyOrderParams p;
            p.market_index = i.market; p.order_ref = i.order_ref; p.base_amount = i.base; p.price = i.price; p.nonce = nonce;
            return s.sign_modify_order(p, c.api_key_index, c.account_index);
        }
        case IntentKind::Cancel:
            return s.sign_cancel_order(i.market, i.order_ref, nonce, c.api_key_index, c.account_index);
        case IntentKind::CancelAll:
            return s.sign_cancel_all(i.ca_tif, i.ca_time_ms, i.market, nonce, c.api_key_index, c.account_index);
    }
    throw SignerError("unknown intent kind");
}

struct SignOptions {
    Batcher* requeue = nullptr;                 // where unsigned intents go back to (cancels keep their priority)
    std::vector<std::string>* errors = nullptr; // one line per intent that could not be signed
    int64_t now_ms = 0;                         // clock for the order-expiry window check (0 = the wall clock)
    std::function<bool()> armed;                // re-checked before EVERY batch; false → creates/modifies are
                                                // not signed (cancels always are). Empty = no check.
};

// Signs the batch in order (cancels first, as the batcher built it) on consecutive nonces and attaches the exact
// bytes to each nonce. If intent k cannot be signed: the signed prefix [0,k) is returned (so cancels already signed
// are never lost), the nonces from k on are released unsent, intent k is reported, and the intents after it are put
// back in the batcher. Without a requeue/errors sink, a failure throws after the release (strict mode).
inline std::vector<SignedTx> sign_batch(Signer& s, NonceManager& nm, std::vector<Intent> batch, const ApiCreds& c,
                                        const SignOptions& o = {}) {
    std::vector<SignedTx> out;
    if (o.armed && !o.armed()) {
        size_t before = batch.size();
        std::erase_if(batch, [](const Intent& i) { return i.kind == IntentKind::Create || i.kind == IntentKind::Modify; });
        if (o.errors && before != batch.size())
            o.errors->push_back("not armed: " + std::to_string(before - batch.size()) + " create/modify dropped");
    }
    if (batch.empty()) return out;
    std::vector<int64_t> nonces = nm.reserve_consecutive(batch.size());
    for (size_t k = 0; k < batch.size(); ++k) {
        try {
            out.push_back(sign_intent(s, batch[k], nonces[k], c, o.now_ms));
            nm.attach(nonces[k], out.back().tx_info);
        } catch (const std::exception& e) {
            if (!o.requeue && !o.errors) { nm.release_unsent(nonces.front()); throw; }   // strict: nothing sendable
            nm.release_unsent(nonces[k]);
            std::vector<Intent> rest(batch.begin() + std::ptrdiff_t(k) + 1, batch.end());
            if (o.requeue) o.requeue->requeue(rest);
            if (o.errors) o.errors->push_back(std::string("intent not signed: ") + e.what());
            break;
        }
    }
    return out;
}

}  // namespace lighter::live
