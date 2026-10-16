"""Private durable relay state. Each transaction uses its own SQLite connection."""
import contextlib
import hashlib
import os
from pathlib import Path
import secrets
import sqlite3
import time

SCHEMA = """
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
PRAGMA user_version=1;
"""

def digest(value):
    return hashlib.sha256(value.encode()).hexdigest()

def password_hash(password, salt=None):
    if not isinstance(password, str) or not 14 <= len(password) <= 256:
        raise ValueError("Password must be 14–256 characters")
    salt = salt or secrets.token_hex(16)
    key = hashlib.scrypt(password.encode(), salt=bytes.fromhex(salt), n=16384, r=8, p=1).hex()
    return salt + ":" + key

def verify_password(password, stored):
    try:
        return secrets.compare_digest(password_hash(password, stored.split(":")[0]), stored)
    except (ValueError, TypeError):
        return False

class Database:
    def __init__(self, root):
        self.root = Path(root).resolve()
        self.root.mkdir(parents=True, exist_ok=True, mode=0o700)
        os.chmod(self.root, 0o700)
        self.files = self.root / "private"
        self.files.mkdir(exist_ok=True, mode=0o700)
        self.path = self.root / "relay.sqlite3"
        with self.connect() as db:
            version = db.execute("PRAGMA user_version").fetchone()[0]
            if version == 0:
                db.executescript("BEGIN IMMEDIATE;" + SCHEMA + "COMMIT;")
                version = 1
            if version == 1:
                db.executescript("BEGIN IMMEDIATE; ALTER TABLE jobs ADD COLUMN intentional_new INTEGER NOT NULL DEFAULT 0; PRAGMA user_version=2; COMMIT;")
            elif version != 2:
                raise RuntimeError("Unsupported database version; restore backup or use matching release")
        os.chmod(self.path, 0o600)

    @contextlib.contextmanager
    def connect(self):
        db = sqlite3.connect(self.path, timeout=15)
        db.row_factory = sqlite3.Row
        db.execute("PRAGMA foreign_keys=ON")
        db.execute("PRAGMA journal_mode=WAL")
        db.execute("PRAGMA synchronous=FULL")
        try:
            with db:
                yield db
        finally:
            db.close()

    def rows(self, sql, args=()):
        with self.connect() as db:
            return [dict(r) for r in db.execute(sql, args).fetchall()]

    def one(self, sql, args=()):
        rows = self.rows(sql, args)
        return rows[0] if rows else None

    def run(self, sql, args=()):
        with self.connect() as db:
            return db.execute(sql, args).rowcount

    def rate(self, bucket, count, window):
        now = time.time()
        with self.connect() as db:
            db.execute("BEGIN IMMEDIATE")
            db.execute("DELETE FROM limits WHERE start < ?", (now - 86400,))
            row = db.execute("SELECT * FROM limits WHERE bucket=?", (bucket,)).fetchone()
            if not row or row["start"] + window < now:
                db.execute("INSERT OR REPLACE INTO limits VALUES(?,?,1)", (bucket, now))
                return True
            if row["count"] >= count:
                return False
            db.execute("UPDATE limits SET count=count+1 WHERE bucket=?", (bucket,))
            return True

    def blob(self, job_id, suffix):
        if not all(c in "0123456789abcdef-" for c in job_id) or len(job_id) != 36:
            raise ValueError("Invalid object ID")
        return self.files / (job_id + suffix)

    def cleanup_blobs(self, job_id):
        for suffix in (".upload", ".ready", ".tmp"):
            self.blob(job_id, suffix).unlink(missing_ok=True)
