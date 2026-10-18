-- 0005: evidence metadata, consultant reviews, investment models.

CREATE TABLE evidence_files (
  id                 uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id    uuid NOT NULL REFERENCES organizations(id),
  facility_id        uuid,
  calculation_run_id uuid,
  activity_record_id uuid,
  original_filename  text NOT NULL CHECK (original_filename ~ '^[^/\\]{1,255}$'),   -- no path components
  storage_key        text NOT NULL UNIQUE CHECK (storage_key ~ '^[A-Za-z0-9._-]{1,200}$'),
  content_type       text NOT NULL CHECK (content_type IN ('application/pdf','image/png','image/jpeg','text/csv',
                       'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet')),
  size_bytes         bigint NOT NULL CHECK (size_bytes > 0 AND size_bytes <= 26214400),   -- 25 MiB hard cap
  sha256             text NOT NULL CHECK (sha256 ~ '^[0-9a-f]{64}$'),
  review_status      text NOT NULL DEFAULT 'pending_review' CHECK (review_status IN ('pending_review','accepted','rejected')),
  uploaded_by        uuid NOT NULL REFERENCES users(id),
  created_at         timestamptz NOT NULL DEFAULT now(),
  FOREIGN KEY (organization_id, facility_id)        REFERENCES facilities (organization_id, id),
  FOREIGN KEY (organization_id, calculation_run_id) REFERENCES calculation_runs (organization_id, id),
  FOREIGN KEY (organization_id, activity_record_id) REFERENCES activity_records (organization_id, id)
);
CREATE INDEX evidence_files_org_idx ON evidence_files (organization_id, created_at DESC);
CREATE INDEX evidence_files_run_idx ON evidence_files (calculation_run_id);

CREATE TABLE consultancy_reviews (
  id                 uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id    uuid NOT NULL REFERENCES organizations(id),
  calculation_run_id uuid NOT NULL,
  reviewer_id        uuid NOT NULL REFERENCES users(id),
  assigned_by        uuid NOT NULL REFERENCES users(id),
  status             text NOT NULL DEFAULT 'assigned'
                       CHECK (status IN ('assigned','in_review','changes_requested','approved','rejected')),
  comment            text,
  assigned_at        timestamptz NOT NULL DEFAULT now(),
  decided_at         timestamptz,
  CONSTRAINT decided_matches_status CHECK ((status IN ('approved','rejected','changes_requested')) = (decided_at IS NOT NULL)),
  FOREIGN KEY (organization_id, calculation_run_id) REFERENCES calculation_runs (organization_id, id)
);
CREATE UNIQUE INDEX consultancy_reviews_one_active ON consultancy_reviews (calculation_run_id, reviewer_id)
  WHERE status IN ('assigned','in_review');
CREATE INDEX consultancy_reviews_reviewer_idx ON consultancy_reviews (reviewer_id, status);

CREATE TABLE investment_projects (
  id              uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id uuid NOT NULL REFERENCES organizations(id),
  facility_id     uuid,
  name            text NOT NULL CHECK (length(btrim(name)) > 0),
  project_type    text NOT NULL CHECK (project_type IN
                    ('rooftop_solar','ground_solar','wind','energy_efficiency','waste_heat_recovery')),
  description     text,
  created_by      uuid NOT NULL REFERENCES users(id),
  created_at      timestamptz NOT NULL DEFAULT now(),
  updated_at      timestamptz NOT NULL DEFAULT now(),
  UNIQUE (organization_id, name),
  UNIQUE (organization_id, id),
  FOREIGN KEY (organization_id, facility_id) REFERENCES facilities (organization_id, id)
);
CREATE TRIGGER investment_projects_updated BEFORE UPDATE ON investment_projects FOR EACH ROW EXECUTE FUNCTION set_updated_at();

CREATE TABLE investment_scenarios (
  id              uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id uuid NOT NULL REFERENCES organizations(id),
  project_id      uuid NOT NULL,
  name            text NOT NULL CHECK (length(btrim(name)) > 0),
  assumptions     jsonb NOT NULL CHECK (jsonb_typeof(assumptions) = 'object'),
  results         jsonb CHECK (results IS NULL OR jsonb_typeof(results) = 'object'),
  model_version   text NOT NULL,
  created_by      uuid NOT NULL REFERENCES users(id),
  created_at      timestamptz NOT NULL DEFAULT now(),
  FOREIGN KEY (organization_id, project_id) REFERENCES investment_projects (organization_id, id)
);
CREATE INDEX investment_scenarios_project_idx ON investment_scenarios (project_id, created_at DESC);
