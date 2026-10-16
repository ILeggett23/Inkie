# Cloudflare companion service

This directory implements the private companion HTTPS application/API using a Worker, D1 metadata, private R2 uploads, and a durable Queue. A separate service binding reaches the isolated Python converter through Cloudflare Containers. The functional UI and API share the deployed Cloudflare origin. GitHub Pages is optional and its workflow is manual. Provider credentials never enter Pages or frontend assets.

## Verified status

The interface/API is live at **https://inkbound-companion.bridgecrm-zayway.workers.dev**. Dedicated D1 migrations, private R2 access, queue conversion and authenticated delivery were verified over real HTTPS. All five formats passed content/range/checksum/ACK/tombstone checks. A separate generic receiver validated an EPUB and fsynced both file and directory before ACK. Actual scheduled retention cleanup was observed on one synthetic past-expiry fixture. Temporary verification accounts/data were removed.

Tom is enabled through Cloudflare Workers AI, using `@cf/meta/llama-4-scout-17b-16e-instruct` with a 128-output-token cap. No OpenAI key is needed for this configuration. Two tiny synthetic AI requests were attempted: the first exposed a scalar stream-normalization defect, fixed and tested locally; the second completed with genuine persisted SSE and returned `H I` for the handwritten `HI` fixture. The exact-literal assertion remains a recorded failure; a correctable interpretation is supported, and general handwriting accuracy is unqualified. No additional live inference retries were performed.

The local Worker suite passed 13 grouped tests. Subsequent scalar/complex-value regression checks passed separately. The reference service/boundary suite passed 29 tests. Local ARM64 and GitHub AMD64 converter images passed their ten-case smoke tests. Live Chromium checks at 390, 768 and 1280 pixels passed authentication, layout, draft retention and sign-out. These results do not establish physical-device behavior. See [deployment evidence](DEPLOYMENT-STATUS.md).

## Reproduce locally

From the repository root, use Python 3.12 and Node.js 24:

```sh
python3.12 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
cd cloudflare
npm ci
npm test
WRANGLER_SEND_METRICS=false npm run types
WRANGLER_SEND_METRICS=false npx wrangler deploy --dry-run --env '' --outdir work/dry-run
```

The test suite creates its own synthetic users and private temporary D1/R2 data, runs every ordered SQL migration, starts a loopback-only converter, and removes temporary storage afterward. Loopback listening must be allowed. No Cloudflare login is required. `npm run dev` alone leaves conversion/Tom unavailable until their local bindings/secrets are configured; an unavailable converter is reported as failed, never delivered.

To test the converter image, build from the repository root so the reviewed conversion code and pinned dependencies are included:

```sh
docker build -f cloudflare/converter/Dockerfile -t inkbound-converter:local .
```

The actual ARM64 image passed all ten synthetic smoke cases in an isolated local Docker runtime on 2026-09-19. The Linux/AMD64 image also passed its ten-case CI smoke test and is deployed to Cloudflare; live conversion was verified separately. The image build/runtime is a separate gate from Worker bundling and direct Python adapter tests. `--containers-rollout=none` does not qualify a container image.

## Deliberate production setup

Do not run these steps until the selected account, resource charges and capped provider usage are approved.

1. Confirm the intended Cloudflare account. Create a dedicated private R2 bucket, D1 database, and Queue matching `wrangler.jsonc`; keep R2 public access disabled. Confirm Workers/Queues/Containers availability and charges for that account.
2. Set the production D1 database ID and the exact verified HTTPS `APP_ORIGIN` in `env.production`. Set resource names/bindings consistently in the production environment. Keep `ALLOW_LOCAL_HTTP=false`.
3. Back up the existing D1 database and private R2 data under the operator's encrypted retention policy before applying migrations. Apply **all** ordered migration files with `npx wrangler d1 migrations apply inkbound-companion --remote --env production`. Migration 0002 adds durable Tom conversation deletion markers; do not skip it on an existing database.
4. Generate a random signing secret of at least 32 bytes and set `DOWNLOAD_SECRET` using the interactive `npx wrangler secret put DOWNLOAD_SECRET --env production` command. Never put secret values in shell arguments or source files.
5. Prepare an account with `python3 create-user.py your-email --output work/create-user.sql` from this directory. It prompts securely and creates a private, ignored SQL file. Apply it deliberately with `npx wrangler d1 execute inkbound-companion --remote --env production --file work/create-user.sql`; then remove that private file under the operator's data policy. Browser registration is disabled.
6. Build and qualify the converter image, then deploy its separate Worker/Container from `converter/`. Its Worker has public routes and preview URLs disabled; its Container disables outbound Internet access, runs as a non-root user, and validates bounded input through a subprocess with resource/time limits. Connect only the `CONVERTER` service binding. Enable `CONVERSION_ENABLED=true` in the main production Worker only after actual conversion is verified.
7. For Workers AI, configure the `AI` binding, `TOM_PROVIDER=workers-ai`, the exact allowed `TOM_MODELS`, `TOM_MAX_OUTPUT_TOKENS`, and `TOM_ENABLED=true` after qualification. For optional OpenAI use, select `TOM_PROVIDER=openai`, set `OPENAI_API_KEY` interactively, and use an available OpenAI model allowlist. The application limits each account to 30 accepted request attempts per day, two active requests, bounded text/images and output tokens. These are application controls, not a guaranteed currency spending cap; configure provider controls and a separately approved test budget.
8. Dry-run and deploy the main Worker explicitly with `--env production`. Verify `/healthz`, exact-origin HTTPS sign-in, pairing, upload, conversion, resumable delivery, durable ACK cleanup, tombstones, and a capped real Tom request. `/healthz` confirms D1 access and reports configured flags; it does not prove the converter/provider works.
9. The Cloudflare deployment serves its own interface. If optional Pages publishing is deliberately enabled later, set public `INKBOUND_SERVICE_ORIGIN` to the verified service URL. That value is never an API key.

## Durability, deletion and recovery

Uploads expire seven days after creation. ACK, cancellation, revocation and document tombstones initiate immediate relay-object cleanup. The scheduled sweep runs every ten minutes and retries persisted cleanup intent and outbox work. In-flight rejected writes are also cleaned so cancellation cannot leave late private chunks behind. Stable identity/status/tombstone records prevent replay from restoring deleted content.

Tom conversations retain original request images and actual response text until explicit deletion, bounded by the per-account storage policy. History is read in pages with one payload streamed at a time. Conversation deletion atomically refuses active requests, records a durable deletion marker, and cleans all payloads in bounded pages. If R2 deletion fails, pending deletion rows remain and the scheduled sweep resumes; a deleted conversation ID cannot accept delayed new turns. The user can start a new conversation with a new ID. Backups are separate private copies and require an explicit operator retention/deletion policy.

For a consistent service backup, pause new writes and consumers, export the D1 database, and back up the private R2 bucket and secret configuration to encrypted operator storage. Restore matching schema/code and objects together, then run synthetic delivery/ACK and deletion checks before re-enabling users. Do not place private exports, uploaded documents, conversations, secrets or logs in this repository or CI artifacts.

## Primary implementation references

- [Cloudflare Workers best practices](https://developers.cloudflare.com/workers/best-practices/workers-best-practices/)
- [Wrangler commands](https://developers.cloudflare.com/workers/wrangler/commands/)
- [Private R2 Worker access](https://developers.cloudflare.com/r2/api/workers/workers-api-reference/)
- [D1 migrations](https://developers.cloudflare.com/d1/reference/migrations/)
- [Cloudflare Containers](https://developers.cloudflare.com/containers/)
