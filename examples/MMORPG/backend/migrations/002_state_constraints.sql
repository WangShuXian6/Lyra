BEGIN;
ALTER TABLE characters ADD CONSTRAINT state_shape CHECK (
 jsonb_typeof(state) = 'object' AND state ?& ARRAY['level','xp','health','mana','inventory','position']
 AND jsonb_typeof(state->'inventory') = 'array' AND jsonb_typeof(state->'position') = 'object');
CREATE INDEX save_requests_created_idx ON save_requests(created_at);
CREATE INDEX sessions_expiry_idx ON sessions(expires_at);
INSERT INTO schema_migrations(version) VALUES (2);
COMMIT;
