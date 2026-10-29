// mm_live — the live Lighter market maker (one market, $11 post-only quotes, the $5 pilot risk limits).
//
//   mm_live --config FILE --mode offline|probe|live [--archive FILE.jsonl.zst] [--max-events N] [--init-risk]
//           [--check-config]
//   mm_live --help
//
// Modes (the config's "mode" must equal --mode, or nothing starts — exit 2):
//   offline  one recorded Lighter archive day through the whole pipeline against the in-process FakeVenue: no socket,
//            no key file (a throwaway in-memory key), alerts to the log only. Exit 0 only if every end-of-run
//            invariant holds (src/dex/live/offline.hpp).
//   probe    mainnet, real key, armed: cancel-all + adopt, then ONE post-only bid ≥ 20 % below the best bid at the
//            minimum size, ack, cancel, confirm no open order, one dead-man re-arm (abort + schedule) confirmed by
//            the sequencer, then a clean stop (cancel-all, dead-man abort). Writes <state_dir>/probe_report.json.
//   live     mainnet, real key, armed: the market maker. SIGTERM/SIGINT = cancel-all, wait, abort the dead-man, exit.
// --check-config validates config, key file, arming and network pairing and exits — it opens no socket.
// --init-risk: the FIRST start only — creates the persisted risk baselines (a missing risk_state.json is refused
//            otherwise, so deleting it can never reset the loss stops).
//
// Safety rails (see docs/mm_live_runbook.md): mainnet host + chain 304 only (testnet refused by name); the signer is
// sha256-pinned and only allowlisted symbols are resolved (no leverage/transfer/withdraw entry point exists); the
// key file must be 0600, outside every git tree; probe/live need the arming file and no NO_DEPLOY_MM; a single
// instance per state dir (flock); GODEBUG=asyncpreemptoff=1 is put in place by re-exec'ing once if missing.

#include "dex/live/driver.hpp"
#include "dex/live/engine.hpp"
#include "dex/live/instance_lock.hpp"
#include "dex/live/mm_live_config.hpp"
#include "dex/live/net.hpp"
#include "dex/live/offline.hpp"

#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>

#include <atomic>
#include <memory>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace lighter::live;
namespace fs = std::filesystem;

static constexpr const char* kRepoRoot = "/home/theone/Desktop/rithmic_engine";
static volatile std::sig_atomic_t g_stop = 0;
static void on_signal(int) { g_stop = 1; }

static std::string g_label = "mm_live";
static void logline(const std::string& s) {
    std::time_t t = std::time(nullptr);
    char ts[32];
    std::strftime(ts, sizeof ts, "[%Y-%m-%d %H:%M:%S]", std::gmtime(&t));
    std::printf("%s [%s] %s\n", ts, g_label.c_str(), s.c_str());
    std::fflush(stdout);
}

static void usage(FILE* f) {
    std::fprintf(f, "usage: mm_live --config FILE --mode offline|probe|live [--archive FILE] [--max-events N] [--init-risk] [--check-config]\n"
                    "       mm_live --help\n");
}

