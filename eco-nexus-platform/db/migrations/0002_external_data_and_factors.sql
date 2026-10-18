-- 0002: external data provenance and versioned emission factors.

CREATE TABLE external_data_sources (
  id                uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  source_key        text NOT NULL UNIQUE CHECK (source_key ~ '^[a-z0-9_]+$'),
  name              text NOT NULL,
  publisher         text NOT NULL,
  access_kind       text NOT NULL CHECK (access_kind IN ('documented_api','file_download','manual_import_only')),
  documentation_url text,
  endpoint_url      text,
  requires_api_key  boolean NOT NULL DEFAULT false,
  licence_note      text,
  api_verified      boolean NOT NULL DEFAULT false,
  last_verified_at  timestamptz,
  notes             text,
  created_at        timestamptz NOT NULL DEFAULT now(),
  -- An API may only be marked usable once a human has verified its documentation.
  CONSTRAINT api_requires_verification CHECK (
    access_kind <> 'documented_api' OR (endpoint_url IS NOT NULL AND api_verified AND last_verified_at IS NOT NULL))
);

CREATE TABLE imported_datasets (
  id                  uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  source_id           uuid NOT NULL REFERENCES external_data_sources(id),
  label               text NOT NULL,
  original_filename   text,
  sha256              text NOT NULL CHECK (sha256 ~ '^[0-9a-f]{64}$'),
  import_method       text NOT NULL CHECK (import_method IN ('manual_file','api')),
  is_live             boolean NOT NULL DEFAULT false,
  retrieved_at        timestamptz NOT NULL,
  dataset_version     text,
  valid_from          date,
  valid_to            date,
  row_count           integer CHECK (row_count IS NULL OR row_count >= 0),
  verification_status text NOT NULL DEFAULT 'pending' CHECK (verification_status IN ('pending','verified','rejected')),
  verified_by         uuid REFERENCES users(id),
  verified_at         timestamptz,
  imported_by         uuid REFERENCES users(id),
  notes               text,
  created_at          timestamptz NOT NULL DEFAULT now(),
  CHECK (valid_to IS NULL OR valid_from IS NULL OR valid_to >= valid_from),
  -- A manually imported file can never be presented as live data.
  CONSTRAINT manual_is_not_live CHECK (import_method <> 'manual_file' OR NOT is_live),
  CONSTRAINT verified_has_verifier CHECK (verification_status <> 'verified' OR (verified_by IS NOT NULL AND verified_at IS NOT NULL))
);
CREATE INDEX imported_datasets_source_idx ON imported_datasets (source_id, retrieved_at DESC);

CREATE TABLE emission_factor_sources (
  id                  uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  name                text NOT NULL,
  publisher           text NOT NULL,
  document_title      text,
  document_url        text,
  publication_date    date,
  valid_from          date,
  valid_to            date,
  imported_dataset_id uuid REFERENCES imported_datasets(id),
  -- 'development_fixture' marks fabricated data used only for local demos/tests.
  origin              text NOT NULL CHECK (origin IN ('official_import','development_fixture')),
  licence_note        text,
  created_at          timestamptz NOT NULL DEFAULT now(),
  CHECK (valid_to IS NULL OR valid_from IS NULL OR valid_to >= valid_from)
);

