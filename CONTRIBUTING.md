# Contributing

LabelQt is an independent C++/Qt 6 LabelPlus text project editor. Keep changes aligned with the current architecture and cross-platform goal.

## Code Style

- Use C++23. Desktop UI uses Qt 6 Widgets; the Android preview uses Qt Quick/QML over the same services.
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
- `src/ui`: desktop Qt Widgets UI classes.
- `src/services`: application services such as project workflow, session persistence, backup, OCR, archive and process handling.
- `translations`: Qt `.ts` files.
- `preference.json`: runtime UI tuning defaults.

See `docs/architecture.md` for more detail.

## Android Port Collaboration

The Android design baseline is [docs/android-port-plan.md](docs/android-port-plan.md). The technical preview is built
using [docs/android-build.md](docs/android-build.md); it is not yet the complete mobile release described by the plan.
Its scope and milestone gates also apply to Agent contributions.

Read [Android engineering contracts](docs/android-development.md) for module ownership, editor/worker lifetimes,
model updates, naming and storage invariants. Keep [Android usage](docs/android-user-guide.md) synchronized with
implemented behavior, not the future feature list. Documentation-only changes need fast checks; code changes need
impact-based mobile host tests, desktop regressions and APK/device validation as detailed in the engineering guide.

- Keep the C++ document, stable identities, undo, revisions, editing and proofreading rules shared with desktop.
  QML presents models and issues commands; JNI bridges Android facilities and never mutates the active document.
- Add focused mobile adapters after extracting genuinely shared rules from desktop controllers. Avoid hidden Widgets,
  duplicate workflow state, a monolithic mobile controller, or platform conditionals scattered through `MainWindow`.
- The first release uses imported app-owned workspace copies. Never treat a `content://` URI as a local path or promise
  local-filesystem transactions for arbitrary document providers. Direct external overwrite/synchronization is out of scope.
- Preserve separate states for recovery protection, workspace saves, generated exports and external sharing. A QQ
  handoff is not delivery confirmation. Standard Android sharing and picker import must converge on shared transfer services.
- Validate all archive paths, project image references, permissions, sizes and collisions before publishing imports.
  Keep existing data intact on cancellation, insufficient space, process death or partial provider writes.
- Serialize conflicting file operations in services. Workers use snapshots; callbacks validate document session and
  version. Handle IME composition and cancelled gestures explicitly rather than simulating desktop mouse shortcuts.
- Mobile builds may exclude Python and its private dependencies, but must retain the shared mutation/version contracts
  and preserve desktop automation behavior. No alternate editing or undo implementation is permitted.
- Every implementation change records its milestone, shared-boundary impact, recovery/cancellation behavior, regression
  evidence and remaining device checks. Real QQ/file-provider and lifecycle tests complement, not replace, unit tests.
- Keep Android support marked as planned/experimental until its release gates pass; sync the manual, build instructions,
  translation tools and dependency notices as the corresponding capabilities become real.

## Project Metadata In LabelPlus Comments

- LabelQt-specific project metadata must be stored in the unified compressed `LabelQtMetadata` comment block.
- Use `ProjectMetadataService` for comment block encoding, decoding and rewriting.
- Do not add new standalone `LabelQt...` comment blocks for future metadata sections.
- The block is compressed and base64-encoded for compactness and text safety. It is not encrypted; do not store secrets
  such as API keys in project metadata.

## Qt Licensing Constraints

- Keep the application on LGPL-available Qt modules unless the project intentionally changes to a GPL-compatible release strategy.
- Current required Qt dependencies are `Qt6::Core`, `Qt6::Gui`, `Qt6::Widgets` and `Qt6::Concurrent`. Review any additional module before introducing it. `Qt6::Svg` is an optional enhancement for bundled stylesheet icons.
- Do not introduce Qt GPL-only modules without documenting the license impact and getting an explicit project decision.
- Keep dynamic Qt as the baseline release path; the official Windows static variant must additionally satisfy the
  repository's static-linking checklist and relinking requirements.
- Windows dynamic and static builds, plus the Linux RPM, are the intended official binary release artifacts. Do not
  publish a static Qt binary until its Qt/static-dependency licenses, source offer and relinking instructions have been
  reviewed for that release.
- Linux static builds and DEB packages are experimental and are not part of the supported release matrix.
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

- User-visible C++ UI strings must use `tr()`. QML UI uses `qsTr()`/`qsTranslate()`; extraction and translation
  checks cover both frontends. Keep the same four language catalogs and verify runtime translation contexts.
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
- Bind active-document `LabelEditController` to `ProjectController`, not a detached `Project`, so mutations and undo
  publish monotonic versions independently of UI callbacks. External results must use `ProjectTaskService`, never
  mutate the live document from a worker thread. See `docs/automation-foundation.md` for scope and dependency rules.
- Preserve stable IDs for reorder, rename and undo; allocate new IDs for paste and merge. Store identities through
  `ProjectIdentityService` inside the unified metadata block. Add persistence and stale-task regression tests.
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
