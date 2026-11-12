"""Release contract checks. Standard library only; never loads signing credentials."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


RELEASE_FOLDERS = {name: Path(name) for name in (
    "macos/build/release", "windows/build/dist", "assets", "downloaded")}
TOOLCHAIN_FILES = {
    "macos-arm64": Path("macos/build/release-toolchain.txt"),
    "windows-x64": Path("windows/build/release-toolchain.txt"),
}


def workspace_path(path):
    # Reject symlinks as well as traversal; artifacts must not redirect reads or manifest writes.
    expected = Path.cwd() / path
    if path.is_absolute() or path.resolve() != expected:
        raise ValueError("Release paths must remain inside their fixed workspace locations")
    return path


def checked_mode(mode):
    if mode not in ("signed", "unsigned"):
        raise ValueError("Release mode must be explicitly signed or unsigned")
    return mode


def configured_mode():
    return checked_mode(json.loads(workspace_path(Path(".github/release.json")).read_text()).get("mode"))


def mode_at_commit(commit):
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Expected a full commit SHA")
    content = subprocess.check_output(["git", "show", f"{commit}:.github/release.json"], text=True)
    return checked_mode(json.loads(content).get("mode"))


def signing_kind(platform, mode):
    checked_mode(mode)
    if mode == "signed":
        return "developer-id-notarized" if platform == "macos-arm64" else "authenticode"
    return "ad-hoc-not-notarized" if platform == "macos-arm64" else "unsigned"


def asset_name(v, platform, mode):
    checked_mode(mode)
    suffix = "dmg" if platform == "macos-arm64" else "exe"
    label = "" if mode == "signed" else ("-adhoc" if platform == "macos-arm64" else "-unsigned")
    return f"Noitch-{v}-{platform}{label}.{suffix}"


def release_notes(mode):
    checked_mode(mode)
    if mode == "signed":
        return "macOS: Developer ID signed and notarized. Windows: Authenticode signed."
    return ("## Certificate-free release\n\n"
            "**macOS: Ad-hoc signed, not notarized. Windows: Unsigned.**\n\n"
            "These builds do not verify a publisher identity. Verify SHA256SUMS against the assets. "
            "macOS Gatekeeper and Windows SmartScreen/Smart App Control may warn or block launch. "
            "Do not disable system-wide security protections.\n\n"
            "If you trust the source, see [Apple's opening guidance](https://support.apple.com/en-us/102445) "
            "and [Microsoft's reputation guidance](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation).")


def verify_notes(body, mode):
    notes = release_notes(mode)
    required = notes.split("\n\n")[1] if mode == "unsigned" else notes
    if required not in body:
        raise ValueError("Release notes must disclose the verified signing mode")


def version(tag):
    match = re.fullmatch(r"v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", tag)
    if not match or any(int(n) > 65535 for n in match.groups()):
        raise ValueError("Expected stable tag vMAJOR.MINOR.PATCH, components 0..65535")
    return ".".join(str(int(n)) for n in match.groups())


def head_commit():
    return subprocess.check_output(["git", "rev-parse", "--verify", "HEAD"], text=True).strip()


def tag_commit(tag):
    canonical = "v" + version(tag)
    return subprocess.check_output(
        ["git", "rev-parse", "--verify", "--end-of-options", f"refs/tags/{canonical}^{{commit}}"],
        text=True).strip()


def require_main_commit(commit):
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Expected a full commit SHA")
    subprocess.run(["git", "merge-base", "--is-ancestor", commit, "origin/main"], check=True)


def digest(path):
    result = hashlib.sha256()
    with workspace_path(path).open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def verify(folder, tag, commit, mode):
    checked_mode(mode)
    v = version(tag)
    expected = []
    for platform in TOOLCHAIN_FILES:
        name = asset_name(v, platform, mode)
        metadata = workspace_path(folder / f"{platform}.json")
        data = json.loads(metadata.read_text())
        if (data["tag"], data["commit"], data["asset"]) != (tag, commit, name):
            raise ValueError(f"Metadata does not match release: {metadata}")
        if data.get("release_mode") != mode or data.get("signing") != signing_kind(platform, mode):
            raise ValueError(f"Signing metadata does not match the requested mode: {metadata}")
        if not data["toolchain"] or data["sha256"] != digest(folder / name):
            raise ValueError(f"Invalid toolchain or checksum: {name}")
        expected.extend((name, metadata.name))
    actual = {p.name for p in folder.iterdir()}
    if actual - set(expected) - {"SHA256SUMS"}:
        raise ValueError("Unexpected release assets")
    return expected


def validate_run(run, commit):
    if (run["path"], run["event"], run["head_sha"], run["conclusion"]) != (
            ".github/workflows/release.yml", "push", commit, "success"):
        raise ValueError("Expected a successful Release tag run for this exact commit")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("validate", "metadata", "verify", "compare", "notes"))
    parser.add_argument("--tag", required=True)
    parser.add_argument("--commit")
    parser.add_argument("--folder", choices=RELEASE_FOLDERS)
    parser.add_argument("--other", choices=RELEASE_FOLDERS)
    parser.add_argument("--platform", choices=("macos-arm64", "windows-x64"))
    parser.add_argument("--mode", choices=("signed", "unsigned"))
    args = parser.parse_args()
    v = version(args.tag)
    folder = workspace_path(RELEASE_FOLDERS[args.folder]) if args.folder else None
    if args.command not in ("validate", "notes") and folder is None:
        parser.error("--folder is required")
    mode = configured_mode() if args.command == "validate" else checked_mode(args.mode)
    if args.command == "notes":
        print(release_notes(mode))
        return
    if args.command == "validate":
        commit = tag_commit(args.tag)
        if head_commit() != commit:
            raise ValueError("Checkout is not the tagged commit")
        require_main_commit(commit)
        with open(os.environ["GITHUB_OUTPUT"], "a") as output:
            output.write(f"version={v}\ncommit={commit}\ntag={args.tag}\nmode={mode}\n")
    elif args.command == "metadata":
        if args.platform not in TOOLCHAIN_FILES:
            parser.error("--platform is required for metadata")
        toolchain = workspace_path(TOOLCHAIN_FILES[args.platform])
        asset = asset_name(v, args.platform, mode)
        data = dict(tag=args.tag, commit=args.commit, asset=asset,
                    release_mode=mode, signing=signing_kind(args.platform, mode),
                    sha256=digest(folder / asset), toolchain=toolchain.read_text(),
                    run_id=os.environ.get("GITHUB_RUN_ID"),
                    run_attempt=os.environ.get("GITHUB_RUN_ATTEMPT"))
        workspace_path(folder / f"{args.platform}.json").write_text(json.dumps(data, indent=2) + "\n")
    else:
        names = verify(folder, args.tag, args.commit, mode)
        if args.command == "compare":
            if args.other is None:
                parser.error("--other is required for compare")
            other = workspace_path(RELEASE_FOLDERS[args.other])
            verify(other, args.tag, args.commit, mode)
            names.append("SHA256SUMS")
            for name in names:
                if digest(folder / name) != digest(other / name):
                    raise ValueError(f"Downloaded asset differs: {name}")
        else:
            workspace_path(folder / "SHA256SUMS").write_text("".join(
                f"{digest(folder / name)}  {name}\n" for name in sorted(names)))


if __name__ == "__main__":
    main()