CREATE TABLE emission_factors (
  id              uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  source_id       uuid NOT NULL REFERENCES emission_factor_sources(id),
  factor_key      text NOT NULL CHECK (factor_key ~ '^[a-z0-9_.]+$'),
  version         integer NOT NULL CHECK (version >= 1),
  name            text NOT NULL,
  scope           smallint NOT NULL CHECK (scope IN (1,2)),
  category        text NOT NULL CHECK (category IN ('stationary_fuel','mobile_fuel','electricity_grid','industrial_process','other')),
  scope2_method   text CHECK (scope2_method IN ('location_based','market_based')),
  activity_unit   text NOT NULL,                       -- unit the factor is "per" (e.g. kWh, litre, tonne)
  basis           text NOT NULL CHECK (basis IN ('co2e','by_gas')),
  factor_co2e_kg  numeric(20,10) CHECK (factor_co2e_kg >= 0),   -- kg CO2e per activity_unit
  co2_kg          numeric(20,10) CHECK (co2_kg >= 0),           -- kg gas per activity_unit
  ch4_kg          numeric(20,10) CHECK (ch4_kg >= 0),
  n2o_kg          numeric(20,10) CHECK (n2o_kg >= 0),
  gwp_set         text CHECK (gwp_set IN ('AR4','AR5','AR6')),
  valid_from      date NOT NULL,
  valid_to        date,
  status          text NOT NULL DEFAULT 'draft' CHECK (status IN ('draft','active','retired')),
  supersedes_id   uuid REFERENCES emission_factors(id),
  created_at      timestamptz NOT NULL DEFAULT now(),
  activated_at    timestamptz,
  UNIQUE (factor_key, version),
  CONSTRAINT ef_basis_shape CHECK (
    (basis = 'co2e'   AND factor_co2e_kg IS NOT NULL AND co2_kg IS NULL AND ch4_kg IS NULL AND n2o_kg IS NULL AND gwp_set IS NULL)
 OR (basis = 'by_gas' AND factor_co2e_kg IS NULL AND co2_kg IS NOT NULL AND gwp_set IS NOT NULL)),
  CONSTRAINT ef_scope2_method CHECK ((scope = 2) = (scope2_method IS NOT NULL)),
  CONSTRAINT ef_validity CHECK (valid_to IS NULL OR valid_to >= valid_from),
  CONSTRAINT ef_active_has_timestamp CHECK (status = 'draft' OR activated_at IS NOT NULL)
);
CREATE INDEX emission_factors_lookup_idx ON emission_factors (factor_key, status, valid_from);

-- Past results must never change silently: once a factor leaves 'draft' its numbers are frozen.
-- A change requires a NEW version row that supersedes the old one.
CREATE FUNCTION emission_factors_guard() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  IF TG_OP = 'DELETE' THEN
    IF OLD.status <> 'draft' THEN
      RAISE EXCEPTION 'emission factor %/v% is % and cannot be deleted', OLD.factor_key, OLD.version, OLD.status
        USING ERRCODE = 'integrity_constraint_violation';
    END IF;
    RETURN OLD;
  END IF;
  IF OLD.status <> 'draft' THEN
    IF ROW(NEW.source_id, NEW.factor_key, NEW.version, NEW.name, NEW.scope, NEW.category, NEW.scope2_method,
           NEW.activity_unit, NEW.basis, NEW.factor_co2e_kg, NEW.co2_kg, NEW.ch4_kg, NEW.n2o_kg, NEW.gwp_set, NEW.valid_from)
       IS DISTINCT FROM
       ROW(OLD.source_id, OLD.factor_key, OLD.version, OLD.name, OLD.scope, OLD.category, OLD.scope2_method,
           OLD.activity_unit, OLD.basis, OLD.factor_co2e_kg, OLD.co2_kg, OLD.ch4_kg, OLD.n2o_kg, OLD.gwp_set, OLD.valid_from) THEN
      RAISE EXCEPTION 'emission factor %/v% is immutable once %; create a new version', OLD.factor_key, OLD.version, OLD.status
        USING ERRCODE = 'integrity_constraint_violation';
    END IF;
    IF NEW.status = 'draft' OR (OLD.status = 'retired' AND NEW.status <> 'retired') THEN
      RAISE EXCEPTION 'emission factor %/v% cannot move from % back to %', OLD.factor_key, OLD.version, OLD.status, NEW.status
        USING ERRCODE = 'integrity_constraint_violation';
    END IF;
  END IF;
  RETURN NEW;
END $$;
CREATE TRIGGER emission_factors_immutable BEFORE UPDATE OR DELETE ON emission_factors
  FOR EACH ROW EXECUTE FUNCTION emission_factors_guard();
