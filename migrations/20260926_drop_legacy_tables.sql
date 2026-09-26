-- 2026-09-26 — drop the old Python bot's Postgres mirror tables.
--
-- trade_log / daily_stats / orders / gate_results were dual-written by the retired Python
-- pipeline (last row 2026-05-11); trades / latency_log / session_summary were created by
-- 001_trades.sql / 002_schema_reconcile.sql / 20260423_session_summary_crash_safe.sql and
-- never written. Nothing in the C++ engine or the dashboard reads them from Postgres
-- (the dashboard's /api/stats/* and /api/chart/stats read the legacy SQLite file).
-- Rows were archived to data/archive/legacy_pg_20260926/*.csv before the drop.
--
-- Sorts after every other migration on purpose (the 20260423 file re-creates
-- session_summary), so a fresh `provision_oracle.sh` ends in the same state as this box.
-- loss_limits is NOT touched — it holds the live risk limits the dashboard writes.

DROP VIEW IF EXISTS v_loss_limit_status;
DROP VIEW IF EXISTS v_equity_curve;
DROP VIEW IF EXISTS v_daily_pnl;

DROP TABLE IF EXISTS trade_log;
DROP TABLE IF EXISTS daily_stats;
DROP TABLE IF EXISTS orders;
DROP TABLE IF EXISTS gate_results;
DROP TABLE IF EXISTS trades;
DROP TABLE IF EXISTS latency_log;
DROP TABLE IF EXISTS session_summary;
