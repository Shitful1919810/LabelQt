#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

"$repo_root/scripts/check_translations.sh"
python3 -m compileall -q "$repo_root/scripts/official"
git -C "$repo_root" diff HEAD --check

echo "Fast checks passed."
