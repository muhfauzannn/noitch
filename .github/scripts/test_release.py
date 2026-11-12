from contextlib import contextmanager
import importlib.util
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

RELEASE_SCRIPT = Path(__file__).with_name('release.py').resolve()
spec = importlib.util.spec_from_file_location('release', RELEASE_SCRIPT)
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


@contextmanager
def isolated_workspace():
    previous = Path.cwd()
    with tempfile.TemporaryDirectory() as tmp:
        os.chdir(tmp)
        try:
            yield
        finally:
            os.chdir(previous)


class ReleaseContractTests(unittest.TestCase):
    def test_mode_is_explicit_and_filenames_disclose_unsigned_builds(self):
        for value in (None, '', 'auto', 'Unsigned'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                release.checked_mode(value)
        self.assertEqual(release.asset_name('1.2.3', 'macos-arm64', 'unsigned'),
                         'Noitch-1.2.3-macos-arm64-adhoc.dmg')
        self.assertEqual(release.asset_name('1.2.3', 'windows-x64', 'unsigned'),
                         'Noitch-1.2.3-windows-x64-unsigned.exe')
        self.assertEqual(release.asset_name('1.2.3', 'macos-arm64', 'signed'),
                         'Noitch-1.2.3-macos-arm64.dmg')
        with isolated_workspace():
            Path('.github').mkdir()
            config = Path('.github/release.json')
            config.write_text('{"mode":"unsigned"}')
            self.assertEqual(release.configured_mode(), 'unsigned')
            config.write_text('{}')
            with self.assertRaises(ValueError):
                release.configured_mode()

    def test_publication_requires_signing_disclosure(self):
        notes = release.release_notes('unsigned')
        self.assertIn('Ad-hoc signed, not notarized', notes)
        self.assertIn('Windows: Unsigned', notes)
        release.verify_notes(notes + '\nGenerated changelog', 'unsigned')
        with self.assertRaises(ValueError):
            release.verify_notes('Release v1.2.3', 'unsigned')
        with self.assertRaises(ValueError):
            release.verify_notes(release.release_notes('signed'), 'unsigned')

    def test_git_arguments_are_rejected_before_execution(self):
        for value in ('--upload-pack=evil', 'v1.2.3^{tree}', 'v1.2.3/../../main', 'HEAD', 'v1.2.3\n'):
            with self.subTest(value=value), patch.object(release.subprocess, 'check_output') as execute:
                with self.assertRaises(ValueError):
                    release.tag_commit(value)
                execute.assert_not_called()
        with patch.object(release.subprocess, 'run') as execute:
            with self.assertRaises(ValueError):
                release.require_main_commit('--config=evil')
            execute.assert_not_called()

    def test_cli_rejects_arbitrary_paths(self):
        for option in ('--folder', '--other'):
            result = subprocess.run([sys.executable, str(RELEASE_SCRIPT), 'verify', '--tag', 'v1.2.3',
                                     option, '../../private'], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b'invalid choice', result.stderr)

    def test_workspace_rejects_symlink_reads_and_writes(self):
        with isolated_workspace():
            folder = Path('assets')
            folder.mkdir()
            Path('secret').write_text('do not overwrite')
            link = folder / 'macos-arm64.json'
            link.symlink_to('../secret')
            with self.assertRaises(ValueError):
                release.workspace_path(link)
            with self.assertRaises(ValueError):
                release.workspace_path(Path('../outside'))
            with self.assertRaises(ValueError):
                release.workspace_path(Path.cwd() / 'secret')
            self.assertEqual(Path('secret').read_text(), 'do not overwrite')

    def test_publication_requires_successful_release_run(self):
        run = dict(path='.github/workflows/release.yml', event='push', head_sha='a' * 40, conclusion='success')
        release.validate_run(run, 'a' * 40)
        for key, value in (('path', '.github/workflows/macos.yml'), ('event', 'pull_request'),
                           ('head_sha', 'b' * 40), ('conclusion', 'failure'), ('conclusion', None)):
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                release.validate_run(dict(run, **{key: value}), 'a' * 40)

    def test_release_script_does_not_infer_a_mode(self):
        script = Path(__file__).resolve().parents[2] / 'macos/scripts/release.sh'
        env = dict(PATH=os.environ['PATH'], RELEASE_VERSION='1.2.3', RELEASE_TAG='v1.2.3',
                   RELEASE_COMMIT='a' * 40, MACOS_BUNDLE_ID='app.noitch.mac')
        for mode in (None, 'auto'):
            if mode:
                env['RELEASE_MODE'] = mode
            result = subprocess.run(['bash', str(script)], capture_output=True, text=True, env=env)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('RELEASE_MODE', result.stderr)

    def test_missing_signing_credentials_fail_before_build(self):
        script = Path(__file__).resolve().parents[2] / 'macos/scripts/release.sh'
        result = subprocess.run(['bash', str(script)], capture_output=True, text=True,
                                env=dict(PATH=os.environ['PATH'], RELEASE_MODE='signed', RELEASE_VERSION='1.2.3',
                                         RELEASE_TAG='v1.2.3', RELEASE_COMMIT='a' * 40,
                                         MACOS_BUNDLE_ID='example.noitch', MACOS_TEAM_ID='EXAMPLE',
                                         MACOS_SIGN_IDENTITY='Developer ID Application: Example'))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Missing MACOS_CERTIFICATE_BASE64', result.stderr)

    def test_cli_round_trip_and_tampered_download(self):
        for mode in ('signed', 'unsigned'):
            with self.subTest(mode=mode), isolated_workspace():
                folder = Path('assets')
                folder.mkdir()
                cli = [sys.executable, str(RELEASE_SCRIPT)]
                common = ['--tag', 'v1.2.3', '--commit', 'a' * 40, '--mode', mode, '--folder', str(folder)]
                for platform in release.TOOLCHAIN_FILES:
                    (folder / release.asset_name('1.2.3', platform, mode)).write_bytes(b'test fixture')
                    toolchain = release.TOOLCHAIN_FILES[platform]
                    toolchain.parent.mkdir(parents=True, exist_ok=True)
                    toolchain.write_text('test toolchain')
                    subprocess.run(cli + ['metadata'] + common + ['--platform', platform], check=True)
                subprocess.run(cli + ['verify'] + common, check=True)
                other = Path('downloaded')
                shutil.copytree(folder, other)
                subprocess.run(cli + ['compare'] + common + ['--other', str(other)], check=True)
                metadata = other / 'macos-arm64.json'
                original = metadata.read_text()
                for field, value in (('release_mode', 'unsigned' if mode == 'signed' else 'signed'),
                                     ('signing', 'incorrect signing claim')):
                    data = json.loads(original)
                    data[field] = value
                    metadata.write_text(json.dumps(data))
                    result = subprocess.run(cli + ['compare'] + common + ['--other', str(other)], capture_output=True)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(b'Signing metadata does not match', result.stderr)
                metadata.write_text(original)
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
        with isolated_workspace():
            folder = Path('assets')
            folder.mkdir()
            for platform, suffix in (('macos-arm64', 'dmg'), ('windows-x64', 'exe')):
                name = release.asset_name('1.2.3', platform, 'unsigned')
                (folder / name).write_bytes(b'signed final bytes')
                (folder / f'{platform}.json').write_text(json.dumps(dict(
                    tag='v1.2.3', commit='a' * 40, asset=name, release_mode='unsigned',
                    signing=release.signing_kind(platform, 'unsigned'),
                    sha256=release.digest(folder / name), toolchain='tool version')))
            release.verify(folder, 'v1.2.3', 'a' * 40, 'unsigned')
            with self.assertRaises(ValueError):
                release.verify(folder, 'v1.2.3', 'b' * 40, 'unsigned')
            asset = folder / 'Noitch-1.2.3-windows-x64-unsigned.exe'
            asset.write_bytes(b'modified after signing')
            with self.assertRaises(ValueError):
                release.verify(folder, 'v1.2.3', 'a' * 40, 'unsigned')
            asset.unlink()
            with self.assertRaises(FileNotFoundError):
                release.verify(folder, 'v1.2.3', 'a' * 40, 'unsigned')


if __name__ == '__main__':
    unittest.main()
