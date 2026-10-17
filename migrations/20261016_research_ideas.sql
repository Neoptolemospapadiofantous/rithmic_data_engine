-- edge_lab results (scripts/edge_lab.sh load): one row per idea (latest run) + every run kept.
CREATE TABLE IF NOT EXISTS research_ideas (
    id            text PRIMARY KEY,
    name          text NOT NULL,
    why           text NOT NULL,
    verdict       text NOT NULL,          -- PASS | FAIL at <gate> | QUICK-LOOK
    stage_reached int  NOT NULL,          -- 0..4 = first failed gate, 5 = passed all
    metrics       jsonb NOT NULL,
    spec          jsonb NOT NULL,
    last_run      timestamptz NOT NULL,
    runs          int NOT NULL DEFAULT 1
);
CREATE TABLE IF NOT EXISTS research_runs (
    id            bigserial PRIMARY KEY,
    idea_id       text NOT NULL,
    run_at        timestamptz NOT NULL,
    verdict       text NOT NULL,
    stage_reached int  NOT NULL,
    quick         boolean NOT NULL,
    metrics       jsonb NOT NULL,
    spec          jsonb NOT NULL,
    UNIQUE (idea_id, run_at)
);
