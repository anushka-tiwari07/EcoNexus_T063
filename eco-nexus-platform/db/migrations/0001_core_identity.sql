-- 0001: identity, tenancy, facilities.
-- Conventions: UUID primary keys; timestamptz; text + CHECK instead of enums (easier to evolve);
-- every tenant-owned table carries organization_id and uses composite FKs
-- (organization_id, x_id) so a child row can never point at another tenant's parent.

CREATE FUNCTION set_updated_at() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  NEW.updated_at = now();
  RETURN NEW;
END $$;

CREATE TABLE users (
  id                uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  email             text NOT NULL CHECK (length(email) BETWEEN 3 AND 254 AND position('@' IN email) > 1),
  -- Only Argon2id PHC strings are accepted: a plaintext password cannot be stored by mistake.
  password_hash     text NOT NULL CHECK (password_hash ~ '^\$argon2id\$'),
  display_name      text NOT NULL CHECK (length(btrim(display_name)) > 0),
  is_platform_admin boolean NOT NULL DEFAULT false,
  is_active         boolean NOT NULL DEFAULT true,
  failed_login_count integer NOT NULL DEFAULT 0 CHECK (failed_login_count >= 0),
  locked_until      timestamptz,
  last_login_at     timestamptz,
  created_at        timestamptz NOT NULL DEFAULT now(),
  updated_at        timestamptz NOT NULL DEFAULT now()
);
CREATE UNIQUE INDEX users_email_lower_key ON users (lower(email));
CREATE TRIGGER users_updated BEFORE UPDATE ON users FOR EACH ROW EXECUTE FUNCTION set_updated_at();

CREATE TABLE organizations (
  id          uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  name        text NOT NULL CHECK (length(btrim(name)) > 0),
  legal_name  text,
  sector      text NOT NULL CHECK (length(btrim(sector)) > 0),
  state       text,
  gstin       text CHECK (gstin IS NULL OR gstin ~ '^[0-9A-Z]{15}$'),
  is_active   boolean NOT NULL DEFAULT true,
  created_at  timestamptz NOT NULL DEFAULT now(),
  updated_at  timestamptz NOT NULL DEFAULT now()
);
CREATE UNIQUE INDEX organizations_name_lower_key ON organizations (lower(name));
CREATE TRIGGER organizations_updated BEFORE UPDATE ON organizations FOR EACH ROW EXECUTE FUNCTION set_updated_at();

-- Platform administrators are flagged on users.is_platform_admin (not a per-org role).
CREATE TABLE organization_memberships (
  id              uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id uuid NOT NULL REFERENCES organizations(id),
  user_id         uuid NOT NULL REFERENCES users(id),
  role            text NOT NULL CHECK (role IN ('company_admin','company_operator','consultant','reviewer')),
  created_at      timestamptz NOT NULL DEFAULT now(),
  UNIQUE (organization_id, user_id)
);
CREATE INDEX organization_memberships_user_idx ON organization_memberships (user_id);

CREATE TABLE facilities (
  id              uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id uuid NOT NULL REFERENCES organizations(id),
  name            text NOT NULL CHECK (length(btrim(name)) > 0),
  sector          text NOT NULL CHECK (length(btrim(sector)) > 0),
  state           text,
  address         text,
  production_unit text NOT NULL CHECK (length(btrim(production_unit)) > 0),
  boundary_notes  text,
  is_active       boolean NOT NULL DEFAULT true,
  created_at      timestamptz NOT NULL DEFAULT now(),
  updated_at      timestamptz NOT NULL DEFAULT now(),
  UNIQUE (organization_id, name),
  UNIQUE (organization_id, id)          -- target for tenant-safe composite FKs
);
CREATE TRIGGER facilities_updated BEFORE UPDATE ON facilities FOR EACH ROW EXECUTE FUNCTION set_updated_at();
