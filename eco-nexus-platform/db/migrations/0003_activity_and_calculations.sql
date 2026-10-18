-- 0003: activity/production data and calculation runs with full input snapshots.

CREATE TABLE production_records (
  id              uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id uuid NOT NULL REFERENCES organizations(id),
  facility_id     uuid NOT NULL,
  period_start    date NOT NULL,
  period_end      date NOT NULL,
  product_name    text NOT NULL DEFAULT '',
  quantity        numeric(24,6) NOT NULL CHECK (quantity >= 0),
  unit            text NOT NULL CHECK (length(btrim(unit)) > 0),
  notes           text,
  created_by      uuid NOT NULL REFERENCES users(id),
  created_at      timestamptz NOT NULL DEFAULT now(),
  CHECK (period_end >= period_start),
  FOREIGN KEY (organization_id, facility_id) REFERENCES facilities (organization_id, id),
  UNIQUE (facility_id, period_start, period_end, product_name, unit)
);

CREATE TABLE activity_records (
  id              uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id uuid NOT NULL REFERENCES organizations(id),
  facility_id     uuid NOT NULL,
  period_start    date NOT NULL,
  period_end      date NOT NULL,
  source_type     text NOT NULL CHECK (source_type IN
                    ('stationary_combustion','mobile_combustion','purchased_electricity','industrial_process','other')),
  activity_key    text NOT NULL CHECK (activity_key ~ '^[a-z0-9_.]+$'),   -- e.g. fuel.diesel (maps to a factor_key)
  quantity        numeric(24,8) NOT NULL CHECK (quantity >= 0),            -- a missing reading is never stored as 0
  unit            text NOT NULL CHECK (length(btrim(unit)) > 0),
  data_quality    text NOT NULL CHECK (data_quality IN ('measured','invoiced','estimated','default')),
  dedupe_hash     text NOT NULL CHECK (dedupe_hash ~ '^[0-9a-f]{64}$'),
  import_batch_id uuid,                                                    -- FK added with the CSV-import tables
  notes           text,
  created_by      uuid NOT NULL REFERENCES users(id),
  created_at      timestamptz NOT NULL DEFAULT now(),
  voided_at       timestamptz,                                             -- soft delete; history is kept
  voided_by       uuid REFERENCES users(id),
  CHECK (period_end >= period_start),
  CHECK ((voided_at IS NULL) = (voided_by IS NULL)),
  FOREIGN KEY (organization_id, facility_id) REFERENCES facilities (organization_id, id),
  UNIQUE (organization_id, id)
);
-- Duplicate detection for manual entry and CSV import (only among non-voided rows).
CREATE UNIQUE INDEX activity_records_dedupe_key ON activity_records (organization_id, dedupe_hash) WHERE voided_at IS NULL;
CREATE INDEX activity_records_facility_period_idx ON activity_records (facility_id, period_start, period_end);

CREATE TABLE calculation_runs (
  id                     uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id        uuid NOT NULL REFERENCES organizations(id),
  facility_id            uuid NOT NULL,
  period_start           date NOT NULL,
  period_end             date NOT NULL,
  method_version         text NOT NULL CHECK (length(btrim(method_version)) > 0),
  status                 text NOT NULL DEFAULT 'draft'
                           CHECK (status IN ('draft','submitted','under_review','approved','rejected','superseded')),
  scope1_tco2e           numeric(24,6) CHECK (scope1_tco2e >= 0),
  scope2_location_tco2e  numeric(24,6) CHECK (scope2_location_tco2e >= 0),
  scope2_market_tco2e    numeric(24,6) CHECK (scope2_market_tco2e >= 0),   -- NULL = not calculated (never silently 0)
  total_tco2e            numeric(24,6) CHECK (total_tco2e >= 0),
  production_quantity    numeric(24,6),
  production_unit        text,
  intensity              numeric(24,10) CHECK (intensity >= 0),
  intensity_unit         text,
  data_quality_flags     jsonb NOT NULL DEFAULT '[]' CHECK (jsonb_typeof(data_quality_flags) = 'array'),
  finalized_at           timestamptz,
  created_by             uuid NOT NULL REFERENCES users(id),
  approved_by            uuid REFERENCES users(id),
  approved_at            timestamptz,
  created_at             timestamptz NOT NULL DEFAULT now(),
  updated_at             timestamptz NOT NULL DEFAULT now(),
  CHECK (period_end >= period_start),
  CHECK (intensity IS NULL OR production_quantity > 0),
  CONSTRAINT finalized_has_results CHECK (
    finalized_at IS NULL OR (scope1_tco2e IS NOT NULL AND scope2_location_tco2e IS NOT NULL AND total_tco2e IS NOT NULL)),
  CONSTRAINT approved_is_finalized CHECK (status <> 'approved' OR (finalized_at IS NOT NULL AND approved_by IS NOT NULL AND approved_at IS NOT NULL)),
  FOREIGN KEY (organization_id, facility_id) REFERENCES facilities (organization_id, id),
  UNIQUE (organization_id, id)
);
CREATE INDEX calculation_runs_facility_period_idx ON calculation_runs (facility_id, period_start, period_end);
CREATE TRIGGER calculation_runs_updated BEFORE UPDATE ON calculation_runs FOR EACH ROW EXECUTE FUNCTION set_updated_at();

