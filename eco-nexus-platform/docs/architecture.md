# Architecture decisions (Phase 1 checkpoint)

Modular monolith: browser (HTML/Tailwind/ES modules) -> Crow REST (`/api/v1`) -> services -> domain modules -> PostgreSQL / external-data adapters.

| Decision | Choice | Reason |
|---|---|---|
| Build | single CMake project, C11 + C++20 | one command builds both languages |
| C/C++ boundary | opaque handles + `extern "C"` headers; RAII wrappers in C++ | proven by `test_cpp_interop` |
| Errors in C | `cdsa_status` return codes, no globals | testable, no hidden state |
| Financial truth | PostgreSQL transactions, never the C structures or Redis | per spec section 7 |
| Warnings | `-Wall -Wextra -Wpedantic -Wshadow -Werror` | catch defects early |

## Dependencies
In use: Crow v1.3.3 (pinned, fetched by CMake), asio, nlohmann/json, libpqxx 7.8, OpenSSL (SHA-256, CSPRNG), libsodium (Argon2id), GoogleTest, PostgreSQL 16.
Planned: Redis (optional), Tailwind CLI + Chart.js (frontend build only), Docker/Compose.

## Phase 2 notes
- The C DSA library is **not yet linked into the backend**; it will be added in the phases that genuinely use it (ranking, caches, order-book simulation).
- Crow's `handle_full` bypasses connection-level middleware and OPTIONS requests skip `before_handle`, so HTTP tests run a real server on an ephemeral port and the middleware initialises its context lazily.
- Not yet implemented: shared (Redis) rate limiting, TLS termination (assumed to be a reverse proxy; HSTS is sent in production mode), Docker.

## Phase 3 notes
- Layering: `http/api_routes` (parsing, auth, permission checks) -> `auth/*`, `domain/*` services (business rules, SQL, audit in the same transaction) -> PostgreSQL. Services never see HTTP types.
- Polymorphism in use: `PasswordHasher` (Argon2id + test double), `ResetNotifier`, `HealthProbe`.
- Defence in depth for tenancy: permission check on the session's memberships, every query scoped by organisation id, composite foreign keys in the schema.
- Lessons recorded: route lambdas must not capture locals by reference (Crow keeps them for the server lifetime - a real use-after-scope bug was caught by the integration tests); `nlohmann::json{}` is `null`, not `{}`.

## Frontend + backend integration (merged)
- One deployable: `carbon_server` serves the static `frontend/` and the JSON API under `/api/v1` from the same origin (no CORS).
- Headers depend on the path: `/api/*` keeps `default-src 'none'` + `no-store`; frontend files get a CSP of `'self'` + `no-cache`.
- Static serving resolves against the real frontend root, rejects `..`, absolute paths, dotfiles and directories, and never answers `/api/*` from disk.
- The browser never sees the session token (HttpOnly, SameSite=Strict cookie). `js/api.js` keeps the CSRF token in memory and sends it as `X-CSRF-Token` on every non-GET request.
- `CARBON_FRONTEND_DIR` (default `frontend`, `none` = API only).
