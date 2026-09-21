#!/usr/bin/env bash
# Start the tick collector (build/rithmic_engine) on the DEDICATED market-data
# login instead of the active-env aliases.
#
# Why: the collector reads RITHMIC_AMP_* for its TICKER_PLANT login, and
# scripts/use-env.sh points those aliases at the active broker (Tradeify). A
# Tradeify login supports the executor's MD + ORDER_PLANT pair only — a third
# session (this collector) gets the live executor's MD force-logged-out every
# ~35s (verified 2026-09-21). So the collector logs in with MD_LEGENDS_*
# (a separate Rithmic user on "Rithmic 01") and leaves the Tradeify login to
# the executor. Config::load_dotenv never overrides variables already in the
# environment, so exporting them here is enough.
#
# Usage: scripts/collector.sh            (foreground; nohup/setsid it yourself)
#        scripts/collector.sh --status   (any flag is passed through)
set -euo pipefail
cd "$(dirname "$0")/.."
[ -f .env ] || { echo "collector.sh: .env missing" >&2; exit 1; }
[ -x build/rithmic_engine ] || { echo "collector.sh: build/rithmic_engine missing — cmake --build build --target rithmic_engine" >&2; exit 1; }

# Load .env literally (values may hold shell metacharacters — never `source` it).
while IFS= read -r line || [ -n "$line" ]; do
  line="${line#"${line%%[![:space:]]*}"}"
  case "$line" in ''|'#'*) continue;; esac
  case "$line" in *=*) ;; *) continue;; esac
  k=${line%%=*}; v=${line#*=}
  case "$k" in ''|*[!A-Za-z0-9_]*) continue;; esac
  v="${v%"${v##*[![:space:]]}"}"
  case "$v" in
    \"*\") v=${v#\"}; v=${v%\"};;
    \'*\') v=${v#\'}; v=${v%\'};;
  esac
  case "$k" in MD_LEGENDS_USER|MD_LEGENDS_PASSWORD|MD_LEGENDS_SYSTEM|MD_LEGENDS_URL) printf -v "$k" '%s' "$v";;
  esac
done < .env

: "${MD_LEGENDS_USER:?collector.sh: MD_LEGENDS_USER not set in .env}"
: "${MD_LEGENDS_PASSWORD:?collector.sh: MD_LEGENDS_PASSWORD not set in .env}"
export RITHMIC_AMP_USER="$MD_LEGENDS_USER"
export RITHMIC_AMP_PASSWORD="$MD_LEGENDS_PASSWORD"
export RITHMIC_AMP_SYSTEM="${MD_LEGENDS_SYSTEM:-Rithmic 01}"
export RITHMIC_AMP_URL="${MD_LEGENDS_URL:-wss://rprotocol-mobile.rithmic.com:443}"
echo "[collector.sh] MD login user=${RITHMIC_AMP_USER} system=${RITHMIC_AMP_SYSTEM}" >&2
exec build/rithmic_engine "$@"
