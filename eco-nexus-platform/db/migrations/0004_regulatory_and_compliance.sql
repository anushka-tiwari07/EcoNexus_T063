-- 0004: versioned regulatory rules/targets and estimated (never legal) compliance assessments.

CREATE TABLE regulatory_rule_versions (
  id                  uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  version_label       text NOT NULL UNIQUE,
  title               text NOT NULL,
  issuing_body        text,
  source_document     text,
  source_url          text,
  notification_date   date,
  effective_from      date,
  effective_to        date,
  verification_status text NOT NULL DEFAULT 'unverified' CHECK (verification_status IN ('unverified','verified','superseded')),
  verification_note   text,
  verified_by         uuid REFERENCES users(id),
  verified_at         timestamptz,
  origin              text NOT NULL CHECK (origin IN ('official_import','development_fixture')),
  created_at          timestamptz NOT NULL DEFAULT now(),
  CHECK (effective_to IS NULL OR effective_from IS NULL OR effective_to >= effective_from),
  CONSTRAINT verified_has_verifier CHECK (verification_status <> 'verified' OR (verified_by IS NOT NULL AND verified_at IS NOT NULL))
);

CREATE TABLE regulatory_targets (
  id                      uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  rule_version_id         uuid NOT NULL REFERENCES regulatory_rule_versions(id),
  sector                  text NOT NULL,
  target_year             integer NOT NULL CHECK (target_year BETWEEN 2020 AND 2100),
  intensity_target        numeric(20,8) NOT NULL CHECK (intensity_target >= 0),   -- tCO2e per production_unit
  production_unit         text NOT NULL,
  boundary_description    text,
  applicability_note      text,
  source_reference        text,
  -- UI must warn unless ALL of these are true AND the rule version is verified.
  methodology_verified    boolean NOT NULL DEFAULT false,
  boundary_verified       boolean NOT NULL DEFAULT false,
  applicability_verified  boolean NOT NULL DEFAULT false,
  created_at              timestamptz NOT NULL DEFAULT now(),
  UNIQUE (rule_version_id, sector, target_year)
);

CREATE TABLE compliance_assessments (
  id                          uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id             uuid NOT NULL REFERENCES organizations(id),
  facility_id                 uuid NOT NULL,
  calculation_run_id          uuid NOT NULL,
  regulatory_target_id        uuid NOT NULL REFERENCES regulatory_targets(id),
  eligible_production         numeric(24,6) NOT NULL CHECK (eligible_production > 0),
  regulated_emissions_tco2e   numeric(24,6) NOT NULL CHECK (regulated_emissions_tco2e >= 0),
  benchmark_emissions_tco2e   numeric(24,6) NOT NULL CHECK (benchmark_emissions_tco2e >= 0),
  shortfall_tco2e             numeric(24,6) NOT NULL CHECK (shortfall_tco2e >= 0),
  surplus_tco2e               numeric(24,6) NOT NULL CHECK (surplus_tco2e >= 0),
  assumed_credit_price        numeric(20,4) CHECK (assumed_credit_price >= 0),
  price_source                text,
  price_valid_on              date,
  transaction_fees            numeric(20,4) NOT NULL DEFAULT 0 CHECK (transaction_fees >= 0),
  estimated_purchase_cost     numeric(24,4) CHECK (estimated_purchase_cost >= 0),
  abatement_scenarios         jsonb NOT NULL DEFAULT '[]' CHECK (jsonb_typeof(abatement_scenarios) = 'array'),
  assumptions                 jsonb NOT NULL DEFAULT '[]' CHECK (jsonb_typeof(assumptions) = 'array'),
  unresolved_questions        jsonb NOT NULL DEFAULT '[]' CHECK (jsonb_typeof(unresolved_questions) = 'array'),
  -- The only legal status this system can assign is "estimate". Verified compliance is outside its scope.
  legal_status                text NOT NULL DEFAULT 'estimate_only' CHECK (legal_status = 'estimate_only'),
  created_by                  uuid NOT NULL REFERENCES users(id),
  created_at                  timestamptz NOT NULL DEFAULT now(),
  CHECK (shortfall_tco2e = 0 OR surplus_tco2e = 0),
  CHECK (estimated_purchase_cost IS NULL OR (assumed_credit_price IS NOT NULL AND price_source IS NOT NULL AND price_valid_on IS NOT NULL)),
  FOREIGN KEY (organization_id, facility_id) REFERENCES facilities (organization_id, id),
  FOREIGN KEY (organization_id, calculation_run_id) REFERENCES calculation_runs (organization_id, id)
);
CREATE INDEX compliance_assessments_run_idx ON compliance_assessments (calculation_run_id);
CREATE INDEX compliance_assessments_org_idx ON compliance_assessments (organization_id, created_at DESC);
