<div align="center">

# Noitch

A native island for your music, tasks, files, and clipboard.

[![macOS CI](https://github.com/muhfauzannn/noitch/actions/workflows/macos.yml/badge.svg?branch=main)](https://github.com/muhfauzannn/noitch/actions/workflows/macos.yml)
[![Windows CI](https://github.com/muhfauzannn/noitch/actions/workflows/windows.yml/badge.svg?branch=main)](https://github.com/muhfauzannn/noitch/actions/workflows/windows.yml)

[![Download for macOS](https://img.shields.io/badge/Download-macOS_arm64-111827?style=for-the-badge&logo=apple&logoColor=white)](https://github.com/muhfauzannn/noitch/releases)
[![Download for Windows](https://img.shields.io/badge/Download-Windows_x64-0078D4?style=for-the-badge)](https://github.com/muhfauzannn/noitch/releases)

Release downloads are pending. These buttons open the Releases page.

[Features](#features) · [Get started](#get-started) · [Documentation](#documentation) · [Contributing](#contributing)

</div>

Noitch brings a Dynamic Island style overlay to macOS and Windows. Hover over the island at the top
of your screen to see what's playing, follow lyrics, check AI usage, or pick up a file you left on
Shelf. Move away to collapse it.

Two separate native apps: Swift and AppKit on macOS, C++ and Win32 on Windows. Animations run in the
OS compositor, with performance budgets tracked in the repository.

> [!IMPORTANT]
> Noitch is in early development. macOS has local build and test coverage, with known performance
> and interaction gaps. The Windows app has not yet been run on Windows. CI badges show build/test
> status; they do not establish runtime readiness. See the [verification status](docs/development.md#verification-status).

## Features

| | What you can do |
| --- | --- |
| **Music & lyrics** | See artwork, control playback, seek through a track, and open synced lyrics with romanization. |
| **AI usage** | View Claude Code and Codex usage from local logs, with Claude account limits when authenticated. |
| **Tasks** | Connect a Moodle site to see assignments, quizzes, deadlines, and reminders. |
| **File shelf** | Drop files onto the island, then copy or drag them into another app. Pin items you want to keep. |
| **Clipboard history** | Reuse up to 50 text, link, image, or file entries. Pause recording or clear unpinned history. |
| **Calendar on macOS** | See upcoming calendar events beside your music. Calendar integration is not implemented on Windows. |

These features are implemented in both codebases except where noted. Native Windows behavior remains
unverified; the platforms also differ in some animations and image previews. See the
[architecture](docs/architecture.md) for details.

## Get started

### Downloads

There are no published releases yet. Build from source below to try Noitch.

The current release mode needs no signing certificates. It produces a macOS arm64
`-adhoc.dmg` (**Ad-hoc signed, not notarized**) and a Windows x64 portable `-unsigned.exe`
(**Unsigned**). These builds do not verify a publisher identity. macOS Gatekeeper and Windows
SmartScreen/Smart App Control may warn or block launch; managed devices may prevent opening them.
See [Apple's opening guidance](https://support.apple.com/en-us/102445) and
[Microsoft's reputation guidance](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation).

Both platforms still run build/tests, version and architecture checks, and checksum verification
before publication. A certificate-backed mode remains available. Native release validation is still
required. Published builds will appear on the [Releases page](https://github.com/muhfauzannn/noitch/releases).
Maintainers can follow the [release guide](docs/releases.md).

### Build from source

Clone the repository first:

```sh
git clone https://github.com/muhfauzannn/noitch.git
cd noitch
```

#### macOS

Requires Xcode 26 or later. The deployment target is macOS 14+, but older supported OS versions and
Intel Macs have not been validated. Local verification and release packaging target Apple silicon.

```sh
xcodebuild -project macos/Noitch.xcodeproj -scheme Noitch \
  -configuration Release -destination 'platform=macOS,arch=arm64' \
  -derivedDataPath macos/build CODE_SIGN_IDENTITY=- build
open macos/build/Build/Products/Release/Noitch.app
```

This creates a locally ad-hoc-signed build. It is not a notarized distribution build.

#### Windows

Requires Visual Studio 2022 or later with **Desktop development with C++**, and CMake 3.25+.
Windows 10 version 1809+ is the intended minimum and remains untested.

```powershell
cd windows
cmake --preset default -A x64
cmake --build --preset release
.\build\Release\Noitch.exe
```

The executable runs directly, without an installer. Local builds are unsigned.

### First use

1. Quit other notch overlays so they do not compete for the same screen area.
2. Hover over the island to expand it. Use the tabs to switch between Home, Usage, Tasks, and Shelf.
3. Open Settings with the gear button or the island's right-click menu. Connect Moodle or adjust
   clipboard recording there as needed.
4. Right-click the island and choose Quit to exit. On macOS, Noitch has no Dock or menu bar icon.

## Privacy & integrations

Clipboard history is stored locally and persists between launches. It is ordinary application data,
not an encrypted vault; unmarked secrets can enter history. Pause stops recording, and Clear keeps
pinned entries. Shelf references original file paths rather than moving your files.

Lyrics lookup sends track information to LRCLIB. Moodle tasks contact the site you configure.
AI usage totals are read from local logs; Claude limits contact Anthropic using your Claude Code
login. Calendar access on macOS requires permission. Noitch does not require a separate Noitch account.

macOS clipboard recording checks for changes once per second while enabled; Windows uses clipboard
notifications. The macOS media integration uses a bundled MediaRemoteAdapter and undocumented system
APIs, which an OS update may break. See [integration and storage details](docs/architecture.md).

## Performance

Noitch uses Core Animation on macOS and DirectComposition on Windows. The app hands animations to
the compositor and builds feature content when needed. The two platforms share a behavior spec and
keep their implementations independent.

| Idle budget | macOS | Windows |
| --- | --- | --- |
| Memory | 12 MB | 30 MB, provisional |
| CPU | 20 ms/min | 20 ms/min |
| Wakeups | 0.5/second | 0.5/second |

These are targets. The latest macOS CD verification measured 10.66 MB, 38.25 ms CPU/min, and 1.13
wakeups/second, exceeding the CPU and wakeup budgets. Clipboard recording and playback affect the
results. Windows has no runtime measurements yet. See the [measurement history and method](docs/performance.md)
for configuration, exceptions, and outstanding checks.

## Contributing

Bug reports, documentation fixes, and native-platform testing are welcome. For a bug report,
[open an issue](https://github.com/muhfauzannn/noitch/issues) with your OS version, CPU architecture,
build or commit, steps to reproduce, and expected versus actual behavior. Redact credentials and
clipboard contents from logs and screenshots.

Before a substantial change, open an issue to discuss the scope. Read [AGENTS.md](AGENTS.md) and the
[feature guide](docs/adding-features.md): feature changes normally apply to both platforms, runtime
work must remain event-driven, and new dependencies require discussion. Include validation results
and state which platform checks you could not run.

Build and test commands are in the [development guide](docs/development.md). Changes to window,
hover, or animation code also require the local interaction and performance checks described there.

## Documentation

| Guide | Contents |
| --- | --- |
| [Development](docs/development.md) | Build, test, demo modes, and verification status |
| [Architecture](docs/architecture.md) | Features, lifecycles, integrations, and shared behavior |
| [Performance](docs/performance.md) | Budgets, measurements, and known failures |
| [Tech stack](docs/tech-stack.md) | Native stack choices and measured tradeoffs |
| [Adding features](docs/adding-features.md) | How to extend both apps within the performance constraints |
| [Releases](docs/releases.md) | Signing, packaging, publication, and recovery |

## Credits & license

Noitch's macOS media integration includes
[MediaRemoteAdapter](https://github.com/ungive/mediaremote-adapter), distributed under its
[BSD 3-Clause license](macos/Vendor/MediaRemoteAdapter/LICENSE). Lyrics come from
[LRCLIB](https://lrclib.net).

A project-wide license for Noitch has not been selected yet. The adapter's license applies to that
component; it does not license the rest of the repository.
