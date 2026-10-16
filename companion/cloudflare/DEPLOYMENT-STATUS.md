# Cloudflare deployment evidence — 2026-09-19

Service: **https://inkbound-companion.bridgecrm-zayway.workers.dev**

The interface and authenticated API run together on Cloudflare. The converter is reachable only through its service binding; public Worker and preview routes are disabled. R2's public development URL is disabled and no bucket custom domain exists. Dedicated D1/R2/Queue/Worker/Container resources were created after verifying their names were unused. Existing unrelated resources were not modified and no subscription was changed.

## Recorded versions and limits

- Main Worker: `25172cd0-cc83-45ee-a88b-7146224b59ac`.
- Converter Worker: `975ed176-37d4-45a4-bc39-82f2e68b5097`.
- Converter image digest: `sha256:8019033170519aa3d2c542b6a985d83c3eb68e25cef18814e635ab0433d8878b` (`linux/amd64`).
- Converter: at most two basic instances; ten-second idle sleep; outbound Internet disabled; non-root process.
- Relay retention: seven days from creation; post-ACK/cancel/revoke/tombstone cleanup; ten-minute scheduled sweep.
- Tom: Cloudflare Workers AI, `@cf/meta/llama-4-scout-17b-16e-instruct`, at most 128 output tokens per request, 30 accepted attempts/account/day and two active requests/account. These application limits are not a guaranteed currency spending cap.

## Executed live verification

Exactly **ten** synthetic conversion attempts were used, including three diagnostic failures. No personal documents were uploaded.

1. HTTPS sign-in, secure session flags, authenticated reads, unauthorized access, CSRF and exact-origin rejection passed.
2. Pairing, explicit device approval, replay refusal, account/device isolation and revocation passed.
3. PDF, EPUB, DOCX, TXT and Markdown passed actual queue conversion/delivery, split upload retry/resume, range reads, output checksums, acknowledgement validation/replay and deletion tombstones.
4. A malformed PDF was rejected; canceled uploads were retired.
5. A generic synthetic receiver parsed the received EPUB, fsynced the source file and directory, then acknowledged durable import successfully. This is not evidence of physical-device delivery.
6. One synthetic pending job retained the configured seven-day expiry interval, then an operator moved only that fixture's expiry into the past. The actual scheduled Worker sweep marked it expired and completed cleanup.
7. Chromium at 390, 768 and 1280 pixels passed authenticated navigation, no document overflow, local draft reload and sign-out.
8. Temporary accounts and their jobs/devices/conversations were removed. Eleven known synthetic R2 object keys were independently confirmed absent. The real owner's account was created separately and authenticated successfully; its credentials were delivered in a private local file, never source control.

## Streaming defects fixed from live evidence

The first cloud conversion exposed a request framing defect: manually setting Content-Length on an arbitrary Worker stream does not preserve a fixed-length HTTP body. The next exposed that the container service response lost the length metadata required by R2. Both boundaries now use `FixedLengthStream`. The local test bridge now preserves actual streaming across workerd HTTP and an identity-transformed service response, reproducing both failures before the fixes and passing afterward. Input buffering was not substituted for bounded streaming.

## AI evidence and limitation

Exactly **two** tiny synthetic inference requests were attempted, each capped at 128 output tokens. No OpenAI calls or credentials were used.

- A numeral-only text prompt reached inference but failed strict event normalization. Native finite scalar responses are now normalized safely; object/array values still fail closed. The repair passed local regression tests, and that live request was not retried.
- The vision request completed with genuine persisted SSE text and transcribed the handwritten `HI` fixture as `H I`. The exact-literal assertion failed on spacing and remains recorded unchanged. Real vision inference and streaming are established; exact transcription/general handwriting accuracy are not. The interpretation is shown for correction before the next answer request.

The deployed provider identity and model are exposed through authenticated `/v1/tom/config`. Browser/native consent names the actual provider. Queued requests retain provider identity and reject a provider change; provider cancellation receives an abort signal. General questions do not silently attach document context, and handwriting transcription omits unrelated history.

## Separate qualification boundaries

