#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
if (($#)); then
    printf '%s\n' 'The complete Web entry accepts no selectors; use npm test directly for diagnostics.' >&2
    exit 2
fi
root="$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$root/web"
npm run generate
npm test -- --runInBand
