# Agent Guidance

This project is an independent C++/Qt implementation of a LabelPlus text project editor. The old WPF implementation is not part of this branch.

## Encoding

The project may contain Chinese UI strings, documentation and file names. Keep text files in UTF-8 and avoid broad search-and-replace operations that might damage localized text.

## Build

Use the CMake presets when possible:

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

If the local compiler is routed through ccache and CMake fails while detecting `Threads`, retry with:

```bash
cmake -E env CCACHE_DISABLE=1 cmake --preset linux-debug
cmake -E env CCACHE_DISABLE=1 cmake --build --preset linux-debug
cmake -E env CCACHE_DISABLE=1 ctest --preset linux-debug
```

For routine validation, prefer the unified helper entry:

```bash
python scripts/devtool.py check --mode fast
python scripts/devtool.py check --mode all --preset linux-debug
```

When entering an unfamiliar area, use the module router before reading large files:

```bash
python scripts/devtool.py module-map proofread
python scripts/devtool.py module-map automation
python scripts/devtool.py module-map selection
```

See `docs/devtools.md` for the current helper commands.

## Porting Direction

- Prefer Qt Widgets for the main desktop workflow.
- Android has a technical-preview frontend, not yet a supported release target. Read `docs/android-development.md`
  for implementation contracts, `docs/android-user-guide.md` for current behavior, `docs/android-port-plan.md`
  for future scope, and `docs/android-build.md` for toolchain and validation before Android work.
- Keep desktop Widgets and Android Qt Quick/QML as separate frontends over shared C++ services. Do not shrink
  `MainWindow` into a mobile UI or duplicate editing, undo, proofreading and persistence rules in QML/JNI.
- Keep platform-independent logic under `src/core`.
- Put project workflow, session state, OS, archive, OCR and process integration under `src/services`.
- Keep desktop Qt UI classes under `src/ui`; mobile presentation and Android bridges follow
  `src/mobile` and `src/platform/android` boundaries in the Android plan.
- Avoid reintroducing WPF, .NET or Windows-only dependencies on this branch.
- Avoid Qt GPL-only modules unless the user explicitly approves the license impact.

## Android Collaboration Contract

- Keep Android user-facing behavior and engineering contracts synchronized with the two dedicated guides above.
  Use `python scripts/devtool.py module-map android` to find their entry points and shared production modules.
- Reuse `WorkspaceBrowser.qml` for home/drawer management and `SubmitDialog.qml` for validated submissions.
  Keep label model updates incremental when visible identities are unchanged; derive group styles from the complete
  project group order, not the filtered list. Selection or text edits must not recreate gesture targets.
- Keep workspace identity, display title and suggested share filename separate. Renaming a title must not rename
  project/image files. Coordinate active-workspace deletion, timer shutdown and serialized transfer maintenance;
  never treat user workspaces or recovery evidence as disposable cache.
- Follow the scope, phase gates and failure semantics in `docs/android-port-plan.md`; track implementation in `TODO.md`.
  Distinguish the working preview from the uncompleted full-release milestones. Validate APK packaging, host mobile
  tests and desktop regression tests separately; a host QML test is not an Android/QQ device test.
- The first Android release edits imported app-owned workspace copies. External document URIs are opaque capabilities,
  not filesystem paths; never pass them to local path canonicalization, lock files or rename transactions.
- Keep local save, recovery checkpoint, export and handoff-to-another-app as distinct states. Never report a QQ transfer
  as delivered based on a chooser or Activity callback. Do not overwrite the original imported external files.
- Share project ZIPs through Android's standard intents and narrowly scoped read-only FileProvider access. Receiving
  from QQ and manual system-picker import must use the same bounded, validated import service. Never scan QQ's private
  directories, request all-files access as a workaround, or implement archive parsing in UI/JNI code.
- Treat archive entries, TXT image references, names, MIME types and incoming intents as untrusted. Reject traversal,
  links, collisions and resource-budget violations before publishing a workspace; preserve existing projects on failure.
- Keep active documents and undo on their owner thread. Background work uses snapshots and validates session/version
  on return. Coordinate saves, recovery, rename and export in services, not merely by disabling mobile buttons.
- Extract toolkit-independent selection, editor and image-display coordination from existing UI controllers when needed;
  retain thin Widgets/Quick adapters and desktop regression coverage. Do not create hidden Widgets to reuse controllers.
- Mobile touch interaction must not require hover, right-click or modifier keys. Define gesture cancellation and IME
  composition behavior, and test rotation, keyboard resizing, background process death and delayed results.
