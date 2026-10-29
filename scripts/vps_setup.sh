#!/usr/bin/env bash
# vps_setup.sh — bootstrap a fresh Ubuntu 24.04 VPS as a market-data recorder for one venue (founder 2026-10-25:
# one small server per venue, in that venue's region, set up from git identically everywhere).
#
#   curl -fsSL https://raw.githubusercontent.com/<owner>/rithmic_engine/main/scripts/vps_setup.sh | bash -s -- lighter
#   # or, from a clone:  scripts/vps_setup.sh lighter|hyperliquid [SYMBOLS]
#
# What it does (idempotent): apt toolchain + chrony, clone/pull the repo to ~/rithmic_engine, build the recorders and
# mm_sim in build/, create /data/rithmic_dex/<venue> (the archive root; the recorder refuses to start if it is
# missing), install a systemd USER unit `dex-recorder-<venue>` with the venue's default symbols, enable linger so
# it survives logout, start it, and print the receive-lag check command. No keys, no orders, nothing listens.
#
# Venues: lighter (build/lighter_recorder), hyperliquid (build/hl_recorder), extended | aster | paradex
# (build/dex_recorder --venue …, src/dex/venue_feeds.hpp). Default symbol lists = the coins each venue lists of the
# Lighter research set (+ majors); the binaries skip anything the venue does not list.
set -euo pipefail
VENUE=${1:?venue: lighter|hyperliquid|extended|aster|paradex}
SYMS=${2:-}
REPO_URL=${REPO_URL:-git@github.com:Neoptolemospapadiofantous/rithmic_data_engine.git}   # private repo: give the VPS a read-only deploy key first
BRANCH=${BRANCH:-main}
ROOT=/data/rithmic_dex
case "$VENUE" in
  lighter)     BIN=lighter_recorder; FLAG=--symbols
               DEF="AAVE,BNB,ONDO,AVAX,DOT,XLM,ASTER,TRUMP,FIL,OP,CC,POL,WIF,EIGEN,BERA,AZTEC,APEX,UNI,CRV,PYTH,ETHFI,SEI,2Z,GMX,HYPE,NEAR,ENA,WLD,SKR,PRL,ARB,SUI,ZRO,JUP,TAO,LTC,XMR,PUMP,FARTCOIN";;
  hyperliquid) BIN=hl_recorder; FLAG=--coins
               DEF="kPEPE,AAVE,BNB,ONDO,AVAX,DOT,XLM,ASTER,TRUMP,FIL,RENDER,REZ,SOPH,OP,CC,POL,WIF,EIGEN,BERA,BLUR,MERL,AZTEC,APE,ME,MELANIA,SNX,IMX,APEX,RSR,FOGO";;
  extended)    BIN=dex_recorder; FLAG="--venue extended --symbols"
               DEF="AAVE,BNB,ONDO,AVAX,DOT,XLM,ASTER,FIL,EIGEN,AZTEC,UNI,CRV,ETHFI,SEI,HYPE,NEAR,ENA,WLD,ARB,SUI,ZRO,JUP,TAO,LTC,XMR,PUMP,FARTCOIN,LINK,DOGE,ADA,XRP,SOL";;
  aster)       BIN=dex_recorder; FLAG="--venue aster --symbols"
               DEF="AAVE,BNB,ONDO,AVAX,DOT,XLM,ASTER,TRUMP,FIL,OP,CC,POL,WIF,EIGEN,BERA,AZTEC,UNI,CRV,PYTH,ETHFI,SEI,2Z,HYPE,NEAR,ENA,WLD,SKR,PRL,ARB,SUI,ZRO,JUP,TAO,LTC,XMR,PUMP,FARTCOIN";;
  paradex)     BIN=dex_recorder; FLAG="--venue paradex --symbols"
               DEF="AAVE,BNB,ONDO,AVAX,CC,ENA,ETHFI,HYPE,JUP,LTC,NEAR,PUMP,PYTH,SUI,TAO,TRUMP,UNI,XMR,LINK,DOGE,ADA,XRP,SOL";;
  *) echo "unknown venue $VENUE"; exit 2;;
esac
SYMS=${SYMS:-$DEF}

echo "== packages"
sudo apt-get update -qq
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq git cmake g++ pkg-config libboost-all-dev libssl-dev libpq-dev \
  libzstd-dev zlib1g-dev nlohmann-json3-dev libprotobuf-dev protobuf-compiler chrony jq zstd >/dev/null
sudo systemctl enable --now chrony >/dev/null 2>&1 || true

echo "== repo"
if [ -d ~/rithmic_engine/.git ]; then git -C ~/rithmic_engine pull --ff-only; else git clone --depth 50 -b "$BRANCH" "$REPO_URL" ~/rithmic_engine; fi
cd ~/rithmic_engine
echo "commit $(git rev-parse --short HEAD)"

echo "== build (recorders + mm_sim + tests only)"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build -j"$(nproc)" --target "$BIN" mm_sim test_hl_mm test_lighter test_venues 2>&1 | grep -E "error|Built target" || true
build/test_hl_mm | tail -1; build/test_lighter | tail -1; build/test_venues | tail -1

echo "== archive root $ROOT/$VENUE"
sudo mkdir -p "$ROOT/$VENUE" && sudo chown -R "$USER":"$USER" "$ROOT"
mkdir -p data/logs ~/.config/systemd/user

[ "$VENUE" = extended ] && MEMMAX=768M
echo "== unit dex-recorder-$VENUE"
cat > ~/.config/systemd/user/dex-recorder-"$VENUE".service <<EOF
[Unit]
Description=$VENUE book/trade recorder (market-making research), 24/7
After=network-online.target
Wants=network-online.target
StartLimitIntervalSec=0

[Service]
Type=simple
WorkingDirectory=$HOME/rithmic_engine
ExecStart=$HOME/rithmic_engine/build/$BIN --out $ROOT/$VENUE $FLAG $SYMS
StandardOutput=append:$HOME/rithmic_engine/data/logs/${BIN}.log
StandardError=append:$HOME/rithmic_engine/data/logs/${BIN}.log
Restart=always
RestartSec=30
MemoryMax=${MEMMAX:-512M}

[Install]
WantedBy=default.target
EOF
sudo loginctl enable-linger "$USER" >/dev/null 2>&1 || true
systemctl --user daemon-reload
systemctl --user enable --now dex-recorder-"$VENUE"
sleep 5; systemctl --user is-active dex-recorder-"$VENUE"
echo
echo "done. Receive-lag check after a few minutes:  scripts/lag_check.sh $ROOT/$VENUE"
echo "Archive sync to the home box (cron, from the home box):  rsync -az $(hostname):$ROOT/$VENUE/ /media/theone/381C48C51C48803C/rithmic_dex/${VENUE}_vps/"
