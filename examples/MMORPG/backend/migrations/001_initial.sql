BEGIN;
CREATE TABLE IF NOT EXISTS schema_migrations(version integer PRIMARY KEY, applied_at timestamptz NOT NULL DEFAULT now());
CREATE TABLE accounts(
 id uuid PRIMARY KEY DEFAULT gen_random_uuid(), username text NOT NULL UNIQUE CHECK(username ~ '^[a-z0-9_]{3,32}$'),
 password_hash text NOT NULL, created_at timestamptz NOT NULL DEFAULT now()
);
CREATE TABLE sessions(token_hash text PRIMARY KEY, account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
 expires_at timestamptz NOT NULL, created_at timestamptz NOT NULL DEFAULT now());
CREATE INDEX sessions_account_idx ON sessions(account_id);
CREATE TABLE characters(
 id uuid PRIMARY KEY DEFAULT gen_random_uuid(), account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
 name text NOT NULL CHECK(length(name) BETWEEN 2 AND 24), version bigint NOT NULL DEFAULT 0 CHECK(version>=0),
 state jsonb NOT NULL DEFAULT '{"level":1,"xp":0,"health":100,"mana":100,"inventory":[],"position":{"x":0,"y":0,"z":150}}',
 updated_at timestamptz NOT NULL DEFAULT now(), UNIQUE(account_id,name)
);
CREATE INDEX characters_account_idx ON characters(account_id);
CREATE TABLE join_tickets(token_hash text PRIMARY KEY, account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
 character_id uuid NOT NULL REFERENCES characters(id) ON DELETE CASCADE, server_id text NOT NULL,
 expires_at timestamptz NOT NULL, consumed_at timestamptz);
CREATE INDEX join_tickets_expiry_idx ON join_tickets(expires_at);
CREATE TABLE game_leases(character_id uuid PRIMARY KEY REFERENCES characters(id) ON DELETE CASCADE,
 account_id uuid NOT NULL UNIQUE REFERENCES accounts(id) ON DELETE CASCADE, token_hash text NOT NULL,
 server_id text NOT NULL, expires_at timestamptz NOT NULL);
CREATE TABLE save_requests(character_id uuid NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
 request_id uuid NOT NULL, lease_hash text NOT NULL, payload_hash text NOT NULL, result_version bigint NOT NULL,
 created_at timestamptz NOT NULL DEFAULT now(), PRIMARY KEY(character_id, request_id));
INSERT INTO schema_migrations(version) VALUES (1);
COMMIT;
