# Third-Party Notices

This file summarizes third-party code, assets, libraries and services relevant to LabelQt. It is a release checklist aid, not a substitute for the full license texts shipped by each dependency.

## Project License

LabelQt's own source code is licensed under the MIT License. See `LICENSE.txt`.

## Qt

- Source: <https://www.qt.io/>
- Usage: LabelQt is a Qt 6 Widgets application and links to Qt modules such as Core, Gui and Widgets. Qt Svg is used when available for SVG icon/theme support. Qt LinguistTools is used at build time when available.
- License: Qt is available under LGPL/GPL/commercial terms depending on module and distribution model.

Qt source code, Qt SDK files and Qt runtime binaries are not committed to this repository. Binary releases that bundle Qt runtime files must include the required Qt license texts, module/version information and any notices required by the selected Qt license.

The experimental Windows static target is for local validation only. Do not distribute a static Qt build without a separate Qt license review.

## QtKeychain

- Source: <https://github.com/frankosterfeld/qtkeychain>
- Usage: linked as an external library to store automation script secrets in the operating system keychain.
- License: BSD-style license. Use the exact license text shipped with the QtKeychain package used for a binary release.

## LibArchive

- Source: <https://www.libarchive.org/>
- Usage: optional external library for archive reading support when available at build time.
- License: BSD-style license. Include the exact license text from the package used for a binary release when LibArchive is bundled.

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

The dependency is tracked as a Git submodule. Keep the submodule license file available when updating or packaging this dependency.

## Official Automation Scripts

The repository includes official Python automation scripts under `scripts/official`. These scripts are launched as external Python processes and are not embedded into the C++ application binary.

Current script groups:

- `scripts/official/test`: framework examples using only the Python standard library.
- `scripts/official/ocr_preview`: OCR configuration, preview and label-generation scripts.
- `scripts/official/ai_translate`: DeepSeek-compatible AI translation scripts.
- `scripts/official/sdk`: small helper library for LabelQt automation scripts.

Python dependencies are declared in each script group's `requirements.txt`. The repository does not vendor Python wheels, Python runtimes, PaddleOCR models, manga-ocr models or Hugging Face model files. If a release package chooses to bundle any of them, add their exact license texts and notices to the release package.

Known optional Python packages used by official scripts include:

- Pillow
- PaddleOCR
- manga-ocr
- Transformers

Check the installed package metadata for exact versions and licenses before bundling these packages.

## External APIs And Remote Services

The official AI translation script can call a user-configured DeepSeek-compatible API endpoint. The API service, API keys, hosted models and remote runtime are not bundled in this repository.

Users are responsible for configuring credentials and complying with the relevant service terms. Automation script secrets must be stored through LabelQt's QtKeychain-backed keychain path, not in `script.json`, local config files, logs or project files.

## LabelPlus Format And LabelMinus Acknowledgement

LabelQt reads and writes the classic LabelPlus `.txt` project format. It is not an official LabelPlus project.

LabelQt's early product direction, basic editing workflow and parts of the OCR workflow design were influenced by the original WPF project:

- <https://github.com/Yilibala-kid/LabelMinusinWPF>

This C++/Qt branch is an independent implementation and does not contain source code from the original WPF project.
