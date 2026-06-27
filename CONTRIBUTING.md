# Contributing

LabelQt is an independent C++/Qt 6 LabelPlus text project editor. Keep changes aligned with the current architecture and cross-platform goal.

## Code Style

- Use C++23 and Qt 6 Widgets.
- Class names use `PascalCase`.
- Functions use `camelCase`.
- Member variables use the `m_` prefix.
- Prefer small classes and explicit ownership through Qt parent/child relationships.
- For composite Qt widgets, disconnect signals from child/internal widgets before destruction if those signals can fire while owned objects are tearing down.
- When caching raw pointers owned by Qt containers or parent objects, clear or null the cache before the owner clears/destructs. Prefer `QPointer` for cached `QObject`/`QWidget` references used across callbacks, queued events or delayed deletion.
- Avoid clearing or rebuilding a `QMenu`/`QAction` hierarchy from a slot currently triggered by one of its own actions. If a running workflow needs to disable entries, update existing actions in place or defer the rebuild until the action call stack has returned.
- Run `clang-format` with the repository `.clang-format` before committing substantial C++ changes.
- Do not run `clang-format` on CMake files such as `CMakeLists.txt` or `CMakePresets.json`; keep CMake formatting manual and consistent with the surrounding file.
- Do not introduce WPF, .NET or Windows-only dependencies on this branch.

## Architecture Boundaries

- `src/core`: platform-independent models, parsing, preferences and undo infrastructure.
- `src/ui`: Qt Widgets UI classes.
- `src/services`: application services such as project workflow, session persistence, backup, OCR, archive and process handling.
- `translations`: Qt `.ts` files.
- `preference.json`: runtime UI tuning defaults.

See `docs/architecture.md` for more detail.

## Project Metadata In LabelPlus Comments

- LabelQt-specific project metadata must be stored in the unified compressed `LabelQtMetadata` comment block.
- Use `ProjectMetadataService` for comment block encoding, decoding and rewriting.
- Do not add new standalone `LabelQt...` comment blocks for future metadata sections.
- The block is compressed and base64-encoded for compactness and text safety. It is not encrypted; do not store secrets
  such as API keys in project metadata.

## Qt Licensing Constraints

- Keep the application on LGPL-available Qt modules unless the project intentionally changes to a GPL-compatible release strategy.
- Current required Qt dependencies should stay limited to `Qt6::Core`, `Qt6::Gui` and `Qt6::Widgets` unless a new module is reviewed. `Qt6::Svg` is an optional enhancement for bundled stylesheet icons.
- Do not introduce Qt GPL-only modules without documenting the license impact and getting an explicit project decision.
- Prefer dynamic linking for Qt in release packaging.
- The Windows `LabelQtStatic` target is experimental and local-only. Do not publish static Qt binaries without a Qt license review.
- Do not commit Qt source code, Qt SDK files or bundled Qt binaries into this repository.
- Binary releases must include Qt license notices, Qt module/version information and third-party dependency notices.
- When adding a third-party dependency, document its license and keep it compatible with the intended project license.
- Bundled BreezeStyleSheets resources must keep their local license files and `THIRD_PARTY_NOTICES.md` entry in sync.
- When adding bundled assets, official automation scripts, Python requirements, external API integrations or optional
  native libraries, update `README.md`, `docs/porting-dependencies.md` and `THIRD_PARTY_NOTICES.md` in the same change.

## UI And Workflow Separation

- Keep `MainWindow` as an orchestration layer for menus, widgets, signal/slot wiring and UI feedback.
- Do not add new file-format parsing, project workflow, label mutation or session persistence logic directly to `MainWindow`.
- Put non-widget workflow logic in `src/services`; UI classes can call services and then refresh controls.
- Keep asynchronous image display work in `ImagePageViewController`: cache lookup, pending request IDs, loaded-image
  validation, adjacent-page preloading and delayed view restoration belong there.
