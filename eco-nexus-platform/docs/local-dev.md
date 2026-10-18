# Local development (Phase 3)

Requirements: GCC 13+ (C11/C++20), CMake >= 3.25, PostgreSQL 14+, and the packages
`libpqxx-dev libasio-dev nlohmann-json3-dev libgtest-dev libssl-dev pkg-config` (Ubuntu 24.04 names).
Crow v1.3.3 is downloaded by CMake at configure time (needs internet once).

```bash
# 1. database + development-only role
sudo -u postgres psql -c "CREATE ROLE carbon_dev LOGIN CREATEDB PASSWORD 'carbon_dev_pw'" \
                      -c "CREATE DATABASE carbon OWNER carbon_dev"
# 2. configuration
cp .env.example .env && set -a && . ./.env && set +a
# 3. build, migrate, run
cmake -S . -B build && cmake --build build -j
./build/backend/carbon_migrate up
./build/backend/carbon_seed          # development-only demo accounts (refuses CARBON_ENV=production)
./build/backend/carbon_server
curl localhost:8080/api/v1/health/ready
# 4. tests (plain + AddressSanitizer/UBSan)
./scripts/test.sh
```

Startup exit codes: `2` bad configuration, `3` database unreachable, `4` schema out of date
(run `carbon_migrate up`, or set `CARBON_AUTO_MIGRATE=true` in development only), `1` other fatal error.

Database tests need `CARBON_TEST_ADMIN_URL` (a CREATEDB role). Without it they are reported as SKIPPED, not passed.

## Development accounts (created by `carbon_seed`; public password, never use anywhere real)
Password for all: `DevOnly-ChangeMe-123!`

| Email | Role |
|---|---|
| platform.admin@dev.test | platform administrator |
| admin@acme.dev.test | company admin of "Acme Cement (demo)" |
| operator@acme.dev.test | company operator (Acme) |
| consultant@dev.test | consultant (Acme and Borealis) |
| reviewer@dev.test | reviewer (Acme) |
| admin@borealis.dev.test | company admin of "Borealis Steel (demo)" (second tenant, for isolation demos) |

Try it:
```bash
curl -c jar -H 'Content-Type: application/json' -d '{"email":"admin@acme.dev.test","password":"DevOnly-ChangeMe-123!"}' localhost:8080/api/v1/auth/login
# take csrf_token and organization_id from the response, then:
curl -b jar localhost:8080/api/v1/organizations
curl -b jar -H 'Content-Type: application/json' -H "X-CSRF-Token: $CSRF" -d '{"name":"Kiln 9","sector":"cement","production_unit":"tonne"}' localhost:8080/api/v1/organizations/$ORG/facilities
```
The password-reset code is printed in the server log in development (no email transport exists).

Open http://localhost:8080 for the UI (sign in with a development account, or create a company account; passwords need 12+ characters).

New settings: `CARBON_FRONTEND_DIR`, `CARBON_ALLOW_SELF_REGISTRATION` (default true in development, false in production), `CARBON_SESSION_TTL_HOURS`, `CARBON_SESSION_IDLE_MINUTES`, `CARBON_ARGON2_PROFILE`.

## Known workaround
Ubuntu's libpqxx 7.8 is built as C++17 while this project is C++20; `backend/include/carbon/pqxx_abi_fix.hpp`
is force-included to keep the headers ABI-compatible (option `CARBON_PQXX_ABI_WORKAROUND`).
