# Third-Party Notices

This file summarizes third-party code, assets, libraries and services relevant to LabelQt. It is a release checklist aid, not a substitute for the full license texts shipped by each dependency.

## Project License

LabelQt's own source code is licensed under the MIT License. See `LICENSE.txt`.

## Qt

- Source: <https://www.qt.io/>
- Usage: the desktop frontend links Core, Gui, Widgets and Concurrent, with Svg for icon/theme support.
  The mobile preview instead uses Qml, Quick, QuickControls2, Quick Dialogs and Svg over the shared services.
  It uses the system input method, not Qt Virtual Keyboard. Qt LinguistTools is used at build time.
- License: Qt is available under LGPL/GPL/commercial terms depending on module and distribution model.

Qt source code, Qt SDK files and Qt runtime binaries are not committed to this repository. Binary releases that bundle Qt runtime files must include the required Qt license texts, module/version information and any notices required by the selected Qt license.

The Windows static build is an intended official release artifact. Each static release must include the applicable Qt
and statically linked dependency license texts, exact component/version information, corresponding-source offer and
relinking instructions required by the selected licenses. Linux static builds remain experimental and are not official
release artifacts.

## QtKeychain

- Source: <https://github.com/frankosterfeld/qtkeychain>
- Usage: linked as an external library to store automation script secrets in the operating system keychain.
- License: BSD-style license. Use the exact license text shipped with the QtKeychain package used for a binary release.

QtKeychain and the Python runner are desktop-only; they are not linked into the Android preview.

## libarchive (Mobile Transfer)

- Source: <https://github.com/libarchive/libarchive/releases/tag/v3.8.11>
- Usage: ZIP/7z/RAR/tar workspace import and ZIP export in `src/services/transfer`; not linked by the desktop-only target.
- Android: source release 3.8.11, SHA256 `a3dc0872b1b39da34dc18ef1b944f0f11ffd37425d70ae04be679279f1cf43d2`,
  fetched by CMake and statically linked. zlib and liblzma are enabled; tools, XML,
  cryptography and other compression dependencies are disabled. Host mobile tests use system libarchive >= 3.6 with LZMA.
- License: upstream COPYING describes the BSD-family licenses of the component sources. Ship that exact COPYING
  and applicable source notices with a binary distribution; no libarchive SDK/source is committed here.

Test-only RAR fixtures from libarchive 3.8.11 are retained under `tests/fixtures/archives/` with provenance
and the corresponding BSD notices. They contain public upstream test data, not user projects.

## liblzma (Android 7z Import)

- Source: <https://github.com/tukaani-project/xz/releases/tag/v5.8.4>
- Android uses XZ Utils 5.8.4, SHA256 `4ce24038fd4221e0d13bc1a2de7a4db56e90b92b3bf75321f6c14be73f65de4b`
  for `xz-5.8.4.tar.xz`; CMake builds and statically links only liblzma, not the XZ command-line tools or scripts.
- License: liblzma is BSD Zero Clause (0BSD), as described in that release's `COPYING` / `COPYING.0BSD`.
  Preserve the exact source/version and license materials with Android distributions. No SDK/source is vendored.
- Some other XZ distribution components have different licenses; they are not automatically part of this application.

## Android Runtime Dependencies

- The Qt 6.10.2 Android template uses AndroidX Core 1.16.0 (Apache-2.0) for FileProvider. Gradle resolves its
  transitive components; collect their actual licenses/versions for release, not only this direct dependency.
- NDK r27c supplies libc++ (Apache-2.0 with LLVM exceptions) and links platform zlib. Keep the NDK runtime notices.
- No QQ SDK is used. File exchange uses Android intents, the system document picker and scoped read-only URI grants.
- APKs bundle dynamic Qt libraries. Before public distribution, collect the exact Qt module SBOMs, all dependency
  notices and corresponding-source/rebuild instructions, including rebuilding/re-signing with modified Qt.
  A technical-preview build is not a completed license or release audit; see `docs/android-build.md`.

## zlib

- Source: <https://zlib.net/>; compatible system implementations may use <https://github.com/zlib-ng/zlib-ng>.
- Usage: bounded streaming inflation of qCompress-compatible project metadata. No zlib source is vendored.
- License: zlib license, a permissive license compatible with the application's MIT license.
- Windows binary packages must include the exact linked triplet's zlib license and version among native notices.
  Static builds must link the static library. Linux packages use their system library dependency.

## BreezeStyleSheets

- Source: <https://github.com/Alexhuszagh/BreezeStyleSheets>
- Bundled path: `resources/themes/breeze`
- License: MIT
- Local license file: `resources/themes/breeze/LICENSE.md`

The bundled stylesheets provide Breeze-like Qt Widgets themes. The resource directory is committed to this repository, so keep the local license file when updating these assets.

## Material UI / Material Design Icons

- Bundled path: `resources/themes/breeze`
- License: Apache License 2.0
- Local license file: `resources/themes/breeze/MaterialUi.LICENSE`

Some SVG assets included with BreezeStyleSheets carry the Apache License 2.0 notice.

## diff-match-patch

- Source: <https://github.com/google/diff-match-patch>
- Bundled path: `third_party/diff-match-patch`
- Usage: linked from the C++ Qt implementation to produce proofreading text differences.
- License: Apache License 2.0
- Local license file: `third_party/diff-match-patch/LICENSE`

The dependency is tracked as a Git submodule. Keep the submodule license file available when updating this dependency.

Release packages should include third-party license texts under a `licenses/` directory:

- `licenses/BreezeStyleSheets-LICENSE.md`
- `licenses/MaterialUi-LICENSE.txt`
- `licenses/diff-match-patch-LICENSE.txt`

Windows shared and static packages additionally collect the exact vcpkg triplet's native dependency notices under
`licenses/native/` and include `QT_SOURCE_OFFER.txt` pointing to the corresponding Qt source archive controlled by the
LabelQt project for that release. Static packages additionally include `STATIC_LINKING_NOTICE.txt` and `RELINKING.md`.

## Mobile Icons

The mobile frontend bundles selected unmodified SVG icons from [Lucide 0.468.0](https://github.com/lucide-icons/lucide/tree/0.468.0/icons).
They use the ISC license with upstream attribution to Feather (MIT) portions and Lucide contributors.
The original license is retained in `src/mobile/qml/icons/LICENSE` and embedded alongside the APK resources.
These assets replace font-dependent Unicode symbols; no icon font or remote asset loading is required.

## Official Automation Scripts

The repository includes official Python automation scripts under `scripts/official`. These scripts are launched as external Python processes and are not embedded into the C++ application binary.

Current script groups:

- `scripts/official/test`: native v2 development examples; excluded from release packages.
- `scripts/official/sdk`: the MIT-licensed, standard-library-only Python 3.10+ task SDK.

The old OCR/translation scripts and their dependencies have been removed. No Python runtime, wheels or model
weights are vendored. New extensions must declare their own dependencies and license notices.

## External APIs And Remote Services

The v2 examples do not contact remote services. User extensions may do so; users are responsible for the relevant
service terms. Secrets must use LabelQt's QtKeychain-backed path, not JSON, logs or project files.
Process isolation is not an operating-system sandbox.

## LabelPlus Format And LabelMinus Acknowledgement

LabelQt reads and writes the classic LabelPlus `.txt` project format. It is not an official LabelPlus project.

LabelQt's early product direction, basic editing workflow and parts of the OCR workflow design were influenced by the original WPF project:

- <https://github.com/Yilibala-kid/LabelMinusinWPF>

This C++/Qt branch is an independent implementation and does not contain source code from the original WPF project.
