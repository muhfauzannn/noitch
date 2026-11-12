# Releases

Noitch produces a macOS arm64 DMG and a Windows x64 **portable executable**, not an installer.
A stable tag `vMAJOR.MINOR.PATCH` starts the Release workflow. Each numeric component must be at
most 65535, with no leading zeroes. Both products receive that version at build time.

## Explicit release mode

The **tagged commit's** `.github/release.json` selects the mode:

```json
{
  "mode": "unsigned"
}
```

| Mode | macOS | Windows | Certificate secrets |
| --- | --- | --- | --- |
| `unsigned` (current) | Ad-hoc signed, not notarized | Unsigned | None |
| `signed` | Developer ID signed and notarized | Authenticode signed with timestamp | Required |

Missing or unknown modes stop validation. Signed builds never fall back to unsigned when a secret,
signing step or notarization fails. To switch modes, review and commit the configuration change
before creating the next tag. Publication reads the mode from that tagged commit, even if `main`
has since selected a different mode.

For example, `v0.1.0` in unsigned mode produces:

- `Noitch-0.1.0-macos-arm64-adhoc.dmg`
- `Noitch-0.1.0-windows-x64-unsigned.exe`
- `macos-arm64.json`, `windows-x64.json`, and `SHA256SUMS`

Signed-mode filenames omit `-adhoc` / `-unsigned`. Metadata includes the mode and platform signing
status. Certificate-free drafts have a marked title and release notes stating **macOS: Ad-hoc signed,
not notarized. Windows: Unsigned.** The publisher requires the signing disclosure to remain present.

Unsigned-mode verification proves package/signature integrity, versions and architecture; it does
not establish a publisher identity, notarization, or OS reputation acceptance. Checksums establish
consistency with the downloaded metadata/build artifacts, not an independently trusted publisher.

