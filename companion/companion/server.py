"""Single-node private companion service; run behind the supplied TLS proxy."""
import argparse
import base64
import getpass
import hashlib
import hmac
from http.cookies import SimpleCookie
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import secrets
import signal
import sqlite3
import subprocess
import sys
import threading
import time
from urllib.parse import urlsplit, parse_qs
import uuid

from .db import Database, digest, password_hash, verify_password
from .tom import Tom, TomError, DEFAULT_MODEL, valid_id

ROOT = Path(__file__).resolve().parent.parent
MAX_UPLOAD = 64 * 1024 * 1024
MAX_DELIVERY = 64 * 1024 * 1024
CHUNK = 1024 * 1024
FORMATS = {"pdf", "epub", "docx", "txt", "md"}
TERMINAL = {"imported", "canceled", "expired", "deleted"}

class APIError(Exception):
    def __init__(self, status, message, code=None):
        self.status, self.message, self.code = status, message, code or "request_rejected"

def need(condition, status, message, code=None):
    if not condition:
        raise APIError(status, message, code)

def safe_name(name):
    need(isinstance(name, str) and 1 <= len(name) <= 180 and name not in (".", "..") and not re.search(r"[/\\\x00-\x1f\x7f]", name), 400, "Filename is invalid")
    return name

def uid():
    return str(uuid.uuid4())

class App:
    def __init__(self, data, origin, dev=False, api_key="", models=(DEFAULT_MODEL,)):
        self.db = Database(data)
        self.db.run("UPDATE jobs SET status='queued' WHERE status='processing'")
        self.db.run("UPDATE tom SET status='interrupted',error='Service restarted; explicit retry required' WHERE status IN ('queued','streaming')")
        for job in self.db.rows("SELECT id FROM jobs WHERE status IN ('imported','canceled','expired','deleted')"):
            self.db.cleanup_blobs(job["id"])
        self.origin = origin.rstrip("/")
        parsed = urlsplit(self.origin)
        if parsed.scheme not in ("http", "https") or not parsed.netloc or parsed.path or parsed.query or parsed.fragment or parsed.username:
            raise ValueError("Origin must be an exact HTTP(S) origin")
        if not dev and parsed.scheme != "https":
            raise ValueError("Production requires an HTTPS origin")
        self.dev = dev
        self.host = parsed.netloc
        self.tom = Tom(self.db, api_key, models)
        self.mutations = threading.Lock()
        self.stop = threading.Event()
        secret_path = self.db.root / "download-signing.key"
        if not secret_path.exists():
            fd = os.open(secret_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            with os.fdopen(fd, "wb") as f:
                f.write(secrets.token_bytes(32)); f.flush(); os.fsync(f.fileno())
        self.signing_key = secret_path.read_bytes()
        self.worker = threading.Thread(target=self.process_jobs, daemon=True)
        self.worker.start()

    def ticket(self, job, device_id, expires=None):
        expires = int(expires or time.time() + 300)
        content = f"{device_id}:{job['id']}:{job['sha256']}:{expires}"
        signature = hmac.new(self.signing_key, content.encode(), hashlib.sha256).hexdigest()
        return f"{expires}.{signature}", expires

    def check_ticket(self, value, job, device_id):
        try:
            expires = int(value.split(".")[0])
            need(time.time() < expires <= time.time() + 301, 410, "Download access expired; poll queue again")
            expected, _ = self.ticket(job, device_id, expires)
            need(hmac.compare_digest(value, expected), 403, "Invalid download access")
        except (ValueError, AttributeError):
            raise APIError(403, "Download access required")

    def process_jobs(self):
        last_sweep = 0
        while not self.stop.wait(.15):
            if time.time() - last_sweep > 60:
                self.sweep(); last_sweep = time.time()
            with self.db.connect() as db:
                db.execute("BEGIN IMMEDIATE")
                row = db.execute("SELECT * FROM jobs WHERE status='queued' ORDER BY created LIMIT 1").fetchone()
                if row:
                    db.execute("UPDATE jobs SET status='processing',updated=? WHERE id=?", (time.time(), row["id"]))
            if not row:
                continue
            job = dict(row)
            source = self.db.blob(job["id"], ".upload")
            temporary = self.db.blob(job["id"], ".tmp")
            ready = self.db.blob(job["id"], ".ready")
            try:
                with source.open("rb") as f:
                    checksum = hashlib.file_digest(f, "sha256").hexdigest()
                if checksum != job["source_sha256"] or source.stat().st_size != job["source_size"]:
                    raise APIError(409, "Upload checksum/length mismatch")
                # Do not inherit session/provider credentials or arbitrary PYTHONPATH.
                environment = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "LANG": "C.UTF-8", "PYTHONPATH": str(ROOT), "PYTHONDONTWRITEBYTECODE": "1"}
                result = subprocess.run([sys.executable, "-m", "companion.convert", str(source), str(temporary), job["source_format"], job["filename"]], cwd=self.db.files, env=environment, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=30, check=False)
                need(len(result.stdout) <= 8192, 422, "Converter response exceeds limit")
                response = json.loads(result.stdout)
                need(result.returncode == 0, 422, response.get("error", "Conversion failed"))
                need(temporary.stat().st_size <= MAX_DELIVERY, 413, "Converted document exceeds size limit")
                with temporary.open("rb") as f:
                    checksum = hashlib.file_digest(f, "sha256").hexdigest()
                size = temporary.stat().st_size
                with self.mutations:
                    latest = self.db.one("SELECT status FROM jobs WHERE id=?", (job["id"],))
                    if latest["status"] != "processing":
                        self.db.cleanup_blobs(job["id"])
                        continue
                    os.replace(temporary, ready)
                    fd = os.open(self.db.files, os.O_RDONLY)
                    try: os.fsync(fd)
                    finally: os.close(fd)
                    self.db.run("UPDATE jobs SET status='waiting for device',format=?,sha256=?,size=?,converted=?,error=NULL,updated=? WHERE id=?", (response["format"], checksum, size, int(response["converted"]), time.time(), job["id"]))
                    source.unlink(missing_ok=True)
            except Exception as exc:
                message = exc.message if isinstance(exc, APIError) else "Document processing failed or exceeded resource limits"
                self.db.run("UPDATE jobs SET status='failed',error=?,updated=? WHERE id=? AND status='processing'", (message, time.time(), job["id"]))
                temporary.unlink(missing_ok=True)

    def sweep(self):
        with self.mutations:
            now = time.time()
            for job in self.db.rows("SELECT id FROM jobs WHERE expires<? AND status NOT IN ('imported','canceled','expired','deleted')", (now,)):
                self.db.run("UPDATE jobs SET status='expired',error='Relay copy expired after 7 days',updated=? WHERE id=?", (now, job["id"]))
                self.db.cleanup_blobs(job["id"])
            self.db.run("DELETE FROM sessions WHERE expires<?", (now,))
            self.db.run("DELETE FROM pairs WHERE expires<?", (now,))

    def close(self):
        self.stop.set()
        with self.tom.lock:
            ids = list(self.tom.active)
        for ident in ids:
            self.tom.cancel(ident)
        self.worker.join(timeout=35)

