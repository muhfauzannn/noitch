"""Release contract checks. Standard library only; never loads signing credentials."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def version(tag):
    match = re.fullmatch(r"v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", tag)
    if not match or any(int(n) > 65535 for n in match.groups()):
        raise ValueError("Expected stable tag vMAJOR.MINOR.PATCH, components 0..65535")
    return tag[1:]


def git(*args):
    return subprocess.check_output(["git", *args], text=True).strip()


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(folder, tag, commit):
    v = version(tag)
    expected = []
    for platform, suffix in (("macos-arm64", "dmg"), ("windows-x64", "exe")):
        name = f"Noitch-{v}-{platform}.{suffix}"
        metadata = folder / f"{platform}.json"
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
    parser.add_argument("--folder", type=Path)
    parser.add_argument("--other", type=Path)
    parser.add_argument("--platform", choices=("macos-arm64", "windows-x64"))
    parser.add_argument("--toolchain", type=Path)
    args = parser.parse_args()
    v = version(args.tag)
    if args.command == "validate":
        commit = git("rev-parse", f"refs/tags/{args.tag}^{{commit}}")
        if git("rev-parse", "HEAD") != commit:
            raise ValueError("Checkout is not the tagged commit")
        subprocess.run(["git", "merge-base", "--is-ancestor", commit, "origin/main"], check=True)
        with open(os.environ["GITHUB_OUTPUT"], "a") as output:
            output.write(f"version={v}\ncommit={commit}\ntag={args.tag}\n")
    elif args.command == "metadata":
        suffix = "dmg" if args.platform == "macos-arm64" else "exe"
        asset = f"Noitch-{v}-{args.platform}.{suffix}"
        data = dict(tag=args.tag, commit=args.commit, asset=asset,
                    sha256=digest(args.folder / asset), toolchain=args.toolchain.read_text(),
                    run_id=os.environ.get("GITHUB_RUN_ID"),
                    run_attempt=os.environ.get("GITHUB_RUN_ATTEMPT"))
        (args.folder / f"{args.platform}.json").write_text(json.dumps(data, indent=2) + "\n")
    else:
        names = verify(args.folder, args.tag, args.commit)
        if args.command == "compare":
            verify(args.other, args.tag, args.commit)
            names.append("SHA256SUMS")
            for name in names:
                if digest(args.folder / name) != digest(args.other / name):
                    raise ValueError(f"Downloaded asset differs: {name}")
        else:
            (args.folder / "SHA256SUMS").write_text("".join(
                f"{digest(args.folder / name)}  {name}\n" for name in sorted(names)))


if __name__ == "__main__":
    main()
