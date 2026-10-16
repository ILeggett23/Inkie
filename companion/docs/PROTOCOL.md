# Inkbound Companion protocol v1

This network contract is generic and contains no reader implementation. All IDs are lowercase canonical UUIDs; SHA-256 values are lowercase hexadecimal; timestamps are Unix seconds. JSON requests use `Content-Type: application/json`. Production traffic uses HTTPS only. Do not place tokens in URLs or logs. Schema: `protocol/v1.json`.

## Account and browser session

An administrator creates an account interactively (`python -m companion.server --data /private/runtime --create-user you@example.com`); public registration is disabled. Passwords use salted scrypt. `POST /v1/auth/login {email,password}` returns `{email,csrf}` and an HttpOnly, Secure, SameSite=Strict session cookie with a 12-hour lifetime. `GET /v1/me` returns the email, CSRF token, limits and Tom configuration. Every browser mutation requires the exact configured `Origin` and `X-CSRF-Token`. `POST /v1/auth/logout {}` revokes the current session. No tokens are stored in browser local storage.

GitHub Pages is a static entry point. Its public `config.js` can identify the separate HTTPS API service. Sign-in and private app activity open on that service's own origin, avoiding cross-site session failures on phones. GitHub Pages cannot host private uploads, queues, conversions or provider secrets.

## Device pairing

1. Reader sends `POST /v1/pair/start {name}` (no existing credential). Result 201: `{pairing_id,code,pairing_token,expires_at}`. Code has eight unambiguous letters/digits; all pairing credentials expire in five minutes.
2. Account browser sends `POST /v1/pair/claim {code}`. Result: `{pairing_id,state:"claimed",name,message}`. Codes are single claim only.
3. Reader polls `GET /v1/pair/{pairing_id}` using `Authorization: Bearer <pairing_token>`. Result: `{pairing_id,state,account,expires_at}`. Before claim, state is `pending` and account is null. After claim, state is `claimed` and account is the email that must be shown for approval.
4. After deliberate device approval, reader sends `POST /v1/pair/{pairing_id}/approve {approve:true}` with pairing bearer. Result 201: `{device_id,device_token,expires_at}`. Device credential lasts 90 days. Approval is single-use: if the result is lost, re-pair rather than guessing or replaying credential issuance. `{approve:false}` rejects the claim.
5. Browser `GET /v1/devices` lists `{devices:[{id,name,last_seen,expires,revoked}]}`. `DELETE /v1/devices/{id}` revokes immediately and cancels pending relay bytes. Imported local documents remain local.

## Private resumable upload

`POST /v1/jobs {device_id,filename,size,sha256,upload_id?,new_import?}` returns 201 `{job_id,document_id,status:"uploading",received:0,duplicate:false}`. `upload_id` is a client-generated UUID recommended for retrying creation. Original and delivered files are limited to 64 MiB. Filenames never select filesystem paths. Account pending quota is 100 jobs / 512 MiB. Supported source extensions are PDF, EPUB, DOCX, UTF-8 TXT and MD.

By default the same source checksum for a device returns the existing job/document identity (200, `duplicate:true`). This includes already imported content. `new_import:true` deliberately creates a new document identity; it must only follow a user's intentional new import, never an automatic network retry. If the previous identity is tombstoned, ordinary creation fails 409 with `code:tombstoned`.

`PUT /v1/jobs/{job_id}/chunk` uploads at most 1 MiB with `Content-Range: bytes START-END/TOTAL` and raw octet-stream body. A successful response `{received,replayed}` means those bytes passed fsync before metadata advanced. Exact repeat chunks are idempotent; differing repeats and gaps fail 409. On interruption fetch `GET /v1/jobs/{job_id}` and resume from `received`. `POST /v1/jobs/{job_id}/complete {}` queues validation only when all bytes arrived. Checksum validation/conversion happens off-request in a bounded separate process. `POST /v1/jobs/{job_id}/retry {}` resets a failed upload to zero only when its original private bytes remain; the client reselects and sends its original file. No automatic upload retry occurs on reconnect.

`GET /v1/jobs` returns `{jobs:[...]}` newest first, maximum 200, scoped to the account. Optional, at most once each: `?limit=N` (1–200) returns only the newest N, and `?ids=A,B,…` (1–32 distinct job UUIDs) returns only those jobs of this account, so the web app polls just its current upload batch instead of the whole history. Anything else is 400. `DELETE /v1/jobs/{id}` cancels pending relay delivery and removes its server bytes. It never deletes an imported local document (409 if already imported).

Statuses: `uploading` → `queued` → `processing` → `waiting for device` → `transferring` → `imported`. Alternatives are `failed`, `canceled`, `expired`, `deleted`. All queued processing is durable. After a service restart, processing jobs are requeued. A reader being asleep/offline is a normal waiting state; delivery is not promised during that period.

