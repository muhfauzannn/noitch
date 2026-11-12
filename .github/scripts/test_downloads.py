import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

import downloads
import release
from test_release import isolated_workspace

TAG = 'v1.2.3'
COMMIT = 'a' * 40


def fixture(mode):
    folder = Path('assets')
    folder.mkdir()
    for platform in release.TOOLCHAIN_FILES:
        name = release.asset_name('1.2.3', platform, mode)
        content = f'{platform}: verified bytes'.encode()
        (folder / name).write_bytes(content)
        (folder / f'{platform}.json').write_text(json.dumps(dict(
            tag=TAG, commit=COMMIT, asset=name, release_mode=mode,
            signing=release.signing_kind(platform, mode), toolchain='test fixture',
            sha256=hashlib.sha256(content).hexdigest())))
    names = release.verify(folder, TAG, COMMIT, mode)
    (folder / 'SHA256SUMS').write_text(''.join(
        f'{release.digest(folder / name)}  {name}\n' for name in sorted(names)))
    shutil.copytree(folder, 'downloaded')


class DirectDownloadTests(unittest.TestCase):
    def test_existing_five_asset_drafts_are_supported_in_both_modes(self):
        for mode in ('signed', 'unsigned'):
            with self.subTest(mode=mode), isolated_workspace():
                fixture(mode)
                original_checksums = Path('assets/SHA256SUMS').read_bytes()
                downloads.check(Path('downloaded'), TAG, COMMIT, mode)
                with self.assertRaisesRegex(ValueError, 'incomplete'):
                    downloads.check(Path('downloaded'), TAG, COMMIT, mode, complete=True)
                downloads.prepare(TAG, COMMIT, mode)
                for file in Path('downloads').iterdir():
                    shutil.copyfile(file, Path('downloaded') / file.name)
                downloads.check(Path('downloaded'), TAG, COMMIT, mode, complete=True)
                self.assertEqual(Path('downloaded/SHA256SUMS').read_bytes(), original_checksums)
                self.assertEqual(len(list(Path('downloaded').iterdir())), 8)
                with self.assertRaises(FileExistsError):
                    downloads.prepare(TAG, COMMIT, mode)

    def test_partial_uploads_can_be_verified_before_retry(self):
        with isolated_workspace():
            fixture('unsigned')
            downloads.prepare(TAG, COMMIT, 'unsigned')
            name = downloads.DOWNLOAD_NAMES['macos-arm64']
            shutil.copyfile(Path('downloads') / name, Path('downloaded') / name)
            downloads.check(Path('downloaded'), TAG, COMMIT, 'unsigned')
            with self.assertRaisesRegex(ValueError, 'incomplete'):
                downloads.check(Path('downloaded'), TAG, COMMIT, 'unsigned', complete=True)

    def test_changed_alias_and_unknown_assets_are_rejected(self):
        with isolated_workspace():
            fixture('unsigned')
            downloads.prepare(TAG, COMMIT, 'unsigned')
            name = downloads.DOWNLOAD_NAMES['windows-x64']
            alias = Path('downloaded') / name
            alias.write_bytes(b'not the original executable')
            with self.assertRaisesRegex(ValueError, 'differs from its verified original'):
                downloads.check(Path('downloaded'), TAG, COMMIT, 'unsigned')
            shutil.copyfile(Path('downloads') / name, alias)
            Path('downloaded/unexpected.exe').write_bytes(b'unverified')
            with self.assertRaisesRegex(ValueError, 'unexpected'):
                downloads.check(Path('downloaded'), TAG, COMMIT, 'unsigned')

    def test_changed_original_and_checksum_listing_are_rejected(self):
        with isolated_workspace():
            fixture('unsigned')
            manifest = Path('downloaded') / downloads.CHECKSUM_NAME
            manifest.write_text('incorrect')
            with self.assertRaisesRegex(ValueError, 'checksums differ'):
                downloads.check(Path('downloaded'), TAG, COMMIT, 'unsigned')
            manifest.write_text(downloads.download_manifest(TAG, 'unsigned'))
            Path('downloaded/SHA256SUMS').write_text('changed original checksums')
            with self.assertRaisesRegex(ValueError, 'Original release asset differs'):
                downloads.check(Path('downloaded'), TAG, COMMIT, 'unsigned')

    def test_invalid_original_checksum_listing_blocks_preparation(self):
        with isolated_workspace():
            fixture('unsigned')
            Path('assets/SHA256SUMS').write_text('unverified checksum listing')
            with self.assertRaisesRegex(ValueError, 'Original build checksum listing'):
                downloads.prepare(TAG, COMMIT, 'unsigned')
            self.assertFalse(Path('downloads').exists())

    def test_cli_checks_complete_downloads(self):
        script = str(Path(__file__).with_name('downloads.py').resolve())
        with isolated_workspace():
            fixture('unsigned')
            common = ['--tag', TAG, '--commit', COMMIT, '--mode', 'unsigned']
            subprocess.run([sys.executable, script, 'prepare'] + common, check=True)
            shutil.copytree('downloaded', 'downloads-verified')
            shutil.copytree('downloads', 'downloads-verified', dirs_exist_ok=True)
            subprocess.run([sys.executable, script, 'check', '--folder', 'downloads-verified',
                            '--complete'] + common, check=True)


if __name__ == '__main__':
    unittest.main()
