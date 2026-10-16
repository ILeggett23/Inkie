# Live deployment

The companion interface and API are deployed together at **https://inkbound-companion.bridgecrm-zayway.workers.dev** on the existing Cloudflare account. The private GitHub repository stores source; Pages publishing is optional and manual.

Private document delivery and all five supported formats passed live synthetic verification. Tom uses **Cloudflare Workers AI** with the allowlisted `@cf/meta/llama-4-scout-17b-16e-instruct` model and a 128-output-token cap. It requires consent and does not need an OpenAI API key. Real vision inference and persisted streaming were observed; the fixture `HI` was interpreted as `H I`, so correction remains part of the workflow. This is not a general handwriting-accuracy qualification. See [Cloudflare deployment evidence](cloudflare/DEPLOYMENT-STATUS.md).

# Inkbound Reader Companion

A private document relay and optional Tom AI service, with a responsive phone/tablet/desktop interface. This repository contains only the companion service, generic network contract, original UI, and synthetic tests. The reader's source, runtime, recovery system, personal documents and device evidence remain outside this repository.

## What runs

- Sign-in with administrator-created accounts, private sessions and CSRF protection.
- Reader pairing with a five-minute code, browser claim and explicit reader approval; device revocation.
- Multi-file upload, resumable durable chunks, checksum validation, private queue, delivery range requests, and idempotent durable reader ACK. “Imported” is never set by upload alone.
- PDF and EPUB validation. DOCX, UTF-8 TXT and basic Markdown conversion into self-contained EPUB in bounded separate processes. DOCX preserves paragraphs, headings, emphasis, tables and embedded PNG/JPEG. Advanced layout, fields, macros, external resources, unsupported image formats and DRM are rejected explicitly.
- Stable document/job IDs, duplicate detection, intentional reimport, deletion tombstones, seven-day relay retention and byte deletion after ACK.
- Cloudflare Workers AI plus an optional server-side OpenAI Responses API adapter: selected frozen context, explicit per-request consent, handwriting image transcription, bounded genuine SSE deltas, persisted partials/history, cancellation and explicit retry. Authorized usage is required; only the optional OpenAI adapter requires a separate OpenAI API key.

`companion/` is the runnable Python/SQLite reference and standalone service. `cloudflare/` is the Cloudflare deployment implementation; consult its own README for its current tested status and prerequisites. `web/` is shared original frontend source. `protocol/v1.json` and `docs/PROTOCOL.md` define the boundary.

## Local setup

Use Python 3.12 (3.11+ required) and Node only for optional browser tests. Do not use a laptop as a production relay; local mode is for development/integration.

```sh
python3.12 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
.venv/bin/python -m companion.server --data ./work/private-runtime --create-user you@example.com
.venv/bin/python -m companion.server --data ./work/private-runtime --origin http://127.0.0.1:8765 --dev
```

Open `http://127.0.0.1:8765` locally. Account creation asks for a password interactively; do not put it in command arguments. `--dev` allows an HTTP-only local cookie and must never be used for production. Provider keys, if configured, enter only through the service environment/secret manager. `.env.example` contains placeholders and is not automatically loaded.

## Automated verification

```sh
.venv/bin/python -m unittest -v tests.test_integration tests.test_boundary
.venv/bin/python scripts/check_dependencies.py
.venv/bin/python scripts/audit_boundary.py --untracked-source
node --check web/app.js
```

The HTTP suite uses real temporary SQLite/private storage, synthetic documents and the actual validation/conversion subprocess. Provider simulations are explicitly separated in the `SimulatedProviderHTTP` tests and do not establish real OpenAI behavior or access. `tests/serve_fixture.py` starts a private synthetic server for an independently implemented receiver; its runtime credential output must never be committed.

Browser verification:

```sh
mkdir -p work
.venv/bin/python -m tests.serve_fixture --data ./work/browser-runtime --out ./work/browser-fixture.json
# In a second terminal, with Playwright and its Chromium installed:
INKBOUND_FIXTURE=./work/browser-fixture.json node tests/browser.cjs
```

The browser suite checks sign-in, navigation, no horizontal document overflow, draft preservation, real upload/conversion and sign-out at 390, 768 and 1280 pixels. Screenshots and runtime credentials stay under ignored `work/`.

## Publishing and deployment

The deployed Cloudflare Worker serves both `web/` and the authenticated API. GitHub stores source; its optional manual Pages workflow serves only static `web/`. Configure repository variable `INKBOUND_SERVICE_ORIGIN` to the deployed HTTPS companion origin; the Pages entry opens the private service on its own origin, so phone browsers can use secure session cookies reliably. Without that variable, Pages clearly shows that the backend is unavailable. Never put provider keys into Pages variables or assets.

Current deployment target is Cloudflare, following the user's existing account preference. No paid resource or provider call is activated by these source files. Actual resources, account authorization, API credentials, an operational converter, and deployment verification must be completed before claiming end-to-end network readiness. The standalone Docker configuration remains an alternative on an approved persistent host; see `docs/DEPLOYMENT.md`.

The Pages workflow uploads **only `web/`**, not repository root, tests, private storage, source packages or evidence. Before the first push, stage explicit paths, review `git diff --cached`, run the boundary/history audit and inspect tracked files. Never add a symlink or submodule to local reader code. Check private-repository Pages eligibility in the actual GitHub account; do not make the repository public merely to enable Pages.

See `SECURITY.md`, `NOTICE.md`, `WORKLOG.md` and `docs/PROTOCOL.md` for limits, provenance and observed checks.