## Outbound delivery and acknowledgement

Use `Authorization: Bearer <device_token>` on all device requests. Reader sends `GET /v1/device/jobs`; response is the v1 manifest in `protocol/v1.json`, maximum eight entries. A reader that set aside rejected payloads may send `?exclude=A,B,…` (1–96 distinct job UUIDs, at most once): those jobs are left out of this response without being canceled, acknowledged or tombstoned, and the remaining entries come newest first so the latest upload is not held behind them. Without `exclude` the order stays oldest first. Entries contain `job_id`, `document_id`, `filename`, `format` (pdf/epub), post-conversion `sha256`, `size`, `converted`, `intentional_new` (true only for an explicitly requested new import), a five-minute `download_token`, and `download_expires_at`.

Fetch `GET /v1/device/jobs/{job_id}/content` with the same bearer and `X-Download-Token: <download_token>`. Single forward byte ranges use `Range: bytes=N-M`, 206 and `Content-Range`; full download is 200. `ETag` is the quoted SHA-256. Refresh an expired download ticket by polling the queue. A GET changes status to transferring; it is not import acknowledgement.

The receiver must never infer intentional-import permission from a fresh UUID. Only the authenticated manifest `intentional_new:true`, persisted from the browser’s deliberate `new_import:true`, may authorize a content-tombstone exception.

The receiver must download to private staging, preserve/resume a bounded partial, validate length and SHA-256, reject malicious/unsupported content, parse/open the supported document, and durably save bytes plus catalogue identity before ACK. Only then send `POST /v1/device/jobs/{job_id}/ack {document_id,sha256,durable:true}`. Result `{status:"imported",job_id,document_id}` is idempotent for the same identity/hash. The first accepted ACK removes relay byte copies. Stable document IDs and a local import ledger prevent duplicate books when an ACK response is lost. No local reading state, handwriting, or library listing is part of this contract.

Before removing a local imported identity permanently (including Trash policies that block old delivery), persist its tombstone locally and send `POST /v1/device/tombstones {document_id}` when online. Result `{recorded:true}` is idempotent. The server excludes that identity, removes its queued bytes, marks jobs deleted, and rejects old ACKs/downloads. Retain local tombstones across restarts and send them before polling old jobs. Intentional reimport gets a fresh identity; never erase tombstones merely to make a retry pass.

## Tom

Both account sessions and device bearers can call the Tom API. Device requests are restricted to that device; account access is scoped to its user. Server-side `OPENAI_API_KEY` is never returned. Default validated allowlist is `gpt-5.4-mini`; operators may configure exact model IDs and must verify account access. Provider calls are not made when unconfigured.

`POST /v1/tom/requests` requires `{request_id,conversation_id,model,question,scope,context,history,consent:true}`. Optional fields: `handwriting_image`, `page_image`, `purpose` (`answer`, `transcribe` or `read-answer`). Scope is `word`, `sentences`, `paragraphs`, `page`, or `general`. For selected scopes, context is `{document_id,revision,anchor,text,page_kind?}`. `revision` and JSON `anchor` freeze the source for the turn. Page scope requires `page_kind:pdf-source-page` or `epub-rendered-page`. General scope requires null context and forbids page imagery. No library/book attachment is inferred.

Question maximum 8,000 characters; selected text 16,000; combined input text 32,000; at most 20 explicit `{role:"user"|"assistant",text}` history messages; each image a data-URL PNG/JPEG no larger than 2 MiB; no remote image URL. Input imagery must be explicitly disclosed. `purpose:transcribe` requires handwriting and asks for a correctable transcription with `[unclear]`, not fabricated confidence. The original handwriting remains in the frozen request. `purpose:read-answer` (Cloudflare service only) takes the handwriting as the whole question (empty `question`, `handwriting_image` required; scope and context rules as for `answer`). The service reads it with the transcription prompt over the handwriting alone, publishes the result as one `event:question` + `data:{"text":"..."}` before any delta, then answers that text exactly as an `answer` request would, with the context and history and without the image. It is one request from the reader, counted as two of the daily attempts. Unreadable handwriting ends `interrupted` with no answer call. The service advertises it with the response header `X-Inkbound-Tom-Purposes: answer,transcribe,read-answer` on `GET /v1/tom/config`, whose body keys are unchanged; readers without that header use `transcribe` then `answer`. Reading/handwriting/unsent drafts must remain offline-capable on the client.