- Keep page navigation widget refresh in `ProjectViewController`, and keep table/canvas/current-label selection
  synchronization in `LabelSelectionController`.
- Keep active text editor detection, commit and restore logic in `EditorStateController`.
- Keep reusable preference controls in small helpers or widgets. Table-style preference pages should live in focused
  widgets such as `GroupStyleEditorWidget` or `AutomationShortcutEditorWidget` instead of inline blocks in
  `PreferenceDialog`.
- Table models should emit edit requests and leave project mutation to controllers.
- When a current-page label edit only changes marker/table/editor state, refresh labels in place instead of reloading the image.
- Reserve full image reloads for real page/image changes, project open/creation, and explicit session restore paths.

## UI Text And i18n

- User-visible UI strings must use `tr()`.
- When adding or changing a `tr()` string, update all of the 4 files:
  - `translations/labelqt_zh_CN.ts`
  - `translations/labelqt_zh_TW.ts`
  - `translations/labelqt_en_US.ts`
  - `translations/labelqt_ja_JP.ts`
- Then run:

```bash
python scripts/check_translations.py
cmake --build --preset linux-debug --target release_translations
```

## Preferences

- Configurable UI behavior should go through `AppPreferences`.
- Defaults should live in `preference.json`.
- Preference dialog controls should preserve the same JSON shape that `AppPreferences` reads.
- Preference dialog fallback values should come from `AppPreferences` defaults instead of another hand-written copy.
- Group colors are assigned by group index. If a group has no configured color, text UI uses the default color and image markers use black.

## Undo

- New reversible project edits must use the Qt-backed `UndoStack` wrapper, which is implemented with `QUndoCommand` and `QUndoStack`.
- When adding a feature that changes labels, groups, pages or project data, add undo and redo behavior in the same change.
- Avoid adding one-off undo state in UI code.
- Label and group edits should go through `LabelEditController` so normal edits and undo replay use the same apply helpers.
- Prefer small undo callbacks that call shared apply helpers, so table edits, canvas edits and future batch tools keep the same behavior.
- User-configurable shortcuts, including undo/redo shortcuts, should go through `AppPreferences`, `preference.json` and the preference dialog.

## Session And Local State

- Store machine-local layout and per-project resume state with `QSettings` through `SessionStateStore`.
- Do not write local window geometry, splitter positions or last-viewed page back to `preference.json`.

## Verification

For normal development:

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
python scripts/check_translations.py
```

For quick pre-commit checks:

```bash
python scripts/check_fast.py
```

For the full local check set:

```bash
python scripts/check_all.py
```

`check_all.py` chooses the default debug preset for the current platform. You can pass a preset explicitly when needed,
for example on Windows with Visual Studio:

```bash
python scripts/check_all.py windows-vs-debug
```

Or use an environment variable:

```bash
LABELQT_CHECK_PRESET=windows-vs-debug python scripts/check_all.py
```

For Qt model/view or widget interaction changes:

- Add `QAbstractItemModelTester` coverage for new or changed `QAbstractItemModel` classes when practical.
- Add lightweight `QTest` widget interaction tests for bug-prone mouse/keyboard behavior, especially canvas marker
  selection/movement, drag/drop ordering and table filtering.
- Keep QWidget tests runnable in headless environments through the test target's offscreen platform setting.

If ccache interferes with CMake compiler detection:

```bash
cmake -E env CCACHE_DISABLE=1 cmake --preset linux-debug
cmake -E env CCACHE_DISABLE=1 cmake --build --preset linux-debug
cmake -E env CCACHE_DISABLE=1 ctest --preset linux-debug
```

## Commit Messages

Commit messages should be written in Chinese and follow the repository template in `.gitmessage.txt`. Use a
Conventional Commits-style title, and keep the body sections `背景`、`改动`、`验证`、`风险` when they are relevant.

To enable it locally:

```bash
git config commit.template .gitmessage.txt
```

Use the title to summarize the change, then keep the background, changes, verification and risk notes accurate. Remove
empty template sections before committing.
