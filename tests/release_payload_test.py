"""Release boundary regressions: stale/tampered/private files cannot ship."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location('payload', Path(__file__).resolve().parents[1] / 'tools/release_payload.py')
payload = importlib.util.module_from_spec(spec)
spec.loader.exec_module(payload)


class ReleaseBoundaryTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.previous_root = payload.ROOT
        payload.ROOT = self.root
        for folder in ('src/gen', 'recomp', 'profiles', 'tools'):
            (self.root / folder).mkdir(parents=True)
        for name in ('CMakeLists.txt', 'VERSION', 'rom_identity.txt', 'framework_pins.txt'):
            (self.root / name).write_text('original\n')
        for name in ('regen.py', 'regen.sh'):
            (self.root / 'tools' / name).write_text('original\n')
        (self.root / 'recomp/bank00.cfg').write_text('original\n')
        (self.root / 'recomp/funcs.h').write_text('void guest(void);\n')
        (self.root / 'profiles/release.json').write_text('{"id":"test"}\n')
        for name in ('dispatch_v2.c', 'module_v2.c'):
            (self.root / 'src/gen' / name).write_text('/* generated */\n')

    def tearDown(self):
        payload.ROOT = self.previous_root
        self.temp.cleanup()

    def test_roundtrip_and_tampered_archive(self):
        path = self.root / 'private.tar.gz'
        payload.archive(self.root / 'src/gen', path)
        digest = payload.digest(path)
        payload.restore(path, digest)
        with self.assertRaisesRegex(ValueError, 'SHA-256'):
            payload.restore(path, '0' * 64)

    def test_changed_cfg_rejected_but_crlf_checkout_accepted(self):
        path = self.root / 'private.tar.gz'
        payload.archive(self.root / 'src/gen', path)
        cfg = self.root / 'recomp/bank00.cfg'
        cfg.write_bytes(b'original\r\n')
        payload.restore(path, payload.digest(path))
        cfg.write_text('changed\n')
        with self.assertRaisesRegex(ValueError, 'source/profile/pins'):
            payload.restore(path, payload.digest(path))

    def test_archive_traversal_rejected_before_writing(self):
        path = self.root / 'malicious.tar.gz'
        with tarfile.open(path, 'w:gz') as tar:
            item = tarfile.TarInfo('src/gen/../../escaped.c')
            item.size = 3
            tar.addfile(item, io.BytesIO(b'bad'))
        with self.assertRaisesRegex(ValueError, 'Unexpected'):
            payload.restore(path, payload.digest(path))
        self.assertFalse((self.root / 'escaped.c').exists())

    def test_changed_native_include_rejected(self):
        include = self.root / 'src/row_plan.inc'
        include.write_text('original\n')
        path = self.root / 'private.tar.gz'
        payload.archive(self.root / 'src/gen', path)
        include.write_text('changed\n')
        with self.assertRaisesRegex(ValueError, 'source/profile/pins'):
            payload.restore(path, payload.digest(path))

    def test_public_package_rejects_generated_sources(self):
        path = self.root / 'runtime.zip'
        with zipfile.ZipFile(path, 'w') as archive:
            archive.writestr('runtime/src/gen/dispatch_v2.c', '/* private */')
        with self.assertRaisesRegex(ValueError, 'Build input'):
            payload.audit(path)

    def test_public_package_only_allows_the_standalone_support_script(self):
        path = self.root / 'runtime.zip'
        files = ['VERSION', 'framework_pins.txt', 'release-profile.json', 'config.ini',
                 'README.md', 'replay_diagnostic.py', 'assets/boxart.tga', 'SuperMetroidSNESRecomp']
        def make(extra=None):
            with zipfile.ZipFile(path, 'w') as archive:
                for name in files + ([extra] if extra else []):
                    archive.writestr('runtime/' + name, 'fixture')
        make()
        payload.audit(path)
        for name in ('another.py', 'native-row.inc', 'tools/replay_diagnostic.py', 'capture.replay.config.ini',
                     'logs/session.log', 'crash_report_123.json', 'crash_minidump_123.dmp'):
            make(name)
            with self.assertRaises(ValueError):
                payload.audit(path)


if __name__ == '__main__':
    unittest.main()
