-- 012_trade_context_features.sql — multi-timeframe feature vector + validation
-- flags per trade (built by the dashboard's ui/services/features.py with no
-- lookahead: every bar used closed before the entry). Idempotent.
ALTER TABLE trade_context ADD COLUMN IF NOT EXISTS features       JSONB;
ALTER TABLE trade_context ADD COLUMN IF NOT EXISTS features_ok    BOOLEAN;
ALTER TABLE trade_context ADD COLUMN IF NOT EXISTS feature_issues TEXT;
ALTER TABLE trade_context ADD COLUMN IF NOT EXISTS coverage_ok    BOOLEAN;   -- trading day passed data-quality