macOS Gatekeeper may block first launch. If a user trusts the source, Apple's
[opening guidance](https://support.apple.com/en-us/102445) describes the per-app Open Anyway option
where available. Windows SmartScreen can warn, and Smart App Control or managed-device policy may
block an unsigned EXE entirely; see [Microsoft's guidance](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation).
Do not ask users to disable system-wide protections or promise that an override is always available.

## Repository setup

Protect `main` with required CI and review. Add a `v*` tag ruleset restricting tag creation, updates
and deletion to release maintainers. Only trusted, reviewed commits reachable from `main` may be
released. Review changes to workflows, packaging scripts and `.github/release.json` together.

Configure these environments:

- `release-unsigned`: certificate-free build jobs; restrict deployments to protected `v*` tags.
  No secrets are required. GitHub can create this environment on first use; set its restrictions
  before relying on them.
- `release-publish`: restrict to `main`, require a release reviewer, and prevent self-review.
  The reviewer checks native smoke-test and performance evidence before approval.
- `release-signing`: needed only for `signed` mode; restrict to protected `v*` tags and store the
  signing secrets below. Add required reviewers as appropriate.

Unsigned steps do not receive signing secrets or use the signing environment. Signing/publication
workflows do not run on pull requests; separate credential-free CI checks the release contract and
script syntax. Environment protections are repository settings; YAML cannot configure them itself.
Draft inspection/verification jobs need `contents: write` for draft visibility; the build jobs use
`contents: read`.

In unsigned mode, `MACOS_BUNDLE_ID` is optional and defaults to the project's `app.noitch.mac`.
Choose a permanent reverse-DNS identifier you control before signing/distributing under your own
identity. Changing it later changes preferences, Keychain access and login registration identity;
this pipeline does not migrate that data.

## Signed-mode credentials (optional)

Only set these up when changing the committed mode to `signed`.

Repository variables:

| Variable | Value |
| --- | --- |
| `MACOS_BUNDLE_ID` | Permanent distribution identifier; signed mode rejects `app.noitch.mac` |
| `MACOS_TEAM_ID` | Apple Developer team ID |
| `WINDOWS_SIGNING_THUMBPRINT` | Expected Authenticode leaf certificate thumbprint |
| `WINDOWS_TIMESTAMP_URL` | Certificate provider's RFC 3161 timestamp URL |

Secrets in **`release-signing`**, never source or workflow inputs:

| Secret | Value |
| --- | --- |
| `MACOS_SIGN_IDENTITY` | Full `Developer ID Application: … (TEAMID)` identity |
| `MACOS_CERTIFICATE_BASE64` | Base64 PKCS#12 with Developer ID Application certificate/private key |
| `MACOS_CERTIFICATE_PASSWORD` | PKCS#12 password |
| `APPLE_API_KEY_BASE64` | Base64 App Store Connect API private `.p8` key authorized for notarization |
| `APPLE_API_KEY_ID` | API key ID |
| `APPLE_API_ISSUER` | Team API issuer UUID |
| `WINDOWS_CERTIFICATE_BASE64` | Base64 PKCS#12 with trusted Authenticode certificate/private key |
| `WINDOWS_CERTIFICATE_PASSWORD` | PKCS#12 password |

Windows signing supports an **existing exportable PFX**. Many public code-signing certificates
require hardware or a managed service and cannot use this path. Agree on an approved integration
before changing it. No signing service or dependency is added. Self-signed certificates do not
replace a publicly trusted publisher identity; valid signing also does not guarantee SmartScreen
reputation.

Signed macOS jobs import keys into a temporary keychain; Windows imports the PFX into the hosted
runner's user store. Scripts clean up keys and temporary files, and ephemeral hosted runners are
discarded after cancellation. Do not use these scripts on persistent self-hosted runners without
a separate key-management review.

## Create and verify a draft

1. Push reviewed changes to `main`, then wait for CI and resolve the required SonarQube reviews.
2. Create a new, unused version tag on the intended commit and push it. For example:

   ```sh
   git tag -a v0.1.0 -m "Release v0.1.0"
   git push origin v0.1.0
   ```

3. The **Release** workflow validates the mode/tag, checkout and ancestry on `main`. Existing
   releases, including drafts, are refused; API failures stop the workflow.
4. Both platforms test/build the exact commit. macOS embeds the adapter's BSD license and signs
   the framework before the app. Unsigned mode uses ad-hoc signatures for the framework, app and
   DMG without notarization. Signed mode additionally notarizes/staples the app and signed DMG.
   Windows either verifies an unsigned EXE or applies/verifies timestamped Authenticode signing.
5. Both native jobs verify version, architecture and the expected signing state. macOS also checks
   image integrity, the Applications link and bundled resources; signed mode checks notarization
   tickets and Gatekeeper assessment. Unsigned mode does not claim those checks passed.
6. After both succeed, the workflow creates a **draft** with explicit signing notes, both products,
   two platform JSON metadata files and `SHA256SUMS`. Metadata records commit, tag, mode, toolchain,
   SHA-256 and run identity; macOS includes the vendored adapter binary's checksum. CI build artifacts
   may be replaced on retry; GitHub release assets are never overwritten.
7. It downloads all draft assets and compares their exact hashes with the original outputs. Fresh
   OS jobs download the native packages again and repeat their mode-specific verification without
   signing secrets. This is package verification, not an interactive GUI test.

Download the draft onto clean macOS/Windows installations. Check copy/install, launch, media,
expand/collapse, click-through and quit. Test the minimum OS versions claimed for that release.
Record local budget measurements on real hardware; shared GitHub runners are not performance evidence.
Unsigned warnings or blocks on a downloaded/quarantined build must be reported honestly. Do not
approve distribution with required native checks missing.

## Publish the existing verified draft

In **Actions → Publish verified release → Run workflow**, select `main` and supply:

- `tag`: the draft's version tag, such as `v0.1.0`.
- `build_run_id`: the successful **Release** run's numeric ID from its URL.

Approve the `release-publish` environment after checking the draft. The publisher validates that
run's workflow, event, exact source commit and successful conclusion. It reads the tagged mode,
retrieves the original build artifacts, checks metadata/mode/hashes, compares all draft assets, and
requires the correct signing disclosure in the notes before publishing. It never rebuilds/re-signs.
Build artifacts expire after seven days; publish within that window.

Do not use the direct GitHub UI Publish button, which bypasses these checks. Release editing remains
a trusted-maintainer boundary. Tag builds and publication serialize on the same tag. Pushing a tag
alone does not publish publicly, and local implementation creates no remote environment, tag or release.

## Failures and retries

- Invalid mode/tag or non-main commit: rejected before build/signing. Use a correct new tag; never
  move an existing release tag.
- Missing signed credentials or one platform fails: no draft is created. Correct configuration and
  rerun failed jobs. A full rerun is possible while no release exists. Unsigned mode needs no
  certificate secrets; do not switch modes by deleting secrets.
- Upload or native re-verification fails: the draft stays private. Rerun the failed job if possible;
  never use `--clobber`. If a partial upload blocks retry, inspect it, explicitly delete only the
  unpublished draft, then rerun Release completely. Do not delete its tag.
- Existing release: full Release reruns stop. Publish a verified draft using its original successful
  run ID. Published assets are never overwritten by these workflows.
- Expired artifacts: publication stops. Explicitly delete the unpublished draft and rerun Release to
  obtain newly verified assets; do not silently trust an unrelated downloaded binary.
- Bad public release: mark its notes clearly and ship a new patch tag. Keep previous versions
  available rather than replacing files under the same version.

## Helper and verification boundaries

Run `.github/scripts/release.py` from the repository root. Commands that operate on assets require
`--mode signed` or `--mode unsigned`; validation derives it from `.github/release.json`. Artifact
folders are restricted to `macos/build/release`, `windows/build/dist`, `assets`, and `downloaded`.
Toolchain paths are fixed by platform; symlink/traversal redirection is rejected before reads or writes.

Local tests cover both modes' metadata/checksum round trips, mode mismatches, signing disclosures,
invalid paths/arguments and failure with missing signed credentials. `bash -n` and workflow YAML
parsing check syntax. PowerShell parsing is included in release-contract CI on Windows.

The macOS unsigned pipeline was exercised locally: 117 tests passed, and an arm64 DMG was created,
mounted and verified as ad-hoc signed for its image, app and adapter. Signed-mode verification rejected
that ad-hoc DMG. This does not prove launch from a quarantined download or acceptance on another Mac.
Native Windows scripts/build/runtime, credential-backed signing/notarization, and GitHub draft upload/
publication have not been exercised here. See [development.md](development.md) and
[performance.md](performance.md) for native and budget evidence.

Official references: [GitHub secure use](https://docs.github.com/en/actions/reference/security/secure-use),
[Apple notarization](https://developer.apple.com/documentation/security/customizing-the-notarization-workflow),
[Microsoft SignTool](https://learn.microsoft.com/en-us/windows/win32/seccrypto/signtool).

## First tag run: toolchain compatibility

The `v0.1.0` run built the macOS package and passed 117 tests, but its verification failed on
Xcode 26.6: `lipo -verify_arch arm64 <file>` treated the trailing file as another architecture.
Verification now passes the input first (`lipo <file> -verify_arch arm64`), which works with both
toolchains. The failed tag remains unchanged; the corrected workflow uses a new patch tag.
This changes packaging verification only, with no application/runtime or performance-setting change.

The next tag (`v0.1.1`) passed both native package jobs, but the draft contract rejected extra Windows
executables. On Windows, staging in `build/release` aliases CMake's `build/Release` directory, so
`Noitch.exe` and `island_tests.exe` were included alongside the named release asset. Windows now
stages only distribution files in `build/dist`; the contract still rejects unexpected assets.
A regression test checks that staging cannot alias any CMake configuration directory when paths
are compared without case sensitivity. The release is retried under a new patch tag.
