"""Add fixed-name downloads without changing the verified, versioned release assets."""
import argparse
from pathlib import Path
import shutil

import release


DOWNLOAD_NAMES = {
    "macos-arm64": "Noitch-macos-arm64.dmg",
    "windows-x64": "Noitch-windows-x64.exe",
}
CHECKSUM_NAME = "DOWNLOADS.sha256"


def download_manifest(tag, mode):
    v = release.version(tag)
    return "".join(
        f"{release.digest(Path('assets') / release.asset_name(v, platform, mode))}  {name}\n"
        for platform, name in DOWNLOAD_NAMES.items())


def verified_base(tag, commit, mode):
    names = release.verify(Path('assets'), tag, commit, mode)
    expected = ''.join(f"{release.digest(Path('assets') / name)}  {name}\n" for name in sorted(names))
    if release.workspace_path(Path('assets/SHA256SUMS')).read_text() != expected:
        raise ValueError('Original build checksum listing does not match its assets')
    return names + ['SHA256SUMS']


def prepare(tag, commit, mode):
    verified_base(tag, commit, mode)
    output = release.workspace_path(Path('downloads'))
    output.mkdir(exist_ok=True)
    v = release.version(tag)
    for platform, name in DOWNLOAD_NAMES.items():
        source = release.workspace_path(Path('assets') / release.asset_name(v, platform, mode))
        target = release.workspace_path(output / name)
        # Exclusive creation also prevents accidental replacement on a local retry.
        with source.open('rb') as original, target.open('xb') as copy:
            shutil.copyfileobj(original, copy)
    with release.workspace_path(output / CHECKSUM_NAME).open('x') as manifest:
        manifest.write(download_manifest(tag, mode))


def check(folder, tag, commit, mode, complete=False):
    base = verified_base(tag, commit, mode)
    folder = release.workspace_path(folder)
    downloads = set(DOWNLOAD_NAMES.values()) | {CHECKSUM_NAME}
    actual = {path.name for path in folder.iterdir()}
    if not set(base) <= actual or actual - set(base) - downloads:
        raise ValueError('Missing original assets or unexpected release files')
    for name in base:
        if release.digest(Path('assets') / name) != release.digest(folder / name):
            raise ValueError(f'Original release asset differs: {name}')
    v = release.version(tag)
    for platform, name in DOWNLOAD_NAMES.items():
        if name in actual:
            original = Path('assets') / release.asset_name(v, platform, mode)
            if release.digest(original) != release.digest(folder / name):
                raise ValueError(f'Direct download differs from its verified original: {name}')
    if CHECKSUM_NAME in actual:
        if release.workspace_path(folder / CHECKSUM_NAME).read_text() != download_manifest(tag, mode):
            raise ValueError('Direct-download checksums differ')
    if complete and not downloads <= actual:
        raise ValueError('Direct downloads are incomplete')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('command', choices=('prepare', 'check'))
    parser.add_argument('--tag', required=True)
    parser.add_argument('--commit', required=True)
    parser.add_argument('--mode', required=True, choices=('signed', 'unsigned'))
    parser.add_argument('--folder', choices=('downloaded', 'downloads-verified'), default='downloaded')
    parser.add_argument('--complete', action='store_true')
    args = parser.parse_args()
    if args.command == 'prepare':
        prepare(args.tag, args.commit, args.mode)
    else:
        check(Path(args.folder), args.tag, args.commit, args.mode, args.complete)


if __name__ == '__main__':
    main()
