# Security design (Phase 3)

What is implemented, how, and what is deliberately **not** yet done. Each claim below is covered by an automated test (named in brackets).

## Authentication
- **Passwords:** Argon2id via libsodium (`interactive` profile by default, `moderate` selectable). Policy: 12-128 characters, not whitespace/repeated/common/containing the email name. The DB rejects anything that is not an Argon2id PHC string. [`Argon2.*`, `PasswordPolicy.*`, `Schema.PlaintextPasswordIsRejected`]
- **Hash upgrades:** a hash made with weaker parameters is transparently re-hashed at the next successful login. [`Auth.LoginSucceedsCaseInsensitivelyAndRehashesOldParameters`]
- **No login oracle:** unknown user, wrong password, locked and inactive accounts all return the same 401 body, and a dummy hash verification equalises timing. [`Auth.WrongPasswordAndUnknownEmailAreIndistinguishable`]
- **Lockout:** 5 consecutive failures lock the account for 15 minutes (atomic SQL counter, safe under concurrency). Trade-off: an attacker who knows an email can lock that user out; per-IP rate limiting limits the speed of that. [`Auth.AccountLocksAfterFiveFailuresAndRecovers`]
- **Rate limiting:** in-memory sliding window per client IP on login, register and password reset (429 + `Retry-After`). Single-instance only; a multi-instance deployment needs a shared (Redis) limiter - **not implemented**. `X-Forwarded-For` is deliberately **not** trusted, so behind a reverse proxy all clients share the proxy's address until trusted-proxy support is added. [`Auth.LoginIsRateLimitedPerClientAddress`, `RateLimiter.*`]

## Sessions and CSRF
- Opaque 256-bit random token in an `HttpOnly; SameSite=Strict` cookie (`Secure` and `__Host-` prefixed in production). Only the SHA-256 of the token is stored, so a database leak cannot be replayed. [`Auth.RegisterCreates...`, `Schema.SessionTokensMustBeHashed...`, `Auth.ProductionCookiesAreSecureAndHostPrefixed`]
- Absolute lifetime (12 h) and idle timeout (2 h) are configurable; sessions are revoked on logout, password reset and user deactivation. [`Auth.SessionsExpire...`, `Auth.LogoutRevokes...`, `Auth.DeactivatedUser...`]
- CSRF: every POST/PATCH/PUT/DELETE on an authenticated route needs `X-CSRF-Token` equal (constant-time comparison) to the per-session token. [`Auth.Csrf*`, `Org.WriteRequestsNeedCsrf...`]

## Password reset
256-bit random one-time token, only its hash stored, 1 hour lifetime, one live token per user, always `202` for any email, and a successful reset revokes all sessions and clears lockout. **Email delivery is not implemented**: the shipped notifier only logs the code in *development* and logs nothing secret in production. [`Auth.PasswordResetFlow`, `Auth.ResetTokenRules...`, `ResetNotifier.*`]

## Authorisation and tenant isolation
Roles: `company_admin`, `company_operator`, `consultant`, `reviewer` (per organisation) plus the platform-admin user flag.

| Permission | admin | operator | consultant | reviewer | platform admin |
|---|---|---|---|---|---|
| org read | yes | yes | yes | yes | yes |
| org update | yes | - | - | - | - |
| members read / manage | yes | - | - | - | yes |
| facility read | yes | yes | yes | yes | **no** |
| facility write | yes | - | yes | - | **no** |
| audit read | yes | - | yes | yes | yes |

- Enforced on the **server** for every request; the matrix is tested exactly. [`Roles.PermissionMatrixIsExactlyAsDocumented`, `Org.PermissionMatrixOverHttp`]
- A caller with no standing in an organisation gets **404** (existence not revealed), a member lacking the permission gets **403**. [`Org.UserCannotSeeOrTouchAnotherTenant`, `Authorization.*`]
- Organisation ids always come from the URL and are checked against the *session's* memberships, never trusted from the body; every service query is also scoped by organisation id, and the database's composite foreign keys make cross-tenant references impossible. [`Org.FacilityIdFromAnotherTenant...`, `Org.MembershipIdOfAnotherOrganisation...`, `Org.MalformedAndMassAssignmentInputsAreRejected`]
- An organisation always keeps at least one `company_admin` (row-locked, deterministic order), including under concurrent demotions. [`Org.ConcurrentMutualDemotion...`]
- Platform admins can create organisations and manage memberships (needed to bootstrap customers) but cannot read or change company facility data. They can, however, make themselves a member of an organisation - that action is audited. [`Org.PlatformAdminManages...`]

## Input handling
Strict JSON schema-style validation: unknown fields rejected (mass assignment), types/lengths/control characters checked, all problems reported together with HTTP 422; wrong content type 415; invalid JSON 400; 1 MiB body limit (413). Ids are parsed to a `Uuid` type before use. All SQL is parameterised; sort columns come only from whitelists; `LIKE` wildcards in user text are escaped. [`BodyReader.*`, `Org.SqlInjectionAttemptsAreHarmless`, `Org.PaginationFilteringAndSorting`, `Org.OversizedRequestBodiesAreRejected`]

## Audit and logging
Registration, login success/failure, logout, password reset, organisation/membership/facility changes write `audit_events` in the **same transaction** as the change (so failures leave no trace and successes always have one); the table is append-only in the database. Logs are structured JSON, secrets are redacted by key name, and passwords/tokens never appear. [`Org.SensitiveActionsAreAudited...`, `Org.FailedOperationsLeaveNoAudit...`, `Auth.LogsNeverContainPasswordsOrSessionTokens`]

## Known gaps (not yet implemented)
TLS termination (expected from a reverse proxy; HSTS is sent in production), shared rate limiting, trusted-proxy IP handling, change-password and account-recovery by email, MFA, CORS with credentials (frontend will be same-origin), dependency/container vulnerability scanning (Phase 10/11), file-upload validation (Phase 6). Registration reveals whether an email is already registered (409); this UX trade-off is rate limited.