-- A finalized run keeps its numbers forever; only workflow fields (status, approval) may change.
CREATE FUNCTION calculation_runs_guard() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  IF TG_OP = 'DELETE' THEN
    IF OLD.finalized_at IS NOT NULL THEN
      RAISE EXCEPTION 'finalized calculation run % cannot be deleted', OLD.id USING ERRCODE = 'integrity_constraint_violation';
    END IF;
    RETURN OLD;
  END IF;
  IF OLD.finalized_at IS NOT NULL THEN
    IF ROW(NEW.organization_id, NEW.facility_id, NEW.period_start, NEW.period_end, NEW.method_version,
           NEW.scope1_tco2e, NEW.scope2_location_tco2e, NEW.scope2_market_tco2e, NEW.total_tco2e,
           NEW.production_quantity, NEW.production_unit, NEW.intensity, NEW.intensity_unit,
           NEW.data_quality_flags, NEW.finalized_at, NEW.created_by)
       IS DISTINCT FROM
       ROW(OLD.organization_id, OLD.facility_id, OLD.period_start, OLD.period_end, OLD.method_version,
           OLD.scope1_tco2e, OLD.scope2_location_tco2e, OLD.scope2_market_tco2e, OLD.total_tco2e,
           OLD.production_quantity, OLD.production_unit, OLD.intensity, OLD.intensity_unit,
           OLD.data_quality_flags, OLD.finalized_at, OLD.created_by) THEN
      RAISE EXCEPTION 'finalized calculation run % is immutable; create a new run', OLD.id USING ERRCODE = 'integrity_constraint_violation';
    END IF;
  END IF;
  RETURN NEW;
END $$;
CREATE TRIGGER calculation_runs_immutable BEFORE UPDATE OR DELETE ON calculation_runs
  FOR EACH ROW EXECUTE FUNCTION calculation_runs_guard();

-- Snapshot of every input used by a run: original value, unit conversion, factor id+version+values.
CREATE TABLE calculation_inputs (
  id                       uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id          uuid NOT NULL,
  calculation_run_id       uuid NOT NULL,
  activity_record_id       uuid,
  source_type              text NOT NULL,
  activity_key             text NOT NULL,
  original_quantity        numeric(24,8) NOT NULL CHECK (original_quantity >= 0),
  original_unit            text NOT NULL,
  conversion_factor        numeric(24,12) NOT NULL CHECK (conversion_factor > 0),
  converted_quantity       numeric(24,8) NOT NULL CHECK (converted_quantity >= 0),
  converted_unit           text NOT NULL,
  emission_factor_id       uuid NOT NULL REFERENCES emission_factors(id),
  emission_factor_version  integer NOT NULL CHECK (emission_factor_version >= 1),
  factor_snapshot          jsonb NOT NULL CHECK (jsonb_typeof(factor_snapshot) = 'object'),
  source_reference         text NOT NULL,
  scope                    smallint NOT NULL CHECK (scope IN (1,2)),
  scope2_method            text CHECK (scope2_method IN ('location_based','market_based')),
  gwp_set                  text,
  emissions_tco2e          numeric(24,6) NOT NULL CHECK (emissions_tco2e >= 0),
  created_at               timestamptz NOT NULL DEFAULT now(),
  FOREIGN KEY (organization_id, calculation_run_id) REFERENCES calculation_runs (organization_id, id),
  FOREIGN KEY (organization_id, activity_record_id) REFERENCES activity_records (organization_id, id)
);
CREATE INDEX calculation_inputs_run_idx ON calculation_inputs (calculation_run_id);

CREATE FUNCTION calculation_inputs_guard() RETURNS trigger LANGUAGE plpgsql AS $$
DECLARE run_id uuid; fin timestamptz;
BEGIN
  run_id := CASE WHEN TG_OP = 'DELETE' THEN OLD.calculation_run_id ELSE NEW.calculation_run_id END;
  SELECT finalized_at INTO fin FROM calculation_runs WHERE id = run_id;
  IF fin IS NOT NULL THEN
    RAISE EXCEPTION 'inputs of finalized calculation run % are immutable', run_id USING ERRCODE = 'integrity_constraint_violation';
  END IF;
  RETURN CASE WHEN TG_OP = 'DELETE' THEN OLD ELSE NEW END;
END $$;
CREATE TRIGGER calculation_inputs_immutable BEFORE INSERT OR UPDATE OR DELETE ON calculation_inputs
  FOR EACH ROW EXECUTE FUNCTION calculation_inputs_guard();
