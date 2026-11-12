"""Release contract checks. Standard library only; never loads signing credentials."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


RELEASE_FOLDERS = {name: Path(name) for name in (
    "macos/build/release", "windows/build/release", "assets", "downloaded")}
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


def verify(folder, tag, commit):
    v = version(tag)
    expected = []
    for platform, suffix in (("macos-arm64", "dmg"), ("windows-x64", "exe")):
        name = f"Noitch-{v}-{platform}.{suffix}"
        metadata = workspace_path(folder / f"{platform}.json")
        data = json.loads(metadata.read_text())
        if (data["tag"], data["commit"], data["asset"]) != (tag, commit, name):
            raise ValueError(f"Metadata does not match release: {metadata}")
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
    parser.add_argument("command", choices=("validate", "metadata", "verify", "compare"))
    parser.add_argument("--tag", required=True)
    parser.add_argument("--commit")
    parser.add_argument("--folder", choices=RELEASE_FOLDERS)
    parser.add_argument("--other", choices=RELEASE_FOLDERS)
    parser.add_argument("--platform", choices=("macos-arm64", "windows-x64"))
    args = parser.parse_args()
    v = version(args.tag)
    folder = workspace_path(RELEASE_FOLDERS[args.folder]) if args.folder else None
    if args.command != "validate" and folder is None:
        parser.error("--folder is required")
    if args.command == "validate":
        commit = tag_commit(args.tag)
        if head_commit() != commit:
            raise ValueError("Checkout is not the tagged commit")
        require_main_commit(commit)
        with open(os.environ["GITHUB_OUTPUT"], "a") as output:
            output.write(f"version={v}\ncommit={commit}\ntag={args.tag}\n")
    elif args.command == "metadata":
        if args.platform not in TOOLCHAIN_FILES:
            parser.error("--platform is required for metadata")
        toolchain = workspace_path(TOOLCHAIN_FILES[args.platform])
        suffix = "dmg" if args.platform == "macos-arm64" else "exe"
        asset = f"Noitch-{v}-{args.platform}.{suffix}"
        data = dict(tag=args.tag, commit=args.commit, asset=asset,
                    sha256=digest(folder / asset), toolchain=toolchain.read_text(),
                    run_id=os.environ.get("GITHUB_RUN_ID"),
                    run_attempt=os.environ.get("GITHUB_RUN_ATTEMPT"))
        workspace_path(folder / f"{args.platform}.json").write_text(json.dumps(data, indent=2) + "\n")
    else:
        names = verify(folder, args.tag, args.commit)
        if args.command == "compare":
            if args.other is None:
                parser.error("--other is required for compare")
            other = workspace_path(RELEASE_FOLDERS[args.other])
            verify(other, args.tag, args.commit)
            names.append("SHA256SUMS")
            for name in names:
                if digest(folder / name) != digest(other / name):
                    raise ValueError(f"Downloaded asset differs: {name}")
        else:
            workspace_path(folder / "SHA256SUMS").write_text("".join(
                f"{digest(folder / name)}  {name}\n" for name in sorted(names)))


if __name__ == "__main__":
    main()
