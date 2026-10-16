# Deployment and operations

## Selected topology

Cloudflare hosts the authenticated interface and API together at `https://inkbound-companion.bridgecrm-zayway.workers.dev`. D1, private R2, a Queue and an isolated Container are dedicated to this application. Existing unrelated account resources are unchanged. The private GitHub repository holds source; its Pages workflow is manual and not required for this deployment.

The browser uses same-origin HttpOnly, Secure, SameSite=Strict sessions. No cross-site token storage or relaxed cookie policy is required. The optional Pages entry may point to the verified service through public `INKBOUND_SERVICE_ORIGIN`; that URL is never a credential.

Tom's deployed provider is Cloudflare Workers AI, discovered through authenticated `GET /v1/tom/config`. The current allowlisted model is `@cf/meta/llama-4-scout-17b-16e-instruct`, with responses capped at 128 output tokens and at most 30 accepted requests per account per day. The optional OpenAI adapter remains available for separately configured deployments. Both the browser and native client disclose the actual provider before consent. Do not place provider credentials in frontend code or device configuration.

See `cloudflare/DEPLOYMENT-STATUS.md` for executed live checks and their limitations. Service account passwords are distributed separately through private operator-controlled channels and never appear in this repository.

## Standalone Python option

The included Dockerfile and `deploy/compose.yaml` run the reference service on an approved persistent Linux host. They were source-reviewed; Docker was unavailable locally, so image build/runtime qualification is a separate gate. The Python service is single-node and limited to 32 HTTP workers plus two upstream Tom requests; do not scale replicas against a shared SQLite volume. A TLS reverse proxy must preserve the exact configured Host and Origin, disable request/body logging for private paths, limit request bodies to 6 MiB (chunks are 1 MiB), bound concurrent clients, and avoid buffering Tom SSE. Bind the app only to its private proxy interface. `--host 0.0.0.0` is appropriate only inside that controlled container/service network.

Provide exact HTTPS `INKBOUND_ORIGIN`, private `INKBOUND_DATA`, optional `OPENAI_API_KEY`, and an audited comma-separated `INKBOUND_TOM_MODELS` allowlist. The provider endpoint is fixed to OpenAI Responses HTTPS; users cannot redirect it to arbitrary hosts. Credentials stay in runtime secret management and are stripped from conversion-worker environments. The application sets Secure cookies and HSTS in production; it does not trust forwarded headers to downgrade security.

Create the account from an authorized operator shell using the interactive `--create-user` command. Registration is disabled. Lost-password recovery requires an authorized operator to reset the salted hash and revoke that user's existing sessions; there is no insecure browser reset shortcut. Device credentials expire in 90 days and can be revoked from Readers; re-pair when required.

`GET /healthz` verifies SQLite access. Configured exact Host is required. Database schema migrations run transactionally at startup (current version 2); unknown versions fail closed. Before upgrades, stop the service cleanly and back up its private runtime. Downgrading code across a schema change requires restoring the matching backup, never changing `user_version` manually.

## Backups and deletion

For a consistent standalone backup, stop the service and copy the entire private runtime directory (database plus WAL files, private relay bytes, signing key) to encrypted operator-controlled backup storage. Preserve mode 0700 directories/0600 files. Do not store backups in source control, GitHub artifacts or public object storage. Restore into an empty private directory using the same release schema, check `/healthz`, then test a synthetic upload/ACK before use. Backups contain private documents and conversation content; set and disclose their retention separately from the live seven-day relay policy.

Live bytes expire after seven days and are removed immediately after confirmed reader ACK, cancellation, revocation or tombstone. The standalone reference sweeps every minute; the deployed Cloudflare Worker sweeps every ten minutes and retries persisted cleanup intent. Retain identity/status/tombstone records for idempotency. Tom conversations persist until explicitly deleted, bounded to 64 MiB per account; there is no silent history eviction. Removing private backups later requires the operator's own retention procedure.

## Required live checks

Verify TLS/exact-origin sign-in, private storage permissions, durable restart after upload chunks, account isolation, reader pairing/approval, range resume, post-validation durable ACK, relay cleanup, tombstones/revocation, and offline waiting. Confirm device is awake; networking must not override its suspend policy. For a future provider change, perform only a separately authorized capped provider request after access is configured; observe actual deltas/cancel/history and record provider results separately from synthetic SSE tests. Verify no API key appears in frontend assets, browser storage, console logs, URLs or process output.

The production image/Cloudflare deployment, public network delivery, real provider behavior, API costs and actual physical reader behavior remain separate gates from the local test suite. See the integration task's local acceptance record for device-specific outcomes; no such evidence belongs in this repository.

## Primary references used

- OpenAI streaming: https://developers.openai.com/api/docs/guides/streaming-responses
- OpenAI images: https://developers.openai.com/api/docs/guides/images-vision
- Configured model documentation: https://developers.openai.com/api/docs/models/gpt-5.4-mini
- pypdf validation/security: https://pypdf.readthedocs.io/en/stable/modules/PdfReader.html
- GitHub Pages workflow and private-repository eligibility: https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages

Documentation was consulted on 2026-09-13. Provider/account access was not inferred from documentation.