class Handler(BaseHTTPRequestHandler):
    server_version = "InkboundCompanion/1"
    protocol_version = "HTTP/1.1"
    def log_message(self, *args):
        pass  # Never log object names, document bytes, URLs, tokens, or content.

    @property
    def app(self):
        return self.server.app

    def setup(self):
        super().setup()
        self.connection.settimeout(30)

    def headers_secure(self):
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'")
        self.send_header("Permissions-Policy", "camera=(), microphone=(), geolocation=()")
        if not self.app.dev:
            self.send_header("Strict-Transport-Security", "max-age=31536000")

    def respond(self, data, status=200, headers=None):
        raw = json.dumps(data, ensure_ascii=False).encode()
        self.send_response(status)
        self.headers_secure()
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(raw)))
        for key, value in (headers or {}).items():
            self.send_header(key, value)
        self.end_headers()
        self.wfile.write(raw)

    def raw_body(self, limit):
        need(not self.headers.get("Transfer-Encoding"), 400, "Chunked request encoding unsupported")
        need(len(self.headers.get_all('Content-Length', [])) <= 1, 400, 'Ambiguous content length')
        try: size = int(self.headers.get("Content-Length", "0"))
        except ValueError: raise APIError(400, "Invalid content length")
        need(0 <= size <= limit, 413, "Request body exceeds limit")
        raw = self.rfile.read(size)
        need(len(raw) == size, 400, "Incomplete body; retry same offset")
        return raw

    def body(self, limit=6 * 1024 * 1024):
        need(self.headers.get("Content-Type", "").split(";")[0] == "application/json", 415, "JSON required")
        try: body = json.loads(self.raw_body(limit))
        except (ValueError, UnicodeDecodeError): raise APIError(400, "Malformed JSON")
        need(isinstance(body, dict), 400, "JSON object required")
        return body

    def bearer(self):
        value = self.headers.get("Authorization", "")
        return value[7:] if value.startswith("Bearer ") else ""

    def session(self, mutate=False):
        cookies = SimpleCookie()
        try: cookies.load(self.headers.get("Cookie", ""))
        except Exception: raise APIError(401, "Sign in required")
        value = cookies.get("inkbound_session")
        row = self.app.db.one("SELECT s.*,u.email FROM sessions s JOIN users u ON u.id=s.user_id WHERE s.token=? AND expires>?", (digest(value.value if value else ""), time.time()))
        need(row is not None, 401, "Sign in required")
        if mutate:
            self.origin()
            need(secrets.compare_digest(self.headers.get("X-CSRF-Token", ""), row["csrf"]), 403, "CSRF token required")
        return row

    def origin(self):
        need(self.headers.get("Origin") == self.app.origin, 403, "Exact origin required")

    def device(self):
        row = self.app.db.one("SELECT * FROM devices WHERE token=? AND revoked=0 AND expires>?", (digest(self.bearer()), time.time()))
        need(row is not None, 401, "Device credential expired or revoked")
        self.app.db.run("UPDATE devices SET last_seen=? WHERE id=?", (time.time(), row["id"]))
        row["device_id"] = row["id"]
        return row

    def owner(self, mutate=False):
        return self.device() if self.bearer() else self.session(mutate)

    def pair(self, ident):
        row = self.app.db.one("SELECT * FROM pairs WHERE id=? AND token=?", (ident, digest(self.bearer())))
        need(row is not None, 401, "Pairing credential invalid")
        need(row["expires"] > time.time(), 410, "Pairing expired")
        return row

    def device_owned(self, ident, user_id):
        row = self.app.db.one("SELECT * FROM devices WHERE id=? AND user_id=? AND revoked=0 AND expires>?", (ident, user_id, time.time()))
        need(row is not None, 404, "Device unavailable")
        return row

    def job_owned(self, ident, owner, native=False):
        row = self.app.db.one("SELECT * FROM jobs WHERE id=? AND user_id=?", (ident, owner["user_id"]))
        need(row and (not native or row["device_id"] == owner["device_id"]), 404, "Job unavailable")
        return row

    def do_GET(self): self.dispatch("GET")
    def do_POST(self): self.dispatch("POST")
    def do_PUT(self): self.dispatch("PUT")
    def do_DELETE(self): self.dispatch("DELETE")

    def dispatch(self, method):
        try:
            need(self.headers.get("Host") == self.app.host, 403, "Host is not configured")
            need(self.app.db.rate("http:" + self.client_address[0], 1000, 60), 429, "Request rate exceeded")
            parts = urlsplit(self.path)
            path, query = parts.path, parse_qs(parts.query)
            if path == "/healthz" and method == "GET":
                self.app.db.one("SELECT 1")
                need(self.app.worker.is_alive(), 503, 'Document worker unavailable; restart and inspect storage health')
                return self.respond({"ok": True, "protocol": 1})
            if path == "/v1/auth/login" and method == "POST":
                self.origin()
                need(self.app.db.rate("login:" + self.client_address[0], 10, 60), 429, "Too many sign-in attempts")
                data = self.body(4096)
                email = str(data.get("email", "")).strip().lower()
                row = self.app.db.one("SELECT * FROM users WHERE email=?", (email,))
                # Equal-cost check also for nonexistent accounts.
                dummy = "0" * 32 + ":" + "0" * 128
                good = verify_password(data.get("password", ""), row["password"] if row else dummy)
                need(row and good, 401, "Email or password incorrect")
                token, csrf = secrets.token_urlsafe(32), secrets.token_urlsafe(32)
                self.app.db.run("INSERT INTO sessions VALUES(?,?,?,?)", (digest(token), row["id"], csrf, time.time() + 43200))
                return self.respond({"email": row["email"], "csrf": csrf}, headers={"Set-Cookie": "inkbound_session=" + token + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=43200" + ("" if self.app.dev else "; Secure")})
            if path == "/v1/auth/logout" and method == "POST":
                owner = self.session(True)
                self.app.db.run("DELETE FROM sessions WHERE token=?", (owner["token"],))
                return self.respond({"signed_out": True}, headers={"Set-Cookie": "inkbound_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0" + ("" if self.app.dev else "; Secure")})
            if path == "/v1/me" and method == "GET":
                owner = self.session()
                return self.respond({"email": owner["email"], "csrf": owner["csrf"], "max_upload_bytes": MAX_UPLOAD, "chunk_bytes": CHUNK, "retention_days": 7, "delete_after_ack": True, "tom_configured": bool(self.app.tom.api_key), "tom_models": self.app.tom.models, "tom_provider": {"id": "openai", "label": "OpenAI"}, "tom_max_output_tokens": 2048, "tom_daily_limit": 30, "protocol": 1})
            if path == "/v1/pair/start" and method == "POST":
                need(self.app.db.rate("pair-start:" + self.client_address[0], 10, 600), 429, "Pairing rate exceeded")
                data = self.body(4096)
                name = safe_name(data.get("name", "Inkbound Reader"))
                ident, token = uid(), secrets.token_urlsafe(32)
                code = "".join(secrets.choice("ABCDEFGHJKLMNPQRSTUVWXYZ23456789") for _ in range(8))
                expires = time.time() + 300
                self.app.db.run("INSERT INTO pairs VALUES(?,?,?,?,NULL,?,'pending')", (ident, code, digest(token), name, expires))
                return self.respond({"pairing_id": ident, "code": code, "pairing_token": token, "expires_at": expires}, 201)
            if path == "/v1/pair/claim" and method == "POST":
                owner = self.session(True)
                need(self.app.db.rate("pair-claim:" + owner["user_id"], 10, 600), 429, "Pair code attempt limit reached")
                data = self.body(4096)
                code = str(data.get("code", "")).replace("-", "").strip().upper()
                with self.app.db.connect() as db:
                    db.execute("BEGIN IMMEDIATE")
                    row = db.execute("SELECT * FROM pairs WHERE code=? AND expires>? AND state='pending'", (code, time.time())).fetchone()
                    need(row is not None, 409, "Pair code invalid, expired, or already claimed")
                    db.execute("UPDATE pairs SET user_id=?,state='claimed' WHERE id=?", (owner["user_id"], row["id"]))
                return self.respond({"pairing_id": row["id"], "state": "claimed", "name": row["name"], "message": "Approve this account on your reader"})
            match = re.fullmatch(r"/v1/pair/([a-f0-9-]{36})(/approve)?", path)
            if match:
                pair = self.pair(match[1])
                if method == "GET" and not match[2]:
                    account = self.app.db.one("SELECT email FROM users WHERE id=?", (pair["user_id"],))
                    return self.respond({"pairing_id": pair["id"], "state": pair["state"], "account": account["email"] if account else None, "expires_at": pair["expires"]})
                if method == "POST" and match[2]:
                    data = self.body(4096)
                    need(pair["state"] == "claimed", 409, "Pairing is not awaiting device approval")
                    with self.app.db.connect() as db:
                        db.execute("BEGIN IMMEDIATE")
                        changed = db.execute("UPDATE pairs SET state=? WHERE id=? AND state='claimed' AND expires>?", ("approved" if data.get("approve") is True else "rejected", pair["id"], time.time())).rowcount
                        need(changed == 1, 409, "Pairing already consumed")
                        if data.get("approve") is not True:
                            return self.respond({"state": "rejected"})
                        ident, token, expires = uid(), secrets.token_urlsafe(32), time.time() + 90 * 86400
                        db.execute("INSERT INTO devices(id,user_id,name,token,expires) VALUES(?,?,?,?,?)", (ident, pair["user_id"], pair["name"], digest(token), expires))
                    return self.respond({"device_id": ident, "device_token": token, "expires_at": expires}, 201)
            if path == "/v1/devices" and method == "GET":
                owner = self.session()
                return self.respond({"devices": self.app.db.rows("SELECT id,name,last_seen,expires,revoked FROM devices WHERE user_id=? ORDER BY name", (owner["user_id"],))})
            match = re.fullmatch(r"/v1/devices/([a-f0-9-]{36})", path)
            if match and method == "DELETE":
                owner = self.session(True)
                self.device_owned(match[1], owner["user_id"])
                with self.app.mutations:
                    self.app.db.run("UPDATE devices SET revoked=1 WHERE id=?", (match[1],))
                    for job in self.app.db.rows("SELECT id FROM jobs WHERE device_id=? AND status NOT IN ('imported','deleted','expired','canceled')", (match[1],)):
                        self.app.db.run("UPDATE jobs SET status='canceled',error='Device revoked' WHERE id=?", (job["id"],))
                        self.app.db.cleanup_blobs(job["id"])
                return self.respond({"revoked": True})
            if path == "/v1/jobs":
                owner = self.session(method != "GET")
                if method == "GET":
                    query = parse_qs(parts.query, keep_blank_values=True)
                    need(len(query.get("ids", [])) <= 1 and len(query.get("limit", [])) <= 1, 400, "Duplicate delivery query")
                    raw_limit = query.get("limit", ["200"])[0]
                    need(re.fullmatch(r"[1-9][0-9]{0,2}", raw_limit) is not None and int(raw_limit) <= 200, 400, "Invalid delivery limit")
                    ids = query["ids"][0].split(",") if "ids" in query else None
                    need(ids is None or (1 <= len(ids) <= 32 and all(re.fullmatch(r"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}", x) for x in ids) and len(set(ids)) == len(ids)), 400, "Invalid delivery scope")
                    sql = "SELECT id AS job_id,document_id,device_id,filename,source_format,format,source_size,size,source_sha256,received,status,error,converted,created,updated,expires FROM jobs WHERE user_id=?"
                    args = [owner["user_id"]]
                    if ids is not None:
                        sql += " AND id IN (" + ",".join("?" for _ in ids) + ")"
                        args.extend(ids)
                    sql += " ORDER BY created DESC,id DESC LIMIT ?"
                    args.append(int(raw_limit))
                    return self.respond({"jobs": self.app.db.rows(sql, tuple(args))})
                if method == "POST":
                    return self.create_job(owner)
            match = re.fullmatch(r"/v1/jobs/([a-f0-9-]{36})(/(chunk|complete|retry))?", path)
            if match:
                owner = self.session(method != "GET")
                with self.app.mutations:
                    job = self.job_owned(match[1], owner)
                    action = match[3]
                    if method == "GET" and not action:
                        return self.respond({k: v for k, v in job.items() if k != "user_id"})
                    if method == "DELETE" and not action:
                        need(job["status"] != "imported", 409, "Already imported; relay deletion does not delete reader content")
                        self.app.db.run("UPDATE jobs SET status='canceled',updated=? WHERE id=?", (time.time(), job["id"]))
                        self.app.db.cleanup_blobs(job["id"])
                        return self.respond({"status": "canceled"})
                    if method == "PUT" and action == "chunk":
                        return self.chunk(job)
                    if method == "POST" and action == "complete":
                        self.body(4096)
                        need(job["status"] == "uploading" and job["received"] == job["source_size"], 409, "Upload incomplete or already submitted")
                        self.app.db.run("UPDATE jobs SET status='queued',updated=? WHERE id=?", (time.time(), job["id"]))
                        return self.respond({"status": "queued"})
                    if method == "POST" and action == "retry":
                        self.body(4096)
                        need(job["status"] == "failed" and self.app.db.blob(job["id"], ".upload").exists(), 409, "Select the original file to upload again")
                        self.app.db.run("UPDATE jobs SET status='uploading',received=0,error=NULL,updated=? WHERE id=?", (time.time(), job["id"]))
                        self.app.db.blob(job["id"], ".upload").write_bytes(b"")
                        return self.respond({"status": "uploading", "received": 0})
            if path == "/v1/device/jobs" and method == "GET":
                owner = self.device()
                scope = parse_qs(parts.query, keep_blank_values=True)
                values = scope.get("exclude", [])
                need(len(values) <= 1, 400, "Duplicate delivery exclusion")
                excluded = values[0].split(",") if values else []
                need(not values or (1 <= len(excluded) <= 96 and len(set(excluded)) == len(excluded) and all(re.fullmatch(r"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}", x) for x in excluded)), 400, "Invalid delivery exclusions")
                sql = "SELECT * FROM jobs j WHERE device_id=? AND status IN ('waiting for device','transferring') AND expires>? AND NOT EXISTS (SELECT 1 FROM tombstones t WHERE t.device_id=j.device_id AND t.document_id=j.document_id)"
                if excluded:
                    sql += " AND id NOT IN (" + ",".join("?" for _ in excluded) + ")"
                sql += " ORDER BY created " + ("DESC,id DESC" if excluded else "ASC") + " LIMIT 8"
                jobs = self.app.db.rows(sql, (owner["device_id"], time.time(), *excluded))
                result = []
                for job in jobs:
                    ticket, expiry = self.app.ticket(job, owner["device_id"])
                    filename = str(Path(job["filename"]).with_suffix("." + job["format"]))
                    result.append({"job_id": job["id"], "document_id": job["document_id"], "filename": filename, "format": job["format"], "sha256": job["sha256"], "size": job["size"], "converted": bool(job["converted"]), "intentional_new": bool(job["intentional_new"]), "download_token": ticket, "download_expires_at": expiry})
                return self.respond({"version": 1, "jobs": result})
            match = re.fullmatch(r"/v1/device/jobs/([a-f0-9-]{36})/(content|ack)", path)
            if match:
                owner = self.device()
                with self.app.mutations:
                    job = self.job_owned(match[1], owner, True)
                    tombstone = self.app.db.one("SELECT 1 FROM tombstones WHERE device_id=? AND document_id=?", (owner["device_id"], job["document_id"]))
                    need(not tombstone, 409, "Document was deleted; retry cannot restore it", "tombstoned")
                    if method == "GET" and match[2] == "content":
                        need(job["status"] in ("waiting for device", "transferring") and job["expires"] > time.time(), 410, "Relay content unavailable")
                        self.app.check_ticket(self.headers.get("X-Download-Token"), job, owner["device_id"])
                        self.app.db.run("UPDATE jobs SET status='transferring',updated=? WHERE id=?", (time.time(), job["id"]))
                        file = self.app.db.blob(job["id"], ".ready").open("rb")
                    elif method == "POST" and match[2] == "ack":
                        data = self.body(4096)
                        need(job["status"] in ("waiting for device", "transferring", "imported"), 409, "Job cannot be acknowledged")
                        need(data.get("durable") is True and data.get("document_id") == job["document_id"] and data.get("sha256") == job["sha256"], 409, "Durable import identity/checksum acknowledgement required")
                        self.app.db.run("UPDATE jobs SET status='imported',updated=? WHERE id=?", (time.time(), job["id"]))
                        self.app.db.cleanup_blobs(job["id"])
                        return self.respond({"status": "imported", "job_id": job["id"], "document_id": job["document_id"]})
                    else:
                        raise APIError(405, "Method unavailable")
                return self.download(file, job)
            if path == "/v1/device/tombstones" and method == "POST":
                owner = self.device(); data = self.body(4096)
                ident = data.get("document_id")
                need(valid_id(ident), 400, "Document UUID required")
                with self.app.mutations:
                    self.app.db.run("INSERT OR IGNORE INTO tombstones VALUES(?,?,?)", (owner["device_id"], ident, time.time()))
                    for job in self.app.db.rows("SELECT id FROM jobs WHERE device_id=? AND document_id=?", (owner["device_id"], ident)):
                        self.app.db.run("UPDATE jobs SET status='deleted',updated=? WHERE id=?", (time.time(), job["id"]))
                        self.app.db.cleanup_blobs(job["id"])
                return self.respond({"recorded": True})
            if path.startswith("/v1/tom/"):
                return self.tom_route(method, path, query)
            if method == "GET" and path in ("/", "/app.js", "/hash-worker.js", "/sha256-stream.js", "/config.js", "/style.css", "/icon.svg", "/manifest.webmanifest"):
                filename = "index.html" if path == "/" else path[1:]
                raw = (ROOT / "web" / filename).read_bytes()
                mime = {"html": "text/html", "js": "text/javascript", "css": "text/css", "svg": "image/svg+xml", "webmanifest": "application/manifest+json"}[filename.rsplit(".", 1)[1]]
                self.send_response(200); self.headers_secure(); self.send_header("Content-Type", mime + "; charset=utf-8"); self.send_header("Content-Length", str(len(raw))); self.end_headers(); self.wfile.write(raw)
                return
            raise APIError(404, "Endpoint unavailable")
        except APIError as exc:
            self.close_connection = True
            self.respond({"error": exc.message, "code": exc.code}, exc.status)
        except (BrokenPipeError, ConnectionResetError, TimeoutError):
            self.close_connection = True
        except Exception:
            self.close_connection = True
            self.respond({"error": "Service operation failed; no success was recorded", "code": "internal_error"}, 500)

    def create_job(self, owner):
        data = self.body(4096)
        name = safe_name(data.get("filename"))
        fmt = name.rsplit(".", 1)[-1].lower()
        need(fmt in FORMATS, 415, "Supported formats: PDF, EPUB, DOCX, UTF-8 TXT and Markdown")
        size, checksum = data.get("size"), data.get("sha256")
        need(type(size) is int and 0 < size <= MAX_UPLOAD, 413, "File must be 1 byte–64 MiB")
        need(isinstance(checksum, str) and re.fullmatch("[a-f0-9]{64}", checksum), 400, "SHA-256 required")
        device = self.device_owned(data.get("device_id"), owner["user_id"])
        ident = data.get("upload_id", uid())
        need(valid_id(ident), 400, "Upload UUID required")
        with self.app.mutations, self.app.db.connect() as db:
            db.execute("BEGIN IMMEDIATE")
            existing = db.execute("SELECT * FROM jobs WHERE id=?", (ident,)).fetchone()
            if existing:
                need(existing["user_id"] == owner["user_id"] and existing["device_id"] == device["id"] and existing["source_sha256"] == checksum and existing["source_size"] == size, 409, "Upload ID conflict")
                return self.respond({"job_id": ident, "document_id": existing["document_id"], "status": existing["status"], "received": existing["received"], "duplicate": True})
            if data.get("new_import") is not True:
                old = db.execute("SELECT * FROM jobs WHERE device_id=? AND source_sha256=? AND status NOT IN ('canceled','expired') ORDER BY created DESC LIMIT 1", (device["id"], checksum)).fetchone()
                if old:
                    need(old["status"] != "deleted", 409, "Document was removed; explicitly choose Import another copy to create a new identity", "tombstoned")
                    return self.respond({"job_id": old["id"], "document_id": old["document_id"], "status": old["status"], "received": old["received"], "duplicate": True})
            active = db.execute("SELECT COUNT(*),COALESCE(SUM(source_size),0) FROM jobs WHERE user_id=? AND status NOT IN ('imported','canceled','expired','deleted')", (owner["user_id"],)).fetchone()
            need(active[0] < 100 and active[1] + size <= 512 * 1024 * 1024, 429, "Private relay quota reached; remove pending uploads")
            now, doc_id = time.time(), uid()
            fd = os.open(self.app.db.blob(ident, ".upload"), os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            os.close(fd)
            db.execute("INSERT INTO jobs(id,user_id,device_id,document_id,filename,source_format,source_sha256,source_size,status,created,updated,expires) VALUES(?,?,?,?,?,?,?,?,'uploading',?,?,?)", (ident, owner["user_id"], device["id"], doc_id, name, fmt, checksum, size, now, now, now + 7 * 86400))
            db.execute("UPDATE jobs SET intentional_new=? WHERE id=?", (int(data.get("new_import") is True), ident))
        return self.respond({"job_id": ident, "document_id": doc_id, "status": "uploading", "received": 0, "duplicate": False}, 201)

    def chunk(self, job):
        need(job["status"] == "uploading", 409, "Job is not accepting upload chunks")
        match = re.fullmatch(r"bytes (\d+)-(\d+)/(\d+)", self.headers.get("Content-Range", ""))
        need(match is not None, 400, "Content-Range required")
        start, end, total = map(int, match.groups())
        raw = self.raw_body(CHUNK)
        need(total == job["source_size"] and end < total and end >= start and end - start + 1 == len(raw), 409, "Chunk range/length mismatch")
        path = self.app.db.blob(job["id"], ".upload")
        if start < job["received"] and end < job["received"]:
            with path.open("rb") as f:
                f.seek(start)
                need(f.read(len(raw)) == raw, 409, "Retry chunk differs from durable bytes")
            return self.respond({"received": job["received"], "replayed": True})
        need(start == job["received"], 409, "Offset mismatch; fetch job and resume at received bytes")
        with path.open("r+b") as f:
            f.seek(start); f.write(raw); f.truncate(end + 1); f.flush(); os.fsync(f.fileno())
        self.app.db.run("UPDATE jobs SET received=?,updated=? WHERE id=?", (end + 1, time.time(), job["id"]))
        return self.respond({"received": end + 1, "replayed": False})

    def download(self, file, job):
        with file:
            size = job["size"]
            start, end, status = 0, size - 1, 200
            if self.headers.get("Range"):
                match = re.fullmatch(r"bytes=(\d+)-(\d*)", self.headers["Range"])
                need(match is not None, 416, "Only one forward byte range supported")
                start, end = int(match[1]), int(match[2]) if match[2] else size - 1
                need(0 <= start <= end < size, 416, "Invalid byte range")
                status = 206
            self.send_response(status); self.headers_secure()
            self.send_header("Content-Type", "application/pdf" if job["format"] == "pdf" else "application/epub+zip")
            self.send_header("Content-Disposition", 'attachment; filename="document.' + job["format"] + '"')
            self.send_header("Accept-Ranges", "bytes"); self.send_header("Content-Length", str(end - start + 1))
            self.send_header("ETag", '"' + job["sha256"] + '"')
            if status == 206: self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
            self.end_headers(); file.seek(start)
            remaining = end - start + 1
            while remaining:
                block = file.read(min(262144, remaining))
                if not block: break
                self.wfile.write(block); remaining -= len(block)

    def tom_route(self, method, path, query):
        owner = self.owner(method != "GET")
        if path == "/v1/tom/config" and method == "GET":
            return self.respond({"configured": bool(self.app.tom.api_key), "provider": {"id": "openai", "label": "OpenAI"}, "models": self.app.tom.models, "max_output_tokens": 2048, "daily_limit": 30})
        if path == "/v1/tom/requests" and method == "POST":
            try: result = self.app.tom.create(owner, self.body())
            except TomError as exc:
                raise APIError(503 if "unconfigured" in str(exc) else 409, str(exc), "tom_unavailable" if "unconfigured" in str(exc) else "tom_rejected")
            return self.respond(result, 202)
        if path == "/v1/tom/conversations" and method == "GET":
            args = [owner["user_id"]]
            extra = ""
            if owner.get("device_id"):
                extra = " AND device_id=?"; args.append(owner["device_id"])
            return self.respond({"conversations": self.app.db.rows("SELECT conversation_id,MAX(updated) AS updated,COUNT(*) AS turns FROM tom WHERE user_id=?" + extra + " GROUP BY conversation_id ORDER BY updated DESC LIMIT 100", args)})
        match = re.fullmatch(r"/v1/tom/conversations/([a-f0-9-]{36})", path)
        if match:
            rows = self.app.db.rows("SELECT * FROM tom WHERE conversation_id=? AND user_id=? ORDER BY created", (match[1], owner["user_id"]))
            if owner.get("device_id"):
                rows = [r for r in rows if r["device_id"] == owner["device_id"]]
            if method == "GET":
                return self.respond({"turns": [{"request_id": r["id"], "payload": json.loads(r["payload"]), "text": r["text"], "status": r["status"], "error": r["error"]} for r in rows]})
            if method == "DELETE":
                need(all(r["status"] not in ("queued", "streaming") for r in rows), 409, "Stop active requests before deleting conversation")
                with self.app.db.connect() as db:
                    for row in rows: db.execute("DELETE FROM tom WHERE id=?", (row["id"],))
                return self.respond({"deleted": True})
        match = re.fullmatch(r"/v1/tom/requests/([a-f0-9-]{36})(/(events|cancel))?", path)
        if match:
            row = self.app.db.one("SELECT * FROM tom WHERE id=? AND user_id=?", (match[1], owner["user_id"]))
            need(row and (not owner.get("device_id") or row["device_id"] == owner["device_id"]), 404, "Request unavailable")
            if method == "POST" and match[3] == "cancel":
                self.body(4096); self.app.tom.cancel(row["id"])
                return self.respond({"status": "canceled"})
            if method == "GET" and not match[3]:
                return self.respond({"request_id": row["id"], "status": row["status"], "text": row["text"], "error": row["error"]})
            if method == "GET" and match[3] == "events":
                try: after = int(query.get("after", ["0"])[0])
                except ValueError: raise APIError(400, "Invalid event cursor")
                need(after >= 0, 400, "Invalid cursor")
                self.send_response(200); self.headers_secure(); self.send_header("Content-Type", "text/event-stream"); self.send_header("Connection", "close"); self.send_header("X-Accel-Buffering", "no"); self.end_headers(); self.close_connection = True
                deadline = time.monotonic() + 25
                while time.monotonic() < deadline:
                    events = self.app.db.rows("SELECT * FROM tom_events WHERE request_id=? AND seq>? ORDER BY seq LIMIT 100", (row["id"], after))
                    for event in events:
                        self.wfile.write(f"id: {event['seq']}\nevent: {event['event']}\ndata: {event['data']}\n\n".encode()); self.wfile.flush(); after = event["seq"]
                    state = self.app.db.one("SELECT status FROM tom WHERE id=?", (row["id"],))
                    if not state or (state["status"] not in ("queued", "streaming") and not events): break
                    time.sleep(.15)
                return
        raise APIError(404, "Tom endpoint unavailable")

class Server(ThreadingHTTPServer):
    daemon_threads = True
    request_queue_size = 32
    def __init__(self, address, app):
        self.app = app
        self.slots = threading.BoundedSemaphore(32)
        super().__init__(address, Handler)

    def process_request(self, request, address):
        if not self.slots.acquire(blocking=False):
            request.close(); return
        try: super().process_request(request, address)
        except Exception:
            self.slots.release(); raise

    def process_request_thread(self, request, address):
        try: super().process_request_thread(request, address)
        finally: self.slots.release()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", default=os.environ.get("INKBOUND_DATA", "/var/lib/inkbound"))
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--origin", default=os.environ.get("INKBOUND_ORIGIN", "http://127.0.0.1:8765"))
    parser.add_argument("--dev", action="store_true")
    parser.add_argument("--create-user", metavar="EMAIL")
    args = parser.parse_args()
    os.umask(0o077)
    if args.create_user:
        db = Database(args.data)
        email = args.create_user.strip().lower()
        if not re.fullmatch(r"[^\s@]+@[^\s@]+\.[^\s@]+", email):
            parser.error("Valid account email required")
        password = getpass.getpass("New password (14+ characters): ")
        if password != getpass.getpass("Repeat password: "):
            parser.error("Passwords do not match")
        db.run("INSERT INTO users VALUES(?,?,?)", (uid(), email, password_hash(password)))
        print("Account created; password was not logged.")
        return
    models = tuple(x.strip() for x in os.environ.get("INKBOUND_TOM_MODELS", DEFAULT_MODEL).split(",") if x.strip())
    if not models or any(not re.fullmatch(r"[a-zA-Z0-9][a-zA-Z0-9_.-]{0,100}", m) for m in models):
        parser.error("Model allowlist invalid")
    app = App(args.data, args.origin, args.dev, os.environ.get("OPENAI_API_KEY", ""), models)
    server = Server((args.host, args.port), app)
    signal.signal(signal.SIGTERM, lambda *_: threading.Thread(target=server.shutdown, daemon=True).start())
    print("Inkbound Companion listening; protocol v1; request/content logging disabled.", flush=True)
    try: server.serve_forever()
    except KeyboardInterrupt: pass
    finally: server.server_close(); app.close()

if __name__ == "__main__": main()
