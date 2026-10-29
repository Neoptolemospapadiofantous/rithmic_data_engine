cmake_minimum_required(VERSION 3.20)
project(rithmic_engine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Dependencies ───────────────────────────────────────────────────
find_package(Boost 1.82 REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)
find_package(Protobuf REQUIRED)

# PostgreSQL (libpq)
find_package(PostgreSQL REQUIRED)
find_library(ZSTD_LIBRARY NAMES zstd REQUIRED)   # dbn reader (paper_engine --dbn-mbo, dbn_tool) + crypto collector

# ── Protobuf codegen ───────────────────────────────────────────────
set(PROTO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/proto/rithmic.proto")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/proto")
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILE})

# ── Shared sources (used by main binary and tests) ─────────────────
set(LIB_SOURCES
    src/db.cpp
    src/audit.cpp
    src/client.cpp
    src/collector.cpp
    ${PROTO_SRCS}
)

add_library(rithmic_lib STATIC ${LIB_SOURCES})

target_include_directories(rithmic_lib PUBLIC
    src/
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Boost_INCLUDE_DIRS}
    ${Protobuf_INCLUDE_DIRS}
    ${PostgreSQL_INCLUDE_DIRS}
)

target_link_libraries(rithmic_lib PUBLIC
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    ${Protobuf_LIBRARIES}
    ${PostgreSQL_LIBRARIES}
)

target_compile_definitions(rithmic_lib PUBLIC
    BOOST_ASIO_HAS_CO_AWAIT
)

# ── Main binary ────────────────────────────────────────────────────
add_executable(rithmic_engine src/main.cpp)
target_link_libraries(rithmic_engine PRIVATE rithmic_lib)

# ── Test binaries ──────────────────────────────────────────────────
add_executable(test_db tests/test_db.cpp)
target_link_libraries(test_db PRIVATE rithmic_lib)
target_include_directories(test_db PRIVATE src/)

add_executable(test_connection tests/test_connection.cpp)
target_link_libraries(test_connection PRIVATE rithmic_lib)
target_include_directories(test_connection PRIVATE src/)

add_executable(test_validator tests/test_validator.cpp)
target_link_libraries(test_validator PRIVATE rithmic_lib)
target_include_directories(test_validator PRIVATE src/)

# ── Execution layer unit tests (C3 audit finding) ─────────────────────────────
# Standalone — no external test framework, no DB, no Rithmic SDK required.
add_executable(test_risk_manager tests/execution/test_risk_manager.cpp)
target_include_directories(test_risk_manager PRIVATE src/execution src/)
target_compile_features(test_risk_manager PRIVATE cxx_std_20)

add_executable(test_orb_strategy tests/execution/test_orb_strategy.cpp)
target_include_directories(test_orb_strategy PRIVATE src/execution src/)
target_compile_features(test_orb_strategy PRIVATE cxx_std_20)

add_executable(test_trend_strategy tests/execution/test_trend_strategy.cpp)
target_include_directories(test_trend_strategy PRIVATE src/execution src/)
target_compile_features(test_trend_strategy PRIVATE cxx_std_20)

add_executable(test_mtf_scalper
    tests/execution/test_mtf_scalper.cpp
    src/execution/orb_strategy.cpp
)
target_include_directories(test_mtf_scalper PRIVATE src/execution src/)
target_compile_features(test_mtf_scalper PRIVATE cxx_std_20)

add_executable(test_order_manager
    tests/execution/test_order_manager.cpp
    src/execution/order_manager.cpp
    src/execution/risk_manager.cpp
)
target_include_directories(test_order_manager PRIVATE src/execution src/)
target_compile_features(test_order_manager PRIVATE cxx_std_20)

add_executable(test_incident_replay
    tests/execution/test_incident_replay.cpp
    src/execution/order_manager.cpp
    src/execution/risk_manager.cpp
)
target_include_directories(test_incident_replay PRIVATE src/execution src/)
target_compile_features(test_incident_replay PRIVATE cxx_std_20)

