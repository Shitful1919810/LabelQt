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

## Porting Direction

- Prefer Qt Widgets for the main desktop workflow.
- Keep platform-independent logic under `src/core`.
- Put project workflow, session state, OS, archive, OCR and process integration under `src/services`.
- Keep Qt UI classes under `src/ui`.
- Avoid reintroducing WPF, .NET or Windows-only dependencies on this branch.
- Avoid Qt GPL-only modules unless the user explicitly approves the license impact.

## Required Conventions

- Follow `CONTRIBUTING.md` and `docs/architecture.md`.
- Commit messages should be written in Chinese using the repository `.gitmessage.txt` structure: title, background,
  changes, verification, and risk notes. Do not leave empty template sections in the final commit message.
- Never run `clang-format` on CMake files such as `CMakeLists.txt` or `CMakePresets.json`; format CMake changes manually to match the existing style.
- New user-visible UI text must use `tr()`.
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
- User-configurable shortcuts should go through `AppPreferences`, `preference.json` and the preference dialog.
- Automation script secrets such as API keys must go through QtKeychain-backed keychain storage. Do not write secrets to
  `script.json`, `config.json`, automation `input.json`/`output.json`, logs or preference files.
- The Windows `LabelQtStatic` target is experimental and local-only; do not make it an official release artifact without Qt license review.
- Do not commit bundled Qt SDK files, Qt source code or Qt runtime binaries into the repository.
