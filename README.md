# Inkie

Private archive of the Inkbound Reader, Companion, refresh investigation and dedicated Linux builder.

- `reader/`: exact 845-file 21b validation source, SHA256 manifest `e93355c6077d6a9355527fc9826fbdf4a5ee98da45b79cba8870ad96ec36e13c`.
- `companion/`: current Companion source; its 31 production inputs match deployed version `990ac5ed-92fa-475b-b652-abd20ab09fe8`.
- `docs/refresh-reverse-engineering/`: refresh investigation and evidence qualifications.
- `provenance/`: source manifests, original release records and builder configuration.
- `backup/`: standard-library snapshot/restore tools.

The software checks pass: 129 release invocations, complete ASan/UBSan, 25 UX and 30 semantic cases in both configurations. The original fresh run's obsolete palette-fixture failure is preserved separately from the successful test-only retry. Production app manifest: `471b5d004fdbf02c0f008ac29593717594bcdb128f18a8615852aa83198112d0`.

Visible acceptance is unfinished. A supervised candidate reached native input readiness; a separate earlier session restored Xochitl with one interrupted/unsaved contact. The user changed the active task to GitHub migration and VM retirement. Neither optical equality with Xochitl nor live AI-provider output is qualified. See `provenance/MIGRATION-HANDOFF.md`.

## Full builder backup

The [verified builder backup](https://github.com/ILeggett23/Inkie/releases/tag/inkbound-builder-backup-2026-10-15) is complete. All 126 encrypted packs match GitHub server SHA256. The encrypted manifest recovered correctly; 845 current source files and 30 app payload/manifest files were restored from downloaded packs with matching hashes. The provided restore utility passed on 257 actual remotely recovered source files. All 458,819 original project entries remained unchanged. See `provenance/BACKUP-VERIFIED.json` and `provenance/snapshot-index.json`. VM retirement is the next separately verified step.

The snapshot includes 382,673 project files and their original modes, ownership, timestamps, symbolic links and hard-link identity. Duplicate content is stored once in 4 MiB chunks, packed into standard gzip/tar files and encrypted with age. Authentication identity directories are excluded. The manifest and historical captures are encrypted; the private recovery key stays on the Mac at `~/.ssh/inkbound_tablet_ed25519` and is never uploaded.

## Recovery

Install the official `gh` and `age` clients and sign into the owning GitHub account. Keep the existing SSH recovery identity safe.

```sh
gh release download inkbound-builder-backup-2026-10-15 --repo ILeggett23/Inkie --pattern vm-manifest.jsonl.gz.age --output - | age --decrypt -i ~/.ssh/inkbound_tablet_ed25519 | gzip -d > vm-manifest.jsonl
python3 backup/restore_snapshot.py --manifest vm-manifest.jsonl --repo ILeggett23/Inkie --release inkbound-builder-backup-2026-10-15 --identity ~/.ssh/inkbound_tablet_ed25519 --destination /path/to/new-empty-folder
```

To recover only one tree, add `--only-prefix home/release21b-gate-r11`. The restore tool streams only the required packs and writes chunks directly to their file positions, so it does not require a second complete chunk-store copy. It verifies every restored file's SHA256. Ordinary duplicate files remain separate files; only original hard-link groups are linked. The tool refuses existing destination paths and does not follow restored symlinks while writing. Run inside a recreated Linux environment as root if original Linux UID/GID ownership must also be applied. Device-node metadata, if present, needs a separately reviewed restore.

The original builder uses `/home/zayway.guest` with `/home/zayway.linux` as an alias. `provenance/builder-lima.yaml` pins its base image and records no mounted Mac source directories. The VM's OS/SSH identities can be recreated; the encrypted release contains project files rather than a credential-bearing VM disk image.