add_executable(test_trade_end_invariant
    tests/execution/test_trade_end_invariant.cpp
    src/execution/order_manager.cpp
    src/execution/risk_manager.cpp
)
target_include_directories(test_trade_end_invariant PRIVATE src/execution src/)
target_compile_features(test_trade_end_invariant PRIVATE cxx_std_20)

add_executable(test_lifecycle
    tests/execution/test_lifecycle.cpp
    src/execution/order_manager.cpp
    src/execution/risk_manager.cpp
)
target_include_directories(test_lifecycle PRIVATE src/execution src/)
target_compile_features(test_lifecycle PRIVATE cxx_std_20)

enable_testing()
add_test(NAME db_tests              COMMAND test_db)
add_test(NAME validator_tests       COMMAND test_validator)
add_test(NAME risk_manager_tests    COMMAND test_risk_manager)
add_test(NAME orb_strategy_tests    COMMAND test_orb_strategy)
add_test(NAME order_manager_tests   COMMAND test_order_manager)
add_test(NAME lifecycle_tests       COMMAND test_lifecycle)
add_test(NAME incident_replay_tests COMMAND test_incident_replay)
add_test(NAME trade_end_invariant_tests COMMAND test_trade_end_invariant)
add_test(NAME paper_broker_tests    COMMAND test_paper_broker
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME mtf_scalper_tests     COMMAND test_mtf_scalper)
add_test(NAME bracket_broker_tests  COMMAND test_bracket_broker
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})


# ── orb_strategy (standalone ORB signal binary for CI parity tests) ──
# Header-only nlohmann/json; no link deps beyond stdlib.
add_executable(orb_strategy src/orb_strategy_main.cpp)
target_compile_features(orb_strategy PRIVATE cxx_std_17)

# ── NQ ORB Executor ────────────────────────────────────────────────
option(USE_RAPI_SDK "Use native Rithmic R|API+ TCP feed for market data (faster, lower latency)" OFF)

set(RAPI_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sdk")

set(NQ_EXECUTOR_SOURCES
    src/execution/executor_main.cpp
    src/execution/orb_strategy.cpp
    src/execution/order_manager.cpp
    src/execution/risk_manager.cpp
    src/execution/latency_logger.cpp
)

add_executable(nq_executor ${NQ_EXECUTOR_SOURCES})

target_include_directories(nq_executor PRIVATE
    src/execution/
    src/
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Boost_INCLUDE_DIRS}
    ${Protobuf_INCLUDE_DIRS}
    ${PostgreSQL_INCLUDE_DIRS}
)

target_link_libraries(nq_executor PRIVATE rithmic_lib)

target_compile_definitions(nq_executor PRIVATE
    BOOST_ASIO_HAS_CO_AWAIT
)

if(USE_RAPI_SDK)
    message(STATUS "USE_RAPI_SDK=ON — linking native Rithmic R|API+ for market data")

    if(NOT EXISTS "${RAPI_SDK_DIR}/lib/libRApiPlus-optimize.a")
        message(FATAL_ERROR
            "SDK libs not found at ${RAPI_SDK_DIR}/lib/\n"
            "Run:  cp ~/Downloads/RApiPlus.cpp.13.7.0.0/13.7.0.0/linux-gnu-4.18-x86_64/lib/*.a sdk/lib/")
    endif()

    target_compile_definitions(nq_executor PRIVATE USE_RAPI_SDK -DLINUX -D_REENTRANT)
    target_compile_options(nq_executor PRIVATE
        -Wno-sign-compare -Wno-write-strings -Wno-deprecated
        -fno-strict-aliasing
    )
    target_include_directories(nq_executor PRIVATE "${RAPI_SDK_DIR}/include")
    target_link_directories(nq_executor PRIVATE "${RAPI_SDK_DIR}/lib")
    target_link_libraries(nq_executor PRIVATE
        RApiPlus-optimize
        OmneStreamEngine-optimize
        OmneChannel-optimize
        OmneEngine-optimize
        _api-optimize
        _apipoll-stubs-optimize
        _kit-optimize
        ssl                   # bundled static OpenSSL (SDK version)
        crypto
        z pthread rt dl
    )
else()
    message(STATUS "USE_RAPI_SDK=OFF — using WebSocket (Boost.Beast) market data feed")
endif()