- Disabling mobile Python must not remove shared identity/version/commit infrastructure or break desktop automation.
- New QML text uses `qsTr()`/`qsTranslate()` and all four translations. Keep translation extraction/checks covering
  QML as well as C++ and verify runtime contexts, especially namespaced QObject classes.
- Keep Android source under the planned mobile/platform boundaries; review new Qt/archive dependencies and APK licensing
  before introduction. Use the system IME, not an unreviewed Qt Virtual Keyboard dependency. Never commit signing keys.

## Required Conventions

- Follow `CONTRIBUTING.md` and `docs/architecture.md`.
- Use `scripts/devtool.py module-map <topic>` to locate the intended module boundary before adding logic to a large UI class.
- Commit messages should be written in Chinese using the repository `.gitmessage.txt` structure: title, background,
  changes, verification, and risk notes. Do not leave empty template sections in the final commit message.
- Never run `clang-format` on CMake files such as `CMakeLists.txt` or `CMakePresets.json`; format CMake changes manually to match the existing style.
- New user-visible C++ UI text must use `tr()`; QML text follows the Android translation contract above.
- When adding or changing `tr()` strings, update `translations/labelqt_zh_CN.ts`, `translations/labelqt_zh_TW.ts`, `translations/labelqt_ja_JP.ts` and `translations/labelqt_en_US.ts`.
- Run `python scripts/check_translations.py` after UI text changes.
- Configurable UI behavior should go through `AppPreferences` and `preference.json`.
- Reversible project edits must use the Qt-backed `UndoStack`; add undo and redo behavior in the same change that introduces the edit.
- Label edits should go through `LabelEditController` rather than adding new label mutation paths in `MainWindow`.
- Keep `MainWindow` focused on UI orchestration; put project workflow, session state and mutation logic in services.
- Keep image loading, image-cache coordination, adjacent-page preloading and delayed viewport restoration in
  `ImagePageViewController`; do not reintroduce ad hoc image-load request state in `MainWindow`.
- Keep page combo box/source-label refresh in `ProjectViewController`, and keep table/canvas selection synchronization in
  `LabelSelectionController`.
- Keep text editor focus/commit/restore behavior in `EditorStateController`; `MainWindow` should call it rather than
  duplicating active-editor detection.
- Keep preference subpage table logic in focused widgets such as `GroupStyleEditorWidget` and
  `AutomationShortcutEditorWidget`; do not grow large table-building blocks back into `PreferenceDialog`.
- Current-page label changes should update table/marker state in place and must not reload the image unless the current image actually changes.
- Model/view changes, especially drag/drop or filtering in table/list models, should include Qt model tests with
  `QAbstractItemModelTester` where practical.
- Composite Qt widgets that connect signals from child/internal widgets must disconnect those internal signal connections during destruction before owned child objects start tearing down.
- If code caches raw pointers owned by Qt containers or parent objects, such as `QGraphicsScene` items or child widgets, clear or null those cached pointers before the owner clears/destructs. Use `QPointer` for cached `QObject`/`QWidget` references whose lifetime may end outside the current synchronous scope.
- Do not destroy or rebuild a `QMenu`/`QAction` tree from inside a slot triggered by one of its own actions. During long-running or nested-event-loop workflows, update enabled/visible state in place; rebuild menus only after the triggering call stack has unwound.
- New Qt modules and third-party dependencies must be checked for license compatibility and documented.
- Text diff for proofreading must go through `TextDiffService`. The current `diff-match-patch` Qt/C++ port may corrupt
  memory when `diff_cleanupSemanticLossless()` is called on some Chinese text pairs, and
  `diff_cleanupSemantic()` calls that API internally. Do not call either cleanup API outside the `TextDiffService`
  strategy gate unless the port is replaced and regression tests prove the issue is gone.
- Project-internal metadata stored in LabelPlus comment lines must go through the compressed `LabelQtMetadata` block
  handled by `ProjectMetadataService`. Do not add new standalone `LabelQt...` comment blocks.
- User-configurable shortcuts should go through `AppPreferences`, `preference.json` and the preference dialog.
- Automation script secrets such as API keys must go through QtKeychain-backed keychain storage. Do not write secrets to
  `script.json`, `config.json`, automation `input.json`/`output.json`, logs or preference files.
- Windows dynamic and static builds, plus the Linux RPM, are the intended official binary release artifacts. A Windows
  static release must pass the dedicated Qt/static-dependency license and relinking review before publication.
- Linux static builds and DEB packages are experimental and must not be presented as supported release artifacts.
- Do not commit bundled Qt SDK files, Qt source code or Qt runtime binaries into the repository.
