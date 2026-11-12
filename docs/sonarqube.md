# SonarQube review

The [reported check](https://github.com/muhfauzannn/noitch/runs/131303521219) analyzed commit
`7429388` and failed with Security C and Reliability E on new code. The project's
[issue list](https://sonarcloud.io/project/issues?id=muhfauzannn_noitch&branch=main) returned 17 open
bugs/vulnerabilities on 2026-11-12. This document records source changes and two review decisions;
it is not evidence that the remote quality gate has passed.

## Source repairs

| Rule | Findings | Change |
| --- | --- | --- |
| `pythonsecurity:S8705` | 1 | Replace the generic Git command wrapper with fixed operations. Rebuild a canonical tag from validated numeric version components, end Git option parsing, and validate commit SHAs before the ancestry check. |
| `pythonsecurity:S8707` | 1 | Remove the arbitrary toolchain-file argument. Select known toolchain files by platform and allow only the four workflow artifact directories. Reject symlink/traversal redirection before reads or manifest writes. |
| `cpp:S3584` | 4 | Hold Moodle, Claude limits, media properties and artwork results in `unique_ptr`. Transfer ownership only after `PostMessageW` succeeds; failure destroys the result locally. Existing receivers already take ownership through `unique_ptr`. |
| `cpp:S6232` | 6 | Use `std::bit_cast<LONGLONG>` to read the file-size representation after `GetFileSizeEx` succeeds, preserving the positive-size and 1 MB limits without accessing a potentially inactive union member. |
| `cpp:S2193` | 3 | Use integer sample indices for the spring/fade tests and derive floating-point times from them, including the fade endpoint explicitly. |

The Windows message handoff remains asynchronous. These changes clarify its ownership contract;
they do not add a worker, change message routing, or establish a Windows runtime test result.

Release metadata paths are now fixed relative to the repository working directory. Run the helper
from the repository root. `--folder`/`--other` accept only `macos/build/release`,
`windows/build/dist`, `assets`, or `downloaded`. The toolchain path is selected internally from
the platform, with no `--toolchain` override. Hashing reads artifacts in bounded chunks.

## Review: HTTP text in clipboard history

Issue: `AaHQdFNvjMLOmrcLgxh-`, rule `cpp:S5332`,
[Windows Shelf source](../windows/src/features/shelf/shelf_source.cpp).

The flagged literal identifies a copied HTTP URL as a link. `ShelfSource::Capture` reads the local
clipboard, classifies the text and stores its bytes locally. This code makes no HTTP request and
does not follow, fetch or upgrade the URL. Copying the item later reproduces its original bytes.
The macOS implementation also recognizes both HTTP and HTTPS links.

Proposed SonarQube disposition: **False positive**. Replacing the literal with HTTPS or removing
HTTP recognition would change clipboard behavior without securing any network operation. Leave
the source intact and review this specific finding in SonarQube. Its remote status has not been changed.

## Accepted design risk: unattended Moodle credential access

Issue: `AaHNdWEXgpvt7GrdqosL`, rule `swift:S6288`,
[macOS Moodle credential storage](../macos/Noitch/Features/Tasks/MoodleSource.swift).

The owner explicitly chose on 2026-11-12 to retain automatic Moodle checks and document this risk
for SonarQube review. Per-read user-presence authentication would interrupt the approved unattended
check every 30 minutes. This is a design exception to the rule, not a false positive.

The credential is a Moodle API key, stored as a generic password in the login Keychain with
`kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly`. It is device-local, read for each request,
sent only in an HTTPS POST body and never logged. The account is scoped by Moodle host. Signing out
deletes the item; disabling Tasks stops its checks. The Moodle password itself is never stored.
The existing macOS Keychain application-access policy remains in effect.

Residual risk: after the first unlock, the key can be accessed without fresh user authentication,
including while the device is subsequently locked, subject to Keychain/application access controls.
Code able to execute with the app's credential access can use the token. Device-local storage and
TLS do not remove that risk, and `ThisDeviceOnly` does not mean that every read prompts the user.

Review disposition: **Accept the risk for this specific item**, recording the owner's decision and
background-check requirement. A SonarQube reviewer still needs to apply that disposition on the
service. No blanket exclusion, `NOSONAR`, rule deactivation, or quality-gate relaxation is included.
Revisit this choice if unattended checks are removed or the credential's privileges change.

## Verification and remaining work

Eight release-contract tests pass locally, including rejection of malformed Git arguments, unknown
paths, symlink redirection, tampered assets, missing credentials and unrelated source runs. Windows
pure-logic tests, including the SDK macro compilation fixture, pass on macOS. Workflow YAML and
embedded Bash blocks parse locally. Source fixes still require a new SonarQube analysis.

No macOS runtime code changed for this review; its historical budget measurements remain in
[performance.md](performance.md). Windows native build/runtime and before/after budgets cannot be
run on this macOS host. Compiling pure logic does not verify the Windows API changes. Local checks
do not replace MSVC CI, the two service-side issue reviews, or the new SonarQube quality-gate result.
