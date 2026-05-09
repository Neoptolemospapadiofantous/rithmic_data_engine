#!/usr/bin/env bash
# Generate a trading session post-mortem note from PostgreSQL trade data.
# Usage: ./scripts/obsidian_session.sh [YYYY-MM-DD]
set -euo pipefail

VAULT="$HOME/obsidian/rithmic"
DATE="${1:-$(date -u +"%Y-%m-%d")}"
NOTE="$VAULT/sessions/${DATE}.md"

source "$(dirname "$0")/../.env" 2>/dev/null || true

# Pull session data from PostgreSQL
SESSION=$(python3 -c "
import os, sys
try:
  import psycopg2
  conn = psycopg2.connect(
    host=os.environ.get('PG_HOST','localhost'),
    port=os.environ.get('PG_PORT','5432'),
    dbname=os.environ.get('PG_DB','rithmic'),
    user=os.environ.get('PG_USER','rithmic_user'),
    password=os.environ.get('PG_PASSWORD',''),
    connect_timeout=5
  )
  cur = conn.cursor()

  # Trades for the day
  cur.execute('''
    SELECT
      open_time::time,
      direction,
      qty,
      entry_price,
      exit_price,
      pnl_gross,
      close_reason
    FROM trades
    WHERE DATE(open_time) = %s
    ORDER BY open_time
  ''', ('$DATE',))
  trades = cur.fetchall()

  # Aggregate
  total_pnl = sum(float(r[5] or 0) for r in trades)
  total_trades = len(trades)
  outcomes = [r[6] for r in trades if r[6]]

  print(f'TRADES={total_trades}')
  print(f'PNL={total_pnl:.2f}')
  for r in trades:
    t = str(r[0])[:5] if r[0] else '?'
    side = r[1] or '?'
    qty  = r[2] or 1
    entry = r[3] or 0
    exit_ = r[4] or 0
    pnl  = f'{float(r[5] or 0):.2f}' if r[5] else '0.00'
    reason = r[6] or '?'
    print(f'ROW={t}|{side}|{qty}|{entry}|{exit_}|{pnl}|{reason}')

  conn.close()
except Exception as e:
  print(f'ERROR={e}')
" 2>/dev/null)

TRADE_COUNT=$(echo "$SESSION" | grep "^TRADES=" | cut -d= -f2 || echo 0)
PNL=$(echo "$SESSION" | grep "^PNL=" | cut -d= -f2 || echo "0.00")

# Build trade table rows
TRADE_ROWS=$(echo "$SESSION" | grep "^ROW=" | sed 's/^ROW=//' | awk -F'|' '{
  printf "| %s ET | %s | %s | %s | %s | $%s | %s |\n", $1, $2, $3, $4, $5, $6, $7
}')
if [[ -z "$TRADE_ROWS" ]]; then
  TRADE_ROWS="| — | — | — | — | — | — | no trades |"
fi

# Outcome
if (( $(echo "$PNL > 0" | bc -l 2>/dev/null || echo 0) )); then
  OUTCOME="win"
elif (( $(echo "$PNL < 0" | bc -l 2>/dev/null || echo 0) )); then
  OUTCOME="loss"
else
  OUTCOME="flat"
fi

cat > "$NOTE" << MDEOF
---
date: ${DATE}
instrument: MNQ
pnl: ${PNL}
trades: ${TRADE_COUNT}
outcome: ${OUTCOME}
tags: [session, ${OUTCOME}]
---

# Session — ${DATE}

## Summary
- **P&L**: \$${PNL}
- **Trades**: ${TRADE_COUNT}
- **Outcome**: ${OUTCOME}
- **Hermes**: [[hermes/${DATE}]]

## Execution log

| Time (ET) | Side | Qty | Entry | Exit | P&L | Reason |
|-----------|------|-----|-------|------|-----|--------|
${TRADE_ROWS}

## Active audit warnings

- ⚠️ trail_be_offset=1.0pt below breakeven (known, accepted — see [[decisions/trail-be-offset-warn]])

## Notes

MDEOF

echo "[obsidian_session] Written: $NOTE  (trades: $TRADE_COUNT, P&L: \$$PNL)"
