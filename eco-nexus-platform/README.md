# Indian Carbon Compliance, Consultancy & Simulated Trading Platform

> Status: **Phase 3 of 11 complete, with the ECO-NEXUS frontend merged in and wired to the Phase 3 API.** Implemented: C DSA library, PostgreSQL schema + migrator, Crow server, authentication (Argon2id, sessions, CSRF, password reset), roles, organizations, memberships, facilities, audit trail. Everything else is not started.
> All trading in this project is **simulated** and is never an official CCC transaction.

## Phase status
| Phase | Scope | Status |
|---|---|---|
| 1 | Repo, CMake, C11 DSA library, test harness | **Done, tested** |
| 2 | PostgreSQL schema (24 tables), migrator, config, structured logging, Crow server, health endpoints | **Done, tested** |
| 3 | Auth (Argon2id, sessions, CSRF, reset), roles, organizations, memberships, facilities, audit, tenant isolation | **Done, tested** |
| 4-11 | Carbon engine, factors + adapters, compliance, frontend, trading, investment, reports/security hardening, Docker/E2E | Not started |

## Run everything (backend + frontend)
```
cp .env.example .env && set -a && . ./.env && set +a
cmake -S . -B build && cmake --build build -j
./build/backend/carbon_migrate up
./build/backend/carbon_seed     # optional dev accounts, see docs/local-dev.md
./build/backend/carbon_server   # then open http://localhost:8080
```
Where entered data is stored: [docs/data-storage.md](docs/data-storage.md).

## Build and test
See [docs/local-dev.md](docs/local-dev.md) for setup. One command runs everything (plain + ASan/UBSan builds):
```
./scripts/test.sh
```
API so far: 21 operations - health, auth, organizations, members, facilities, audit history ([docs/openapi.yaml](docs/openapi.yaml)).
Development accounts: see [docs/local-dev.md](docs/local-dev.md). Security design and known gaps: [docs/security.md](docs/security.md).
The ECO-NEXUS frontend is served by `carbon_server` itself (open http://localhost:8080). Calculators, compliance, marketplace and investment screens still run in the browser with placeholder factors (Phases 4-9). There is no TLS (use a reverse proxy) and no email delivery; do not expose this server publicly yet.

## Layout
```
CMakeLists.txt        top-level (C11 + C++20, sanitizer option)
dsa/include/cdsa/     public C-compatible headers (extern "C")
dsa/src/              implementations
dsa/tests/            test harness, allocation-failure injection, C++ interop test
backend/               C++20 core library, carbon_server, carbon_migrate, GoogleTest suite
frontend/              ECO-NEXUS static UI: index.html, js/ (api.js = only backend client, sends CSRF token), css/app.css (built),
                       vendor/chart.umd.js, src/input.css + tailwind.config.js + package.json (Tailwind build, only when editing classes)
db/migrations/         versioned SQL (see docs/schema.md)
docs/                  architecture, schema, local-dev, openapi.yaml, DSA complexity
scripts/test.sh       single test command
.github/workflows/    CI
```
