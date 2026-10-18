# Where data entered in the frontend is stored

| What the user enters | Where it goes | Persisted? |
|---|---|---|
| Register: name, email, password, company name, sector | `POST /api/v1/auth/register` -> PostgreSQL: `users` (password only as an Argon2id hash), `organizations`, `organization_memberships` (role `company_admin`) | Yes |
| Login | Server creates a row in `sessions` (only the SHA-256 hash of the token). Browser holds an HttpOnly cookie it cannot read; the CSRF token lives in JS memory only | Yes (server side) |
| Profile: company name, sector, state, GSTIN | `PATCH /api/v1/organizations/{id}` -> PostgreSQL `organizations` (company admins only; GSTIN must be 15 upper-case letters/digits) | Yes |
| Every create/update above | Append-only row in `audit_events` (same transaction) | Yes |
| Profile numbers: credits held, price, target intensity, production, emissions | **Browser `localStorage`**, key `eco-nexus:profile:<user id>` | Only in that browser |
| Calculator, Compliance, Marketplace, Investment inputs and results | Browser memory only (JS variables) | No: lost on reload |
| Dashboard and consultancy tables | Hard-coded demo data in `frontend/js/main.js` | No |

Facilities exist in the database (`facilities` table, API under `/organizations/{id}/facilities`) but the UI has no facility screen yet.
Tables for activity data, calculations, compliance, evidence, simulated trading and investment exist in the schema (migrations 0002-0006) with no API or UI yet; those arrive in Phases 4-9.

Inspect it yourself:
```
psql "$CARBON_DATABASE_URL" -c "select name, sector, state, gstin from organizations"
```
