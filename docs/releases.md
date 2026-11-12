# Releases

Noitch ships a macOS arm64 DMG and a Windows x64 **portable executable**, not an installer.
Stable tags use `vMAJOR.MINOR.PATCH` (no leading zeroes; each component at most 65535).
Both products receive the tag's version at build time; Windows also embeds VERSIONINFO and a
matching assembly version. Local builds keep the default development version.

## One-time repository setup

Create these GitHub environments **before pushing release tags**:

- `release-signing`: restrict deployment to protected `v*` tags; add required reviewers if desired.
- `release-publish`: restrict deployment to `main`, require a release reviewer, and prevent self-review.
  The reviewer checks native smoke-test and performance evidence before approval.

Protect `main` with required CI checks and review. Add a tag ruleset for `v*` restricting creation,
updates and deletion to release maintainers. Do not allow untrusted branches to use either environment.
Only trusted, reviewed code reachable from `main` may run the signing jobs.
Draft inspection/verification jobs need `contents: write` because GitHub restricts draft visibility
to callers with push access; signing jobs themselves retain `contents: read`. Protect changes to release
workflows and scripts with review as well. The signing/publication workflows have no PR trigger and never expose signing
secrets to fork PRs. A separate release-contract CI runs credential-free checks on PRs. Environment protections and certificates require owner configuration; YAML alone
cannot enforce repository settings.

Set the following **repository variables** (verification jobs need them without signing access):

| Variable | Value |
| --- | --- |
| `MACOS_BUNDLE_ID` | Permanent reverse-DNS distribution identifier you control; `app.noitch.mac` is rejected |
| `MACOS_TEAM_ID` | Apple Developer team ID |
| `WINDOWS_SIGNING_THUMBPRINT` | Expected Authenticode leaf certificate thumbprint |
| `WINDOWS_TIMESTAMP_URL` | Your certificate provider's RFC 3161 timestamp URL |

Changing the macOS bundle identifier changes application identity, including preferences, Keychain
access and login registration. Choose it before the first public release. No migration is included.

Set these **secrets in `release-signing`**, never in source or workflow inputs:

| Secret | Value |
| --- | --- |
| `MACOS_SIGN_IDENTITY` | Full `Developer ID Application: … (TEAMID)` identity |
| `MACOS_CERTIFICATE_BASE64` | Base64 PKCS#12 containing Developer ID Application certificate and private key |
| `MACOS_CERTIFICATE_PASSWORD` | PKCS#12 password |
| `APPLE_API_KEY_BASE64` | Base64 App Store Connect API private `.p8` key authorized for notarization |
| `APPLE_API_KEY_ID` | API key ID |
| `APPLE_API_ISSUER` | API issuer UUID (team API key) |
| `WINDOWS_CERTIFICATE_BASE64` | Base64 PKCS#12 with a trusted Authenticode certificate and private key |
| `WINDOWS_CERTIFICATE_PASSWORD` | PKCS#12 password |

Windows signing here supports an **existing exportable PFX**. Many current public code-signing
certificates require hardware or a managed signing service and cannot be exported. A hardware-only
certificate cannot use this path: agree on an approved provider/integration before changing it.
This implementation adds no signing service or dependency. Never substitute a self-signed certificate
for public distribution. Valid signing does not guarantee that SmartScreen reputation warnings disappear.

macOS imports keys into a temporary keychain; Windows imports the configured PFX into the ephemeral
hosted runner's user store. Scripts remove temporary files and imported keys in cleanup. Hosted runners
are discarded even after cancellation. Never use these scripts on persistent self-hosted runners
without a separate key-management review. Missing credentials or failed signing/notarization abort
release creation; there is no unsigned fallback.

## Create a draft

After reviewing the exact commit and CI results, a maintainer pushes its `vX.Y.Z` tag. Do not move
release tags. `.github/workflows/release.yml` then:

1. Validates tag syntax, tagged checkout and ancestry on `main`; refuses any existing release,
   including drafts. API errors stop the job rather than being treated as “not found”.
2. Tests and builds both platforms from that commit on separate hosted runners, then signs them.
   macOS signs the embedded framework before the app, keeps Hardened Runtime and existing
   entitlements, includes the adapter's BSD license, notarizes/staples the app, creates the DMG,
   then signs/notarizes/staples the DMG. It does not use `codesign --deep` for signing.
