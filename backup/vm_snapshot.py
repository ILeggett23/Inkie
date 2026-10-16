"""Read-only VM snapshot manifest with deduplicated 4 MiB content chunks.

Run in the pinned builder via sudo; stdout is private JSONL, stderr is progress.
No files are removed or written in the VM. SSH identities are excluded.
"""
import hashlib, json, os, stat, sys, time
from pathlib import Path

HOME = Path('/home/zayway.guest')
CHUNK = 4 * 1024 * 1024
assert Path('/etc/machine-id').read_text().strip() == '622e1d96c4974ce09dc3c5aca09f88e2'
assert not HOME.is_symlink()
excluded = []
roots = [('home', HOME)]
for p in sorted(Path('/tmp').iterdir()):
    s = p.lstat()
    runtime = p.name.startswith(('ssh-', 'systemd-', '.X11', '.ICE', '.font-', '.XIM', 'snap-private-', 'inkie-migration-'))
    project = s.st_uid == 501 or p.name.startswith(('reader-', 'inkbound-', 'interaction20', 'tom20', 'e2-', 'e2tools', 'rd', 'structured-selection'))
    if project and not runtime and not stat.S_ISSOCK(s.st_mode):
        roots.append(('tmp/' + p.name, p))
    else:
        excluded.append({'path': str(p), 'reason': 'OS_runtime_or_non_project_temporary_entry'})

def emit(v):
    sys.stdout.write(json.dumps(v, separators=(',', ':')) + '\n')

emit({'type': 'header', 'version': 1, 'chunk_size': CHUNK,
      'machine_id': '622e1d96c4974ce09dc3c5aca09f88e2',
      'roots': {name: str(path) for name, path in roots},
      'guest_home_alias': {'/home/zayway.linux': '/home/zayway.guest'},
      'excluded': excluded, 'privacy': 'Encrypt manifest and all content packs before upload',
      'excluded_identity_names': ['.ssh', '.git-credentials']})
seen = {}
hardlinks = {}
files = dirs = links = logical = allocated = unique_bytes = 0
next_progress = 2 * 1024**3
started = time.monotonic()

def same(a, b):
    return (a.st_dev, a.st_ino, a.st_size, a.st_mtime_ns, a.st_ctime_ns) == (b.st_dev, b.st_ino, b.st_size, b.st_mtime_ns, b.st_ctime_ns)

def walk(path, name):
    global files, dirs, links, logical, allocated, unique_bytes, next_progress
    s = path.lstat()
    if path.name in {'.ssh', '.git-credentials'}:
        emit({'type': 'excluded', 'path': name, 'reason': 'authentication_identity'})
        return
    e = {'type': 'entry', 'path': name, 'mode': stat.S_IMODE(s.st_mode),
         'uid': s.st_uid, 'gid': s.st_gid, 'mtime_ns': s.st_mtime_ns,
         'ctime_ns': s.st_ctime_ns, 'dev': s.st_dev, 'ino': s.st_ino, 'nlink': s.st_nlink}
    if stat.S_ISDIR(s.st_mode):
        dirs += 1; e['kind'] = 'dir'; emit(e)
        for child in sorted(path.iterdir()):
            walk(child, name + '/' + child.name)
        assert same(s, path.lstat()), 'Directory changed during snapshot: ' + name
    elif stat.S_ISLNK(s.st_mode):
        links += 1; e.update(kind='symlink', target=os.readlink(path)); emit(e)
    elif stat.S_ISREG(s.st_mode):
        files += 1; logical += s.st_size; allocated += s.st_blocks * 512
        group = (s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns, s.st_ctime_ns)
        if s.st_nlink > 1 and group in hardlinks:
            digest, chunks = hardlinks[group]
        else:
            h = hashlib.sha256(); chunks = []; offset = 0
            fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC)
            with os.fdopen(fd, 'rb', buffering=0) as src:
                assert same(s, os.fstat(src.fileno())), 'Source changed before read: ' + name
                while True:
                    data = src.read(CHUNK)
                    if not data: break
                    h.update(data); digest = hashlib.sha256(data).hexdigest()
                    chunks.append([digest, len(data)])
                    if digest not in seen:
                        seen[digest] = len(data); unique_bytes += len(data)
                        emit({'type': 'chunk', 'sha256': digest, 'size': len(data), 'path': name, 'offset': offset})
                    else:
                        assert seen[digest] == len(data)
                    offset += len(data)
                assert offset == s.st_size and same(s, os.fstat(src.fileno())), 'Source changed during read: ' + name
            digest = h.hexdigest()
            if s.st_nlink > 1: hardlinks[group] = (digest, chunks)
        e.update(kind='file', size=s.st_size, sha256=digest, chunks=chunks); emit(e)
        if logical >= next_progress:
            print(json.dumps({'files': files, 'logical_gib': round(logical/1024**3, 2),
                              'unique_gib': round(unique_bytes/1024**3, 2),
                              'unique_chunks': len(seen), 'elapsed_seconds': round(time.monotonic()-started)}), file=sys.stderr, flush=True)
            next_progress = logical + 2*1024**3
    elif stat.S_ISFIFO(s.st_mode):
        e['kind'] = 'fifo'; emit(e)
    elif stat.S_ISSOCK(s.st_mode):
        emit({'type': 'excluded', 'path': name, 'reason': 'nonpersistent_runtime_socket'})
    elif stat.S_ISCHR(s.st_mode) or stat.S_ISBLK(s.st_mode):
        e.update(kind='character' if stat.S_ISCHR(s.st_mode) else 'block', major=os.major(s.st_rdev), minor=os.minor(s.st_rdev)); emit(e)
    else:
        raise RuntimeError('Unclassified file type: ' + name)

for name, path in roots: walk(path, name)
summary = {'type': 'summary', 'complete': True, 'files': files, 'directories': dirs, 'symlinks': links,
           'logical_bytes': logical, 'allocated_bytes_before_hardlink_dedup': allocated,
           'unique_chunks': len(seen), 'unique_bytes': unique_bytes, 'elapsed_seconds': round(time.monotonic()-started)}
emit(summary); sys.stdout.flush(); print(json.dumps(summary), file=sys.stderr, flush=True)
