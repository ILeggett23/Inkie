"""Stream one standard gzip/tar content pack from the pinned VM; no VM writes."""
import gzip, hashlib, io, json, os, sys, tarfile
from pathlib import Path, PurePosixPath
assert Path('/etc/machine-id').read_text().strip() == '622e1d96c4974ce09dc3c5aca09f88e2'
spec = json.load(sys.stdin)
assert 0 < len(spec) <= 200000
seen = set()
with gzip.GzipFile(fileobj=sys.stdout.buffer, mode='wb', compresslevel=3, mtime=0) as compressed:
    with tarfile.open(fileobj=compressed, mode='w|', format=tarfile.USTAR_FORMAT) as archive:
        for c in spec:
            p = Path(c['source']); digest = c['sha256']
            assert str(p).startswith(('/home/zayway.guest/', '/tmp/'))
            assert '..' not in p.parts and '.ssh' not in p.parts and p.name != '.git-credentials'
            assert len(digest) == 64 and all(v in '0123456789abcdef' for v in digest) and digest not in seen
            assert 0 < c['size'] <= 4*1024*1024 and c['offset'] >= 0
            fd = os.open(p, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC)
            with os.fdopen(fd, 'rb', buffering=0) as src:
                src.seek(c['offset']); data = src.read(c['size'])
            assert len(data) == c['size'] and hashlib.sha256(data).hexdigest() == digest, 'Snapshot chunk changed: ' + str(p)
            item = tarfile.TarInfo('chunks/' + digest)
            item.mode = 0o600; item.size = len(data); item.uid = item.gid = item.mtime = 0
            archive.addfile(item, io.BytesIO(data)); seen.add(digest)