3. Verifies final architecture, versions and signing identity. macOS checks package contents,
   stapled tickets and Gatekeeper assessment; Windows checks Authenticode trust, timestamp,
   signer thumbprint and PE architecture with PowerShell and SignTool.
4. Uploads both final artifacts plus separate JSON metadata with source commit, tag, toolchain,
   SHA-256 and run identity. The macOS metadata includes the vendored adapter binary's checksum.
   CI artifacts may be replaced on a build retry; GitHub release assets are never overwritten.
5. Creates a **draft** only after both platform jobs succeed. Uploads assets without overwrite,
   downloads them again and compares their exact hashes with the build outputs.
6. Downloads each native asset on a fresh corresponding OS runner and repeats package/signature
   verification without signing secrets. This is package verification, not a GUI smoke test.

A draft contains five assets: the DMG, EXE, two platform JSON files and `SHA256SUMS`. Checksums cover
both final signed binaries and metadata. Metadata provides traceability; it is not a signed provenance
attestation and the pipeline does not promise bit-for-bit reproducible signed/notarized builds.

## Verify and publish

Download the draft onto clean macOS and Windows installations. Confirm install/copy, launch, media
adapter behavior, expand/collapse, click-through and quit. Check the minimum OS versions claimed for
release; this workflow does not certify older OS versions. Run the documented local performance
checks on real hardware and record configuration, before/after numbers and any accepted exceptions.
GitHub shared runners are not performance evidence. Do not approve production distribution while
required native checks are missing; Windows has no recorded native runtime validation yet.

Run **Publish verified release** from `main`, supplying the tag and the successful **Release** workflow
run ID. The publisher checks that this was a successful tag-triggered run of the expected workflow
for the exact tagged commit reachable from `main`. After environment approval, it retrieves that run's
original artifacts, rechecks metadata and hashes, compares all downloaded draft assets against them,
and publishes the existing draft. It never rebuilds or re-signs. Build artifacts expire after seven
days; publish within that window. Do not use the GitHub UI's direct Publish button, which bypasses
these checks. GitHub release editing permissions remain a trusted-maintainer boundary.

The two workflows serialize on the same tag. Publication needs an explicit dispatch and environment
approval; tag creation alone never publishes publicly. No repository settings, secrets, tags or
releases are created by a local implementation of this change.

## Failures and retries

- Invalid tag or non-main commit: rejected before signing. Use a new correct tag; do not move an
  existing release tag.
- Missing credentials or one platform fails: no draft is created. Correct configuration and rerun
  the failed jobs. Full reruns are also possible while no release exists.
- Draft creation/upload or native re-verification fails: the draft stays private. Rerun the failed
  job where possible. Never use `--clobber`. If a partial upload prevents a retry, inspect the failure,
  explicitly delete only the unpublished draft, then rerun the complete Release workflow. Do not delete
  its tag. Partial drafts cannot pass the publish workflow's successful-run and checksum checks.
- Existing release: a full Release rerun stops. Publish an existing verified draft using its original
  successful run ID; a public release is never overwritten by these workflows.
- Expired build artifacts: the publisher fails closed. Explicitly delete the unpublished draft and
  rerun Release to obtain new verified assets. Never silently reuse an unverified downloaded binary.
- A published release is bad: do not replace its files under the same version. Mark it clearly in
  release notes and ship a new patch tag; users can download the previous known-good release.

## Local checks and current limits

`python3 .github/scripts/test_release.py` exercises invalid versions, mismatched commits, missing
platform outputs, changed final bytes, tampered downloaded checksums, missing signing credentials,
and unsuccessful or unrelated publication source runs. `bash -n macos/scripts/{release,verify-release}.sh` checks
shell syntax. YAML parsing is only syntax validation, not execution of GitHub Actions.

End-to-end signing, Apple notarization, draft uploads and publication require configured credentials
and GitHub execution. They have not been exercised locally. Windows scripts and VERSIONINFO have
not been built or run on Windows here. See `development.md` and `performance.md` for native evidence;
no existing runtime or performance gaps are cleared by adding CD.

Official references: [GitHub secure use](https://docs.github.com/en/actions/reference/security/secure-use),
[Apple notarization](https://developer.apple.com/documentation/security/customizing-the-notarization-workflow),
[Microsoft SignTool](https://learn.microsoft.com/en-us/windows/win32/seccrypto/signtool).
