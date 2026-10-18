# Database schema (migrations 0001-0008)

26 application tables (the 24 required + `sessions`, `password_reset_tokens`) + `schema_migrations`. Applied by `carbon_migrate`; applied files are checksummed and may not be edited.

| Migration | Tables |
|---|---|
| 0001 | users, organizations, organization_memberships, facilities |
| 0002 | external_data_sources, imported_datasets, emission_factor_sources, emission_factors |
| 0003 | production_records, activity_records, calculation_runs, calculation_inputs |
| 0004 | regulatory_rule_versions, regulatory_targets, compliance_assessments |
| 0005 | evidence_files, consultancy_reviews, investment_projects, investment_scenarios |
| 0006 | simulated_accounts, simulated_credit_inventory, simulated_orders, simulated_trades |
| 0007 | audit_events |
| 0008 | sessions, password_reset_tokens (only SHA-256 of tokens is stored) |

## Guarantees enforced by the database itself (each covered by `tests/test_schema.cpp`)
- **Tenant isolation:** tenant-owned tables use composite FKs `(organization_id, x_id)`, so a row can never point at another organization's facility, run or activity.
- **Passwords:** `users.password_hash` must be an Argon2id PHC string; plaintext is rejected.
- **History cannot change silently:** emission factors freeze once they leave `draft` (changes need a new version); finalized calculation runs and their inputs are immutable (only workflow status may move); audit events are append-only (also blocks TRUNCATE).
- **Honest status labels:** `compliance_assessments.legal_status` can only be `estimate_only`; manually imported datasets cannot be `is_live`; an external source cannot be marked a `documented_api` until verified.
- **Simulation safety:** reserved cash/credits can never exceed balance/quantity (no double spend/oversell); instrument is `SIM_CCC` only; trades are immutable, never self-trades, must pair a buy with a sell inside both limits.
- **Evidence:** no path components in filenames, 25 MiB cap, content-type allow-list.

## Not enforced in the database (done in the application, later phases)
Role checks, unit conversion/calculation, reviewer-is-not-author rule, Row-Level Security (considered; composite FKs chosen first because they need no per-request session settings).