int main(int argc, char** argv) {
    // Go reads the environment the process was EXEC'ed with: put GODEBUG=asyncpreemptoff=1 there first.
    try { reexec_with_godebug_if_needed(argv); } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 2; }

    std::string config_path, mode_s, archive;
    int64_t max_events = 0;
    bool init_risk = false, check_only = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto val = [&]() -> std::string {
            if (i + 1 >= argc) { usage(stderr); std::exit(2); }
            return argv[++i];
        };
        if (a == "--help" || a == "-h") { usage(stdout); return 0; }
        else if (a == "--config") config_path = val();
        else if (a == "--mode") mode_s = val();
        else if (a == "--archive") archive = val();
        else if (a == "--max-events") max_events = std::atoll(val().c_str());
        else if (a == "--init-risk") init_risk = true;
        else if (a == "--check-config") check_only = true;
        else { std::fprintf(stderr, "unknown argument '%s'\n", a.c_str()); usage(stderr); return 2; }
    }
    if (config_path.empty() || mode_s.empty()) { usage(stderr); return 2; }

    MmLiveConfig mc;
    KeyMaterial km;
    try {
        Mode cli_mode = parse_mode(mode_s);
        std::ifstream f(config_path);
        if (!f) throw ConfigError("cannot read config " + config_path);
        nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
        if (j.is_discarded()) throw ConfigError("config is not valid JSON");
        GatewayConfig gw = parse_config(j, cli_mode, std::time(nullptr));
        mc = build_mm_live_config(gw);
        if (sends_orders(gw.mode)) km = load_key_file(gw, kRepoRoot);
        if (gw.mode == Mode::Offline && archive.empty() && !check_only) throw ConfigError("offline mode needs --archive FILE");
        if (gw.mode != Mode::Offline && (!archive.empty() || max_events)) throw ConfigError("--archive/--max-events are offline-only");
    } catch (const ConfigError& e) {
        std::fprintf(stderr, "config refused: %s\n", e.what());
        return 2;
    }
    g_label = mc.engine.label;
    if (check_only) {
        logline("config OK: mode " + std::string(mode_name(mc.gw.mode)) + ", market " + mc.engine.spec.symbol + " (" +
                std::to_string(mc.engine.spec.market_index) + "), account " + std::to_string(mc.gw.account_index) +
                ", key index " + std::to_string(mc.gw.api_key_index) + ", host " + mc.gw.host + " chain " + std::to_string(mc.gw.chain_id) +
                (sends_orders(mc.gw.mode) ? ", key file + arming OK" : "") + " — no socket opened");
        {   // the strategy this config would quote with (frozen + engine.strategy overrides)
            hl::Params p = mmpaper::frozen_params();
            mc.engine.strategy.apply(p);
            char b[256];
            std::snprintf(b, sizeof b, "strategy: half width %.2f, min spread %.2f, imbalance pull %.2f, pause %.2f ticks / %lld ms, requote %.2f ticks",
                          p.half_width_ticks, p.min_spread_ticks, p.imb_pull, p.pause_move_ticks, (long long)p.pause_ms, p.requote_ticks);
            logline(b);
            const auto bn = mc.engine.strategy.bn_pull();
            if (bn.on()) std::snprintf(b, sizeof b, "Binance-lead pull: on — %lld ms window, %.2f bps, hold %lld ms (wss://fstream.binance.com bookTicker)",
                                       (long long)bn.w_ms, bn.x_bps, (long long)bn.hold_ms);
            else std::snprintf(b, sizeof b, "Binance-lead pull: off");
            logline(b);
        }
        return 0;
    }

    // offline keeps its own state (risk baselines, run ids, journal) away from the real account's
    if (mc.gw.mode == Mode::Offline) {
        mc.engine.state_dir += "/offline";
        mc.engine.journal_dir += "/offline";
        mc.engine.status_file = mc.engine.state_dir + "/status.json";
    }
    std::error_code ec;
    fs::create_directories(mc.engine.state_dir, ec);
    fs::create_directories(mc.engine.journal_dir, ec);
    if (!mc.engine.status_file.empty()) fs::create_directories(fs::path(mc.engine.status_file).parent_path(), ec);

    // single instance per state dir (held until exit; the kernel releases it on any death)
    std::string lock_path = mc.engine.state_dir + "/mm_live.lock";
    auto lock = InstanceLock::acquire(lock_path);
    if (!lock) {
        std::fprintf(stderr, "instance lock REFUSED: %s is held by another mm_live\n", lock_path.c_str());
        return 2;
    }
    // …and one per (account, API key) whatever the state dir: two configs pointing at the same key can never both
    // send (they would fight over one nonce stream). Real accounts only — the offline soak's account is fake.
    std::optional<InstanceLock> acct_lock;
    if (mc.gw.mode != Mode::Offline) {
        const char* home = std::getenv("HOME");
        fs::path dir = fs::path(home ? home : "/tmp") / ".local/state/mm_live";
        fs::create_directories(dir, ec);
        std::string p = (dir / ("acct" + std::to_string(mc.gw.account_index) + "_key" + std::to_string(mc.gw.api_key_index) + ".lock")).string();
        acct_lock = InstanceLock::acquire(p);
        if (!acct_lock) {
            std::fprintf(stderr, "instance lock REFUSED: %s (this account + API key) is held by another mm_live\n", p.c_str());
            return 2;
        }
    }

    struct sigaction sa {};
    sa.sa_handler = on_signal;
    sa.sa_flags = SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);

    try {
        Signer signer(Signer::Options{kSignerPath, kSignerSha256, /*allow_generate_key*/ mc.gw.mode == Mode::Offline});
        if (mc.gw.mode == Mode::Offline) {
            ApiKeyPair k = signer.generate_api_key();   // throwaway, in memory, never written anywhere
            signer.create_client(k.private_key, kMainnetChainId, mc.gw.api_key_index, mc.gw.account_index);
            std::fill(k.private_key.begin(), k.private_key.end(), '\0');
        } else {
            signer.create_client(km.private_key, kMainnetChainId, mc.gw.api_key_index, mc.gw.account_index);
            std::fill(km.private_key.begin(), km.private_key.end(), '\0');
        }
        RiskGovernor risk(mc.engine.risk, mc.engine.state_dir,
                          init_risk || mc.gw.mode == Mode::Offline ? RiskGovernor::Open::Init : RiskGovernor::Open::Existing);
        int64_t run_id = next_run_id(mc.engine.state_dir);
        Journal journal(mc.engine.journal_dir, mc.engine.label);
        Alerter alerts(mc.gw.mode == Mode::Offline ? Alerter::Sink([](const std::string& s) { logline("[alert→log] " + s); })
                                                   : Alerter::grid_notify());

        if (mc.gw.mode == Mode::Offline) {
            OfflineOptions oo;
            oo.archive = archive;
            oo.max_events = max_events;
            oo.stop = [] { return g_stop != 0; };
            oo.log = [](const std::string& s) { logline(s); };   // the soak logs exactly what live would
            OfflineResult r = run_offline_soak(mc, signer, risk, journal, alerts, run_id, oo);
            std::printf("%s\n", r.summary.dump(1).c_str());
            logline(r.ok ? "offline soak PASSED: every invariant holds" : "offline soak FAILED");
            return r.ok ? 0 : 1;
        }

        GatewayConfig gw = mc.gw;
        ArmingGate gate(gw);
        WallClock clock;
        BeastHttps http(Endpoint{});
        BeastWs account_ws(Endpoint{});
        BeastWs public_ws(Endpoint{});
        Endpoint bn_ep; bn_ep.host = kReferenceFeedHost; bn_ep.reference_feed = true;   // Binance-lead pull (read-only, only if configured)
        std::unique_ptr<BeastWs> ref_ws;
        Engine eng(mc.engine, Engine::Deps{signer, http, clock, risk, journal, alerts,
                                           [&gate](int64_t now_s) { return gate.armed(now_s); }, logline}, run_id);
        if (!eng.start()) { logline("start refused — exiting"); return 1; }
        if (eng.ref_wanted()) ref_ws = std::make_unique<BeastWs>(bn_ep, eng.ref_ws_path(), 1'500);   // < the 2 s stall guard
        Driver drv(eng, account_ws, public_ws, clock, logline, ref_ws.get());
        logline(std::string("running (") + mode_name(mc.gw.mode) + ")");
        while (!g_stop) {
            drv.step();
            if (mc.gw.mode == Mode::Probe && eng.probe_done()) break;
            clock.sleep_ms(20);
        }
        bool clean = eng.shutdown(g_stop ? "signal" : "probe finished");
        for (int k = 0; k < 10; ++k) { drv.step(); clock.sleep_ms(100); }   // let the last confirmations arrive
        account_ws.close();
        public_ws.close();
        if (ref_ws) ref_ws->close();
        if (mc.gw.mode == Mode::Probe) {
            std::ofstream(mc.engine.state_dir + "/probe_report.json") << eng.probe_report().dump(1) << "\n";
            logline("probe report: " + eng.probe_report().dump());
            return eng.probe_ok() && clean ? 0 : 1;
        }
        return clean ? 0 : 1;
    } catch (const std::exception& e) {
        logline(std::string("fatal: ") + e.what());
        return 1;
    }
}
