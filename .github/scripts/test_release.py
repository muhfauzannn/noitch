import importlib.util
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('release', Path(__file__).with_name('release.py'))
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class ReleaseContractTests(unittest.TestCase):
    def test_publication_requires_successful_release_run(self):
        run = dict(path='.github/workflows/release.yml', event='push', head_sha='a' * 40, conclusion='success')
        release.validate_run(run, 'a' * 40)
        for key, value in (('path', '.github/workflows/macos.yml'), ('event', 'pull_request'),
                           ('head_sha', 'b' * 40), ('conclusion', 'failure'), ('conclusion', None)):
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                release.validate_run(dict(run, **{key: value}), 'a' * 40)

    def test_missing_signing_credentials_fail_before_build(self):
        script = Path(__file__).resolve().parents[2] / 'macos/scripts/release.sh'
        result = subprocess.run(['bash', str(script)], capture_output=True, text=True,
                                env=dict(PATH=os.environ['PATH'], RELEASE_VERSION='1.2.3',
                                         RELEASE_TAG='v1.2.3', RELEASE_COMMIT='a' * 40,
                                         MACOS_BUNDLE_ID='example.noitch', MACOS_TEAM_ID='EXAMPLE',
                                         MACOS_SIGN_IDENTITY='Developer ID Application: Example'))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Missing MACOS_CERTIFICATE_BASE64', result.stderr)

    def test_cli_round_trip_and_tampered_download(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp) / 'assets'
            folder.mkdir()
            toolchain = Path(tmp) / 'toolchain.txt'
            toolchain.write_text('test toolchain')
            cli = [sys.executable, str(Path(__file__).with_name('release.py'))]
            common = ['--tag', 'v1.2.3', '--commit', 'a' * 40, '--folder', str(folder)]
            for platform, suffix in (('macos-arm64', 'dmg'), ('windows-x64', 'exe')):
                (folder / f'Noitch-1.2.3-{platform}.{suffix}').write_bytes(b'test fixture')
                subprocess.run(cli + ['metadata'] + common + ['--platform', platform,
                               '--toolchain', str(toolchain)], check=True)
            subprocess.run(cli + ['verify'] + common, check=True)
            other = Path(tmp) / 'download'
            shutil.copytree(folder, other)
            subprocess.run(cli + ['compare'] + common + ['--other', str(other)], check=True)
            (other / 'SHA256SUMS').write_text('tampered checksum listing')
            result = subprocess.run(cli + ['compare'] + common + ['--other', str(other)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b'Downloaded asset differs', result.stderr)

    def test_reject_invalid_tags(self):
        for tag in ('1.2.3', 'v01.2.3', 'v1.2', 'v1.2.3-rc.1', 'v65536.0.0', 'v1.2.3\n', 'v1.2.3;echo unsafe'):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                release.version(tag)
        self.assertEqual(release.version('v1.2.3'), '1.2.3')

    def test_both_platforms_must_match_source_and_final_bytes(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            for platform, suffix in (('macos-arm64', 'dmg'), ('windows-x64', 'exe')):
                name = f'Noitch-1.2.3-{platform}.{suffix}'
                (folder / name).write_bytes(b'signed final bytes')
                (folder / f'{platform}.json').write_text(json.dumps(dict(
                    tag='v1.2.3', commit='a' * 40, asset=name,
                    sha256=release.digest(folder / name), toolchain='tool version')))
            release.verify(folder, 'v1.2.3', 'a' * 40)
            with self.assertRaises(ValueError):
                release.verify(folder, 'v1.2.3', 'b' * 40)
            asset = folder / 'Noitch-1.2.3-windows-x64.exe'
            asset.write_bytes(b'modified after signing')
            with self.assertRaises(ValueError):
                release.verify(folder, 'v1.2.3', 'a' * 40)
            asset.unlink()
            with self.assertRaises(FileNotFoundError):
                release.verify(folder, 'v1.2.3', 'a' * 40)


if __name__ == '__main__':
    unittest.main()