Remote CI, cloud service behavior, native SDK tests and physical-device tests are separate gates. This document establishes the cloud-side results above. It does not establish optical latency, sleep/exit behavior, real pen/eraser feel or general recognition accuracy. Ordinary model responses remain fallible and may reach the configured output limit; partial answers and explicit retry states are preserved.

Primary references: [Worker stream lengths](https://developers.cloudflare.com/workers/runtime-apis/request/#set-the-content-length-header), [Workers AI binding](https://developers.cloudflare.com/workers-ai/configuration/bindings/), [model and vision schema](https://developers.cloudflare.com/workers-ai/models/llama-4-scout-17b-16e-instruct/), [official stream mapper](https://github.com/cloudflare/ai/blob/main/packages/workers-ai-provider/src/streaming.ts).

Provider consent binding was hardened in the final deployment: requests pin provider/model/output limit, rejected on changes both at POST and before inference. Fourteen local Cloudflare groups and29 Python/boundary tests passed. This security redeploy used no additional conversion or AI call.

## Tom provider switch — 2026-10-02

Tom moved from Workers AI to **OpenAI** (`TOM_PROVIDER=openai`, allowlist `gpt-5.4-mini`, 800 output tokens per request; the 30 accepted attempts/account/day and two active requests limits are unchanged). The owner set `OPENAI_API_KEY` interactively with `wrangler secret put --env production`; the value is not in source. Production Worker version `6ef054ce-726f-4de5-8d97-4f7904082572` (previous: `25172cd0-cc83-45ee-a88b-7146224b59ac`, then the secret-change version `5fb2fa66-6602-4705-a1df-0a8dfded17f5`). `/healthz` reports `tom_configured: true`; `/v1/tom/config` still requires sign-in. No OpenAI request has been made yet; the first live request will come from the owner.


## Inkbound20s illustration service —2026-10-09

Direct user approved exact deployment proposal e971d95cf56ff796c2d09d22942d6eb73e0730718987aa13f50a9a14a526ae76 in the Codex takeover chat. Fresh preflight verified current production baseline185b3269-4db7-465e-93dd-f6f39f7a5cb6 (Oct6), existing resource IDs and only0004_tom_illustrations.sql pending. Applied that additive table/index migration only, then deployed via existing Wrangler4.131.1 --env production --keep-vars. Current version cfd06444-f31c-46e9-ba2e-3f9aae6505b6. No static assets changed. Existing server-managed secrets remained opaque and unchanged.

Exact33publicsource manifest8af7d68e5e87cfecb884c7e276b60bd60414d71b2e2a4fe6458c1bc413e3bc93; original public source backed up before integration.27local workerd/module tests and production dryrun passed with mocked providers; declarations regenerated with installed Wrangler. Correct configurable routing is OpenAI gpt-5.4-mini for reasoning/handwriting reading and factual structure, OpenAI gpt-image-2.5-flare through /v1/images/generations for explicit artistic illustrations. Strict model/consent/owner/cancel/idempotency/retention remain; failure retains prompt and explicit Retry with no text-model rendering fallback. Development metadata logs disabled in production.

Postdeployment current version/resource/model metadata and no pending migration verified. Public /healthz HTTP200 via systemcurl with valid TLS: ok/conversion_configured/tom_configured true. Initial hostPythonurllib HTTP403 retained as a failed diagnostic; no authentication bypass or production repair was needed. Existing users/devices/jobs/tom aggregate counts unchanged between authorized read-only checks; illustrations0. No live provider inference/image generation, private reading upload, credential operation or tablet action. Model entitlement, image/output quality, paired end-to-end behavior and actual device latency/optical results remain NOT_RUN.

Deployment records, exact manifest, backups and approved plan are in /Users/zayway/Documents/Codex/2026-10-07/you-re-taking-over-the-inkbound/work/tom-fixes20s and outputs/20s-COMPANION-DEPLOYMENT-RESULT.json. Code rollback baseline is185b3269; resources/data do not roll back with Worker code. Preserve additive table/data and image cleanup for any already accepted requests; no table/private object deletion or data restoration is included. Tablet20s build/install remains separate.