# ── Cancel Stops utility ───────────────────────────────────────────
# Reads pending_stop_cancels from DB and fires RequestCancelOrder for each.
# Safe to run alongside a live nq_executor.
add_executable(cancel_stops tools/cancel_stops_main.cpp
    src/execution/latency_logger.cpp)
target_include_directories(cancel_stops PRIVATE
    src/execution/
    src/
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Boost_INCLUDE_DIRS}
    ${Protobuf_INCLUDE_DIRS}
    ${PostgreSQL_INCLUDE_DIRS}
)
target_link_libraries(cancel_stops PRIVATE rithmic_lib)
target_compile_definitions(cancel_stops PRIVATE BOOST_ASIO_HAS_CO_AWAIT)
target_compile_features(cancel_stops PRIVATE cxx_std_20)

# ── Hedge coordinator ──────────────────────────────────────────────
# Keeps hedge accounts at the mirror of their anchor accounts' net exposure
# (hedge_pairs / hedge_status tables; dashboard Hedges page manages bindings).
add_executable(hedge_coordinator src/hedge/hedge_main.cpp)
target_include_directories(hedge_coordinator PRIVATE
    src/
    src/hedge/
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Boost_INCLUDE_DIRS}
    ${Protobuf_INCLUDE_DIRS}
    ${PostgreSQL_INCLUDE_DIRS}
)
target_link_libraries(hedge_coordinator PRIVATE rithmic_lib)
target_compile_definitions(hedge_coordinator PRIVATE BOOST_ASIO_HAS_CO_AWAIT)
target_compile_features(hedge_coordinator PRIVATE cxx_std_20)

# ── Paper fleet engine ─────────────────────────────────────────────
# Runs ~20 paper strategies against the live tick stream in PostgreSQL.
# Never connects to Rithmic; only libpq + header-only strategy/risk code.
add_executable(paper_engine
    src/paper/paper_main.cpp
    src/paper/paper_db.cpp
)
target_include_directories(paper_engine PRIVATE
    src/execution/
    src/
    src/paper/
    ${PostgreSQL_INCLUDE_DIRS}
)
target_link_libraries(paper_engine PRIVATE
    ${PostgreSQL_LIBRARIES}
    ${ZSTD_LIBRARY}
)
target_compile_features(paper_engine PRIVATE cxx_std_20)

add_executable(test_paper_broker
    tests/paper/test_paper_broker.cpp
    src/paper/paper_db.cpp
)
target_include_directories(test_paper_broker PRIVATE
    src/execution/
    src/
    src/paper/
    ${PostgreSQL_INCLUDE_DIRS}
)
target_link_libraries(test_paper_broker PRIVATE
    ${PostgreSQL_LIBRARIES}
)
target_compile_features(test_paper_broker PRIVATE cxx_std_20)

add_executable(test_parity_paper_vs_live
    tests/paper/test_parity_paper_vs_live.cpp
    src/paper/paper_db.cpp
    src/execution/order_manager.cpp
    src/execution/risk_manager.cpp
)
target_include_directories(test_parity_paper_vs_live PRIVATE src/execution/ src/ src/paper/ ${PostgreSQL_INCLUDE_DIRS})
target_link_libraries(test_parity_paper_vs_live PRIVATE ${PostgreSQL_LIBRARIES})
target_compile_features(test_parity_paper_vs_live PRIVATE cxx_std_20)
add_test(NAME parity_paper_vs_live COMMAND test_parity_paper_vs_live)

add_executable(test_bracket_broker
    tests/paper/test_bracket_broker.cpp
    src/paper/paper_db.cpp
)
target_include_directories(test_bracket_broker PRIVATE
    src/execution/
    src/
    src/paper/
    ${PostgreSQL_INCLUDE_DIRS}
)
target_link_libraries(test_bracket_broker PRIVATE
    ${PostgreSQL_LIBRARIES}
)
target_compile_features(test_bracket_broker PRIVATE cxx_std_20)