Result 202: `{request_id,status,replayed}`. Same request ID + identical body is idempotent and does not call the provider again. Reusing it with different content fails. A deliberate Retry uses a new request ID with a visible warning that it is a new billable operation. Limits: two simultaneous upstream calls globally, 30 requests/account/day, max 2,048 output tokens, 90-second request budget with 30-second socket timeout and bounded events/output. There is no automatic upstream retry.

`GET /v1/tom/requests/{id}` returns `{request_id,status,text,error}`. `GET /v1/tom/requests/{id}/events?after=N` streams SSE records with numeric `id`, `event:delta` + `data:{"text":"..."}`, `event:status` + `data:{"status":"..."}`, and for `read-answer` one `event:question` + `data:{"text":"..."}`. Each response may close after 25 seconds; reconnect only to this GET with the last applied sequence, never recreate the request. Stream deltas are persisted before publication. Server/client can replay records, so deduplicate by sequence. `POST /v1/tom/requests/{id}/cancel {}` sets canceled and closes upstream best-effort; partial answers remain, and incurred charges may still apply.

Final states are `completed`, `canceled`, `interrupted`; service restarts set unfinished requests to interrupted and never resubmit. `GET /v1/tom/conversations` lists `{conversations:[{conversation_id,updated,turns}]}`; `GET /v1/tom/conversations/{id}` returns frozen turns; `DELETE` permanently removes inactive owned turns. Stop an active request first. No tools, shell, file deletion or device controls are given to Tom. Source text is explicitly treated as untrusted quotations.

## Failure and retention

Errors: `{error:<safe human text>,code:<machine label>}`. Status codes: 400 malformed request, 401 absent/expired/revoked credential, 403 CSRF/origin/ticket denied, 404 unavailable owned object, 409 identity/state/checksum/offset/tombstone conflict, 410 expired delivery/ticket, 413 limit exceeded, 415 unsupported request/format, 429 rate/quota, 500 storage/service failure, 503 Tom unconfigured. Internal parser text/content is never logged or exposed. Native clients should pause/back off on unavailable networking and show explicit states; no device wake/suspend override is implied.

Private relay bytes are deleted after durable ACK, cancellation/revocation/tombstone, or seven days. Job identity/status and tombstones remain to support deduplication. Tom request/history persists until explicit deletion; the provider's retention policy is separate. Persistent storage and backups must be encrypted and accessible only to the service operator. Original phone files and the reader's stock library are outside every deletion operation.

### AI configuration and consent binding

Authenticated `GET /v1/tom/config` returns `configured`, `provider: {id,label}`, `models`, `max_output_tokens` and `daily_limit`. Show these before consent. Every new Tom POST must explicitly include the reviewed `provider` (`openai` or `workers-ai`), `model`, and integer `max_output_tokens` (16–2048), in addition to `consent:true`. These fields are immutable parts of the request fingerprint.

The server rejects a provider or output-limit mismatch before queueing or storing request imagery. The queue consumer checks them again before inference. A configuration change requires renewed review and consent; old requests never move silently to another provider or larger budget. Existing request replay still requires an identical fingerprint. Clients with the earlier unpinned request shape must refresh before sending; locally saved drafts remain private. The reference Python service advertises and accepts `openai` with a 2048-token cap. Production Cloudflare currently advertises Workers AI with a 128-token cap.

### Resumable Tom illustration content (reader 21c)

`GET /v1/tom/illustrations/{request_id}/content` retains the existing owner/device,
completed-parent, cancellation and expiry checks. With no `Range`, it returns the
whole PNG with status 200 for older readers. A single forward range
`Range: bytes=START-END` (or `bytes=START-`) returns status 206,
`Content-Range: bytes START-END/TOTAL`, and the exact partial `Content-Length`.
The end is clamped to EOF; suffix, multiple, reversed, unsafe and out-of-bounds
ranges return 416 with `Content-Range: bytes */TOTAL`.

Both responses advertise `Accept-Ranges: bytes`, the full sealed PNG's SHA-256 in
`X-Content-SHA256`, and the same quoted SHA-256 as `ETag`. `If-Match` must match
that full-image ETag, otherwise 412 is returned without image bytes. The R2 read
also binds to the object's storage ETag so a replacement cannot mix versions.
New images retain SHA-256 metadata at generation; pre-existing images compute
it from the bounded stored PNG without regeneration or a paid provider call.

Reader 21c fetches at most 256 KiB per picture or document request. Picture
pieces and their full-image binding survive helper exit and restart; only a
complete SHA-256-verified image is published. Transfer errors resume the same
illustration ID with backoff, while user cancellation removes its partial copy.
Ordinary HTTP requests use a 30-second inactivity timeout; resumable SSE keeps
its separate total command budget. Picture orchestration remains cancelable
and bounded at 420 seconds, including generation, transfers and retries.
