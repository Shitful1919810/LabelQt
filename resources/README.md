# Resources

This directory is reserved for runtime assets that should ship with the Qt port.

Current resource groups:

- `themes/`: bundled application stylesheet themes and their license files.

Automation scripts live in the top-level `scripts/official` and `scripts/custom` directories, not under `resources/`.

Qt translation source files currently live in the top-level `translations/` directory. CMake compiles generated `.qm` files into the application resource system when `Qt6LinguistTools` is available.
