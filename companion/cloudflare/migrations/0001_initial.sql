
CREATE TABLE users(id TEXT PRIMARY KEY, email TEXT UNIQUE NOT NULL, password TEXT NOT NULL);
CREATE TABLE sessions(token TEXT PRIMARY KEY, user_id TEXT NOT NULL REFERENCES users(id), csrf TEXT NOT NULL, expires REAL NOT NULL);
CREATE TABLE pairs(id TEXT PRIMARY KEY, code TEXT UNIQUE NOT NULL, token TEXT NOT NULL, name TEXT NOT NULL, user_id TEXT REFERENCES users(id), expires REAL NOT NULL, state TEXT NOT NULL);
CREATE TABLE devices(id TEXT PRIMARY KEY, user_id TEXT NOT NULL REFERENCES users(id), name TEXT NOT NULL, token TEXT UNIQUE NOT NULL, expires REAL NOT NULL, revoked INTEGER NOT NULL DEFAULT 0, last_seen REAL);
CREATE TABLE jobs(id TEXT PRIMARY KEY, user_id TEXT NOT NULL REFERENCES users(id), device_id TEXT NOT NULL REFERENCES devices(id), document_id TEXT NOT NULL, filename TEXT NOT NULL, source_format TEXT NOT NULL, format TEXT, source_sha256 TEXT NOT NULL, sha256 TEXT, source_size INTEGER NOT NULL, size INTEGER, received INTEGER NOT NULL DEFAULT 0, status TEXT NOT NULL, error TEXT, created REAL NOT NULL, updated REAL NOT NULL, expires REAL NOT NULL, converted INTEGER NOT NULL DEFAULT 0);
CREATE INDEX jobs_delivery ON jobs(device_id,status);
CREATE INDEX jobs_duplicates ON jobs(device_id,source_sha256);
CREATE TABLE tombstones(device_id TEXT NOT NULL, document_id TEXT NOT NULL, created REAL NOT NULL, PRIMARY KEY(device_id,document_id));
CREATE TABLE tom(id TEXT PRIMARY KEY, user_id TEXT NOT NULL REFERENCES users(id), device_id TEXT, conversation_id TEXT NOT NULL, fingerprint TEXT NOT NULL, payload TEXT NOT NULL, status TEXT NOT NULL, text TEXT NOT NULL DEFAULT '', error TEXT, created REAL NOT NULL, updated REAL NOT NULL);
CREATE TABLE tom_events(request_id TEXT NOT NULL REFERENCES tom(id) ON DELETE CASCADE, seq INTEGER NOT NULL, event TEXT NOT NULL, data TEXT NOT NULL, PRIMARY KEY(request_id,seq));
CREATE TABLE limits(bucket TEXT PRIMARY KEY, start REAL NOT NULL, count INTEGER NOT NULL);


ALTER TABLE jobs ADD COLUMN generation INTEGER NOT NULL DEFAULT 0;
ALTER TABLE jobs ADD COLUMN lease TEXT;
ALTER TABLE jobs ADD COLUMN ready_key TEXT;
CREATE TABLE chunks(job_id TEXT NOT NULL REFERENCES jobs(id) ON DELETE CASCADE,generation INTEGER NOT NULL,start INTEGER NOT NULL,length INTEGER NOT NULL,sha256 TEXT NOT NULL,object_key TEXT NOT NULL,PRIMARY KEY(job_id,generation,start));
CREATE TABLE outbox(id TEXT PRIMARY KEY,kind TEXT NOT NULL,created REAL NOT NULL);
CREATE INDEX sessions_expiry ON sessions(expires);
CREATE INDEX jobs_expiry ON jobs(expires,status);
ALTER TABLE jobs ADD COLUMN intentional_new INTEGER NOT NULL DEFAULT 0;
ALTER TABLE tom ADD COLUMN payload_key TEXT;
ALTER TABLE jobs ADD COLUMN cleanup_pending INTEGER NOT NULL DEFAULT 0;
ALTER TABLE tom ADD COLUMN payload_size INTEGER NOT NULL DEFAULT 0;
