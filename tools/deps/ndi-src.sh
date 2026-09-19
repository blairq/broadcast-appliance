#!/usr/bin/env bash
# ============================================================================
# tools/deps/ndi-src.sh — fuentes de la recepcion NDI(R) abierta (Windows)
#
# libNoIDea (VideoLAN, LGPL) y libmicrodns (LGPL), fijadas por commit (los
# mismos que tools/deps/obs-deps-linux.sh). En Windows se compilan dentro del
# build de CMake (src/app/CMakeLists.txt); en Linux las arma el script de deps.
# ============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
eval "$(grep -E '^(NOIDEA|MICRODNS)_(URL|COMMIT)=' "$ROOT/tools/deps/obs-deps-linux.sh")"

gitpin() { # <dir> <url> <commit>
    local d="$1"
    if [ ! -d "$d/.git" ]; then
        rm -rf "$d"; git init -q "$d"
        git -C "$d" fetch -q --depth 1 "$2" "$3"
        git -C "$d" checkout -q FETCH_HEAD
    fi
    [ "$(git -C "$d" rev-parse HEAD)" = "$3" ] || { echo "[ndi-src] ERROR: commit de $d no coincide" >&2; exit 1; }
}
gitpin "$ROOT/extern/libnoidea" "$NOIDEA_URL" "$NOIDEA_COMMIT"
gitpin "$ROOT/extern/libmicrodns" "$MICRODNS_URL" "$MICRODNS_COMMIT"
echo "[ndi-src] libNoIDea y libmicrodns listos (commits fijados)" >&2