# ── Crypto collector (Binance USDⓈ-M futures ticks/bbo + Bitstamp MBO) ──────────
# Standalone: Boost.Beast websockets + libpq + zstd + nlohmann/json. No Rithmic, no protobuf.
find_library(ZSTD_LIBRARY NAMES zstd REQUIRED)
add_executable(crypto_collector src/crypto/crypto_collector_main.cpp)
target_include_directories(crypto_collector PRIVATE src/ ${Boost_INCLUDE_DIRS} ${PostgreSQL_INCLUDE_DIRS})
target_link_libraries(crypto_collector PRIVATE Boost::system OpenSSL::SSL OpenSSL::Crypto
    ${PostgreSQL_LIBRARIES} ${ZSTD_LIBRARY} pthread)
target_compile_definitions(crypto_collector PRIVATE BOOST_ASIO_HAS_CO_AWAIT)
target_compile_features(crypto_collector PRIVATE cxx_std_20)

add_executable(test_crypto_collector tests/crypto/test_crypto_collector.cpp)
target_include_directories(test_crypto_collector PRIVATE src/)
target_compile_features(test_crypto_collector PRIVATE cxx_std_20)
target_link_libraries(test_crypto_collector PRIVATE pthread)
add_test(NAME crypto_collector_tests COMMAND test_crypto_collector)

# ── DEX market-making research (Hyperliquid books/trades recorder + queue-aware MM simulator) ──
# Research only: no orders, no keys, no Postgres. Model in src/dex/hl_mm.hpp.
add_executable(hl_recorder src/dex/hl_recorder_main.cpp)
target_include_directories(hl_recorder PRIVATE src/ ${Boost_INCLUDE_DIRS})
target_link_libraries(hl_recorder PRIVATE Boost::system OpenSSL::SSL OpenSSL::Crypto ${ZSTD_LIBRARY} pthread)
target_compile_definitions(hl_recorder PRIVATE BOOST_ASIO_HAS_CO_AWAIT)
target_compile_features(hl_recorder PRIVATE cxx_std_20)

find_package(ZLIB REQUIRED)       # Tardis history is gzip CSV (src/dex/hl_events.hpp)
# mm_rl_label: counterfactual (state, action, markout-adjusted reward) dataset for a state-dependent quoting policy (2026-10-28)
add_executable(mm_rl_label src/dex/mm_rl_label_main.cpp)
target_include_directories(mm_rl_label PRIVATE src/)
target_link_libraries(mm_rl_label PRIVATE ${ZSTD_LIBRARY} ZLIB::ZLIB)
target_compile_features(mm_rl_label PRIVATE cxx_std_20)

add_executable(mm_rl_judge src/dex/mm_rl_judge_main.cpp)
target_include_directories(mm_rl_judge PRIVATE src/)
target_compile_features(mm_rl_judge PRIVATE cxx_std_20)

add_executable(mm_sim src/dex/mm_sim_main.cpp)
target_include_directories(mm_sim PRIVATE src/)
target_link_libraries(mm_sim PRIVATE ${ZSTD_LIBRARY} ZLIB::ZLIB)
target_compile_features(mm_sim PRIVATE cxx_std_20)

# Archive health per file (lines, skipped zstd frames, Lighter sequence gaps, book silences) — scripts/mm_debug.sh archive.
add_executable(archive_check src/dex/archive_check_main.cpp)
target_include_directories(archive_check PRIVATE src/)
target_link_libraries(archive_check PRIVATE ${ZSTD_LIBRARY} ZLIB::ZLIB)
target_compile_features(archive_check PRIVATE cxx_std_20)

add_executable(mm_opt src/dex/mm_opt_main.cpp)
target_include_directories(mm_opt PRIVATE src/)
target_link_libraries(mm_opt PRIVATE ${ZSTD_LIBRARY} ZLIB::ZLIB pthread)
target_compile_features(mm_opt PRIVATE cxx_std_20)

add_executable(mm_robust src/dex/mm_robust_main.cpp)
target_include_directories(mm_robust PRIVATE src/)
target_link_libraries(mm_robust PRIVATE ${ZSTD_LIBRARY} ZLIB::ZLIB pthread)
target_compile_features(mm_robust PRIVATE cxx_std_20)

