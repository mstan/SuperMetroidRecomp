#!/usr/bin/env python3
"""Transport private generated inputs and audit public runtime packages.

No ROM is uploaded. Generated C remains in private build storage, accessed by
CI through a short-lived signed URL. Public packages contain only the runtime.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inputs():
    files = [ROOT / name for name in ('CMakeLists.txt', 'VERSION',
             'rom_identity.txt', 'framework_pins.txt', 'tools/regen.py', 'tools/regen.sh')]
    for folder in ('src', 'recomp', 'profiles'):
        files += [p for p in (ROOT / folder).rglob('*') if p.is_file()
                  and 'gen' not in p.relative_to(ROOT).parts
                  and p.name != 'funcs.h' and p.suffix in ('.c', '.h', '.cfg', '.json')]
    # Git may check text out as CRLF on a Windows runner. This is the same
    # source input; generated payload files themselves remain byte-verified.
    return {p.relative_to(ROOT).as_posix(): hashlib.sha256(
            p.read_bytes().replace(b'\r\n', b'\n')).hexdigest() for p in sorted(files)}


def archive(gen, output):
    if not all((gen / n).is_file() for n in ('dispatch_v2.c', 'module_v2.c')):
        raise ValueError('A complete generated native program is required')
    files = {f'src/gen/{p.name}': p for p in gen.iterdir()
             if p.is_file() and (p.suffix in ('.c', '.h') or p.name == 'program_manifest.json')}
    files['recomp/funcs.h'] = ROOT / 'recomp/funcs.h'
    meta = dict(schema='supermetroid private build v1', inputs=inputs(),
                files={name: digest(p) for name, p in sorted(files.items())})
    raw = (json.dumps(meta, sort_keys=True, indent=2) + '\n').encode()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(output, 'w:gz') as tar:
        for name, path in sorted(files.items()):
            tar.add(path, arcname=name, recursive=False)
        entry = tarfile.TarInfo('private-build.json')
        entry.size = len(raw)
        tar.addfile(entry, io.BytesIO(raw))
    print(json.dumps(dict(archive=str(output), sha256=digest(output),
                         profile=json.loads((ROOT / 'profiles/release.json').read_text())['id'])))


def restore(source, expected):
    if not re.fullmatch('[0-9a-f]{64}', expected) or digest(source) != expected:
        raise ValueError('Private build input SHA-256 mismatch')
    with tarfile.open(source, 'r:gz') as tar:
        members = tar.getmembers()
        if len({m.name for m in members}) != len(members):
            raise ValueError('Duplicate private archive member')
        for m in members:
            path = PurePosixPath(m.name)
            allowed = (m.name in ('private-build.json', 'recomp/funcs.h') or
                       (len(path.parts) == 3 and path.parts[:2] == ('src', 'gen')
                        and (path.suffix in ('.c', '.h') or path.name == 'program_manifest.json')))
            if not allowed or not m.isfile() or m.size > 128 * 1024 * 1024:
                raise ValueError(f'Unexpected private archive member: {m.name}')
        if sum(m.size for m in members) > 512 * 1024 * 1024:
            raise ValueError('Oversized generated program')
        meta = json.load(tar.extractfile('private-build.json'))
        if meta['schema'] != 'supermetroid private build v1' or meta['inputs'] != inputs():
            raise ValueError('Generated program does not match this source/profile/pins')
        if set(meta['files']) != {m.name for m in members} - {'private-build.json'}:
            raise ValueError('Private archive file inventory mismatch')
        data = {name: tar.extractfile(name).read() for name in meta['files']}
        for name, raw in data.items():
            if hashlib.sha256(raw).hexdigest() != meta['files'][name]:
                raise ValueError(f'Generated file digest mismatch: {name}')
        for name, raw in data.items():
            path = ROOT / name
            if path.exists() and path.read_bytes() != raw:
                raise ValueError(f'Refusing to replace existing output: {name}')
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(raw)
    print(f'Restored {len(data)} verified generated build files')


def audit(archive_path):
    with zipfile.ZipFile(archive_path) as archive:
        if archive.testzip():
            raise ValueError('ZIP CRC failure')
        names = archive.namelist()
        roots = {PurePosixPath(n).parts[0] for n in names}
        if len(roots) != 1:
            raise ValueError('Package must have one portable root folder')
        for name in names:
            path = PurePosixPath(name)
            if '\\' in name or path.is_absolute() or '..' in path.parts:
                raise ValueError(f'Unsafe ZIP path: {name}')
            # This standalone support tool is intentionally shipped; no other
            # Python, game source, generated C or build tooling is public.
            support_helper = len(path.parts) == 2 and path.name == 'replay_diagnostic.py'
            if path.suffix.lower() in ('.sfc', '.smc', '.rom', '.srm', '.sav', '.c', '.h', '.py', '.a', '.o', '.obj') and not support_helper:
                raise ValueError(f'Build input or private state in package: {name}')
            if path.suffix.lower() in ('.replay', '.dmp', '.jsonl') or '.replay.' in path.name or path.name.startswith(('crash_report_', 'last_run_report')):
                raise ValueError(f'Private diagnostics in package: {name}')
            if 'logs' in path.parts[1:]:
                raise ValueError(f'Private diagnostics in package: {name}')
            if any(part in ('toolchain', 'src', 'recomp', 'recomp-ui', 'snesrecomp', 'saves') for part in path.parts[1:]):
                raise ValueError(f'Build tree or state in package: {name}')
        root = next(iter(roots)) + '/'
        catalog = ROOT / 'mods/preloaded'
        if catalog.is_dir():
            expected = {p.relative_to(ROOT).as_posix(): p
                        for p in catalog.rglob('*') if p.is_file()}
            shipped = {n[len(root):] for n in names
                       if n.startswith(root + 'mods/') and not n.endswith('/')}
            if shipped != set(expected):
                raise ValueError('Runtime mod catalog differs from tracked packages')
            for name, source in expected.items():
                if archive.read(root + name).replace(b'\r\n', b'\n') != source.read_bytes().replace(b'\r\n', b'\n'):
                    raise ValueError(f'Runtime mod package differs from source: {name}')
        for required in ('VERSION', 'framework_pins.txt', 'release-profile.json', 'config.ini', 'README.md', 'replay_diagnostic.py'):
            if root + required not in names:
                raise ValueError(f'Missing package file: {required}')
        if not any(n.startswith(root + 'assets/') for n in names):
            raise ValueError('Missing launcher assets')
        if not any(n in names for n in (root + 'SuperMetroidSNESRecomp', root + 'SuperMetroidSNESRecomp.exe')):
            raise ValueError('Missing native game executable')
    print(f'Runtime package passed: {archive_path.name} ({len(names)} entries)')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    pack = sub.add_parser('archive')
    pack.add_argument('--gen-dir', type=Path, default=ROOT / 'src/gen')
    pack.add_argument('--output', type=Path, required=True)
    unpack = sub.add_parser('restore')
    unpack.add_argument('archive', type=Path)
    unpack.add_argument('--sha256', required=True)
    check = sub.add_parser('audit')
    check.add_argument('archive', type=Path)
    args = parser.parse_args()
    if args.command == 'archive':
        archive(args.gen_dir.resolve(), args.output.resolve())
    elif args.command == 'restore':
        restore(args.archive, args.sha256)
    else:
        audit(args.archive)


if __name__ == '__main__':
    main()
