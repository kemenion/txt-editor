#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
source "$ROOT/third_party/env.sh"

if [[ ! -x "$ROOT/build/txt_core_tests" || ! -x "$ROOT/build/txt-editor" ]]; then
    "$ROOT/scripts/build.sh"
fi

ctest --test-dir "$ROOT/build" --output-on-failure