add_executable(test_hl_mm tests/dex/test_hl_mm.cpp)
target_include_directories(test_hl_mm PRIVATE src/)
target_link_libraries(test_hl_mm PRIVATE ${ZSTD_LIBRARY} ZLIB::ZLIB)
target_compile_features(test_hl_mm PRIVATE cxx_std_20)
add_test(NAME hl_mm_tests COMMAND test_hl_mm)

# mm_lab: the deterministic research judge for market-making ideas (src/dex/mm_lab.hpp; desk wrapper
# scripts/starnet/desk_mm.sh). Reads archives + data/mm_gates params, writes cards and data/mm_lab/registry.jsonl.
add_executable(mm_lab src/dex/mm_lab_main.cpp)
target_include_directories(mm_lab PRIVATE src/)
target_link_libraries(mm_lab PRIVATE ${ZSTD_LIBRARY} ZLIB::ZLIB pthread)
target_compile_features(mm_lab PRIVATE cxx_std_20)
target_compile_definitions(mm_lab PRIVATE MM_LAB_REPO="${CMAKE_SOURCE_DIR}")   # pinned registry + frozen boundaries
# mm_fairlab: scores fair-price (centre) models by how well they predict Lighter's mid 1 s / 10 s ahead (research only)
add_executable(mm_fairlab src/dex/mm_fairlab_main.cpp)
target_include_directories(mm_fairlab PRIVATE src/)
target_link_libraries(mm_fairlab PRIVATE ${ZSTD_LIBRARY} ZLIB::ZLIB pthread)
target_compile_features(mm_fairlab PRIVATE cxx_std_20)

add_executable(test_mm_lab tests/dex/test_mm_lab.cpp)
target_include_directories(test_mm_lab PRIVATE src/)
target_link_libraries(test_mm_lab PRIVATE ${ZSTD_LIBRARY} ZLIB::ZLIB pthread)
target_compile_features(test_mm_lab PRIVATE cxx_std_20)
add_test(NAME mm_lab_tests COMMAND test_mm_lab)
# funding_lab: stage F0 of docs/funding_carry_plan.md — the pre-registered cross-venue funding-carry backtest
# (research/preregistered/2026-10-28_funding_carry.md); reads scripts/funding_history.sh output. Research only.
add_executable(funding_lab src/dex/funding_lab_main.cpp)
target_include_directories(funding_lab PRIVATE src/)
target_compile_features(funding_lab PRIVATE cxx_std_20)
add_executable(test_funding_lab tests/dex/test_funding_lab.cpp)
target_include_directories(test_funding_lab PRIVATE src/)
target_compile_features(test_funding_lab PRIVATE cxx_std_20)
add_test(NAME funding_lab_tests COMMAND test_funding_lab)

# Lighter: public market-data recorder + the 24/7 paper market maker (no keys, no order path).
add_executable(lighter_recorder src/dex/lighter_recorder_main.cpp)
target_include_directories(lighter_recorder PRIVATE src/ ${Boost_INCLUDE_DIRS})
target_link_libraries(lighter_recorder PRIVATE Boost::system OpenSSL::SSL OpenSSL::Crypto ${ZSTD_LIBRARY} pthread)
target_compile_definitions(lighter_recorder PRIVATE BOOST_ASIO_HAS_CO_AWAIT)
target_compile_features(lighter_recorder PRIVATE cxx_std_20)

add_executable(mm_paper src/dex/mm_paper_main.cpp)
target_include_directories(mm_paper PRIVATE src/ ${Boost_INCLUDE_DIRS} ${PostgreSQL_INCLUDE_DIRS})
target_link_libraries(mm_paper PRIVATE Boost::system OpenSSL::SSL OpenSSL::Crypto ${PostgreSQL_LIBRARIES} pthread)
target_compile_definitions(mm_paper PRIVATE BOOST_ASIO_HAS_CO_AWAIT)
target_compile_features(mm_paper PRIVATE cxx_std_20)

# The other zero-maker-fee venues (Extended, Aster, Paradex): one recorder binary, same archive shape and simulator.
add_executable(dex_recorder src/dex/dex_recorder_main.cpp)
target_include_directories(dex_recorder PRIVATE src/ ${Boost_INCLUDE_DIRS})
target_link_libraries(dex_recorder PRIVATE Boost::system OpenSSL::SSL OpenSSL::Crypto ${ZSTD_LIBRARY} pthread)
target_compile_definitions(dex_recorder PRIVATE BOOST_ASIO_HAS_CO_AWAIT)
target_compile_features(dex_recorder PRIVATE cxx_std_20)

add_executable(mm_regime src/dex/mm_regime_main.cpp)
target_include_directories(mm_regime PRIVATE src/)
target_link_libraries(mm_regime PRIVATE ${ZSTD_LIBRARY} ZLIB::ZLIB pthread)
target_compile_features(mm_regime PRIVATE cxx_std_20)

add_executable(test_venues tests/dex/test_venues.cpp)
target_include_directories(test_venues PRIVATE src/)
target_link_libraries(test_venues PRIVATE ${ZSTD_LIBRARY} ZLIB::ZLIB)
target_compile_features(test_venues PRIVATE cxx_std_20)
add_test(NAME venues_tests COMMAND test_venues)

add_executable(test_lighter tests/dex/test_lighter.cpp)
target_include_directories(test_lighter PRIVATE src/)
target_link_libraries(test_lighter PRIVATE ${ZSTD_LIBRARY} ZLIB::ZLIB)
target_compile_features(test_lighter PRIVATE cxx_std_20)
add_test(NAME lighter_tests COMMAND test_lighter)

# Recording side of the DEX archive: market_stats parsing + loader skip, orderBookDetails snapshot/diff,
# recorder status file, time-boxed zstd frames (2026-10-26).
add_executable(test_recorder tests/dex/test_recorder.cpp)
target_include_directories(test_recorder PRIVATE src/)
target_link_libraries(test_recorder PRIVATE ${ZSTD_LIBRARY} ZLIB::ZLIB)
target_compile_features(test_recorder PRIVATE cxx_std_20)
add_test(NAME recorder_tests COMMAND test_recorder)

# ── Databento history (DBN reader, MBO book, dbn_tool) ──────────────────────────
# Header-only reader src/history/dbn.hpp + mbo_feed.hpp; dbn_tool loads bars / MBO minute stats into
# Postgres; paper_engine --dbn-mbo replays MBO files directly (no ticks in Postgres).
add_executable(dbn_tool src/history/dbn_tool_main.cpp)
target_include_directories(dbn_tool PRIVATE src/ ${PostgreSQL_INCLUDE_DIRS})
target_link_libraries(dbn_tool PRIVATE ${PostgreSQL_LIBRARIES} ${ZSTD_LIBRARY})
target_compile_features(dbn_tool PRIVATE cxx_std_20)

add_executable(test_dbn tests/history/test_dbn.cpp)
target_include_directories(test_dbn PRIVATE src/)
target_link_libraries(test_dbn PRIVATE ${ZSTD_LIBRARY})
target_compile_features(test_dbn PRIVATE cxx_std_20)

# ── Payout simulator (paper trade history × prop funded-account payout rules) ───
# scripts/payout_sim.sh exports the trades; model in src/paper/payout_sim.hpp.
add_executable(payout_sim src/paper/payout_sim_main.cpp)
target_include_directories(payout_sim PRIVATE src/paper/)
target_compile_features(payout_sim PRIVATE cxx_std_20)

add_executable(test_payout_sim tests/paper/test_payout_sim.cpp)
target_include_directories(test_payout_sim PRIVATE src/paper/)
target_compile_features(test_payout_sim PRIVATE cxx_std_20)

# Walk-forward parameter selection (scripts/walk_forward.sh; model in src/paper/walk_forward.hpp).
add_executable(walk_forward src/paper/walk_forward_main.cpp)
target_include_directories(walk_forward PRIVATE src/paper/)
target_compile_features(walk_forward PRIVATE cxx_std_20)

add_executable(test_walk_forward tests/paper/test_walk_forward.cpp)
target_include_directories(test_walk_forward PRIVATE src/paper/)
target_compile_features(test_walk_forward PRIVATE cxx_std_20)
add_test(NAME dbn_tests COMMAND test_dbn)

# ── Audit Daemon ───────────────────────────────────────────────────
# Standalone C++20 binary — only libpq + nlohmann/json, no Boost/Protobuf.
add_executable(audit_daemon src/audit_daemon_main.cpp)
target_include_directories(audit_daemon PRIVATE
    src/
    ${PostgreSQL_INCLUDE_DIRS}
)
target_link_libraries(audit_daemon PRIVATE
    ${PostgreSQL_LIBRARIES}
)
target_compile_features(audit_daemon PRIVATE cxx_std_20)

# ── Install ────────────────────────────────────────────────────────
install(TARGETS rithmic_engine DESTINATION bin)
install(DIRECTORY certs/ DESTINATION share/rithmic_engine/certs)

# ── edge_lab: idea → screen → walk-forward → controls → robustness → prop fit (research/README.md) ──
# Header-only (nlohmann/json); reads data/edge_lab/cache exported by scripts/edge_lab.sh. No DB link.
add_executable(edge_lab src/research/edge_lab_main.cpp)
target_compile_features(edge_lab PRIVATE cxx_std_20)
target_link_libraries(edge_lab PRIVATE pthread)

add_executable(test_edge_lab tests/research/test_edge_lab.cpp)
target_compile_features(test_edge_lab PRIVATE cxx_std_20)
target_link_libraries(test_edge_lab PRIVATE pthread)

# synthetic market with a planted edge — validates the pipeline end to end (scripts/edge_lab_validate.sh)
add_executable(synth_market tests/research/synth_market.cpp)
target_compile_features(synth_market PRIVATE cxx_std_20)

# Lighter live order gateway, OFFLINE core (src/dex/live/): signer wrapper over the pinned v1.1.6 .so (dlopen at
# runtime from its absolute path, sha256-checked, allowlisted symbols only), config/network guard, sizing, nonces,
# rate limiter + batcher, order state machine, risk limits, probe plan. Tests use a FakeExchange; no network.
add_executable(test_lighter_gateway tests/dex/test_lighter_gateway.cpp)
target_include_directories(test_lighter_gateway PRIVATE src/ tests/dex/)
target_link_libraries(test_lighter_gateway PRIVATE OpenSSL::Crypto ${CMAKE_DL_LIBS} pthread)
target_compile_features(test_lighter_gateway PRIVATE cxx_std_20)
add_test(NAME lighter_gateway_tests COMMAND test_lighter_gateway)

# Lighter live market maker (mm_live): engine + account stream + sender + reconciler + dead-man + journal on top of the
# gateway core. Modes offline (in-process FakeVenue soak, no socket) / probe / live (mainnet only). Runbook:
# docs/mm_live_runbook.md; unit deploy/mm-live@.service (NOT installed). Tests are offline (fake venue + a local TLS
# server on 127.0.0.1); run them under env GODEBUG=asyncpreemptoff=1.
add_executable(mm_live src/dex/mm_live_main.cpp)
target_include_directories(mm_live PRIVATE src/ ${Boost_INCLUDE_DIRS})
target_link_libraries(mm_live PRIVATE Boost::system OpenSSL::SSL OpenSSL::Crypto ${ZSTD_LIBRARY} ${CMAKE_DL_LIBS} pthread)
target_compile_definitions(mm_live PRIVATE BOOST_ASIO_HAS_CO_AWAIT)
target_compile_features(mm_live PRIVATE cxx_std_20)
add_executable(test_mm_live tests/dex/test_mm_live.cpp)
target_include_directories(test_mm_live PRIVATE src/ tests/dex/ ${Boost_INCLUDE_DIRS})
target_link_libraries(test_mm_live PRIVATE Boost::system OpenSSL::SSL OpenSSL::Crypto ${ZSTD_LIBRARY} ${CMAKE_DL_LIBS} pthread)
target_compile_definitions(test_mm_live PRIVATE BOOST_ASIO_HAS_CO_AWAIT)
target_compile_features(test_mm_live PRIVATE cxx_std_20)
add_dependencies(test_mm_live mm_live)   # the binary-level test runs ./mm_live next to it
add_test(NAME mm_live_tests COMMAND ${CMAKE_COMMAND} -E env GODEBUG=asyncpreemptoff=1 $<TARGET_FILE:test_mm_live>)
