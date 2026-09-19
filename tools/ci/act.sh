#!/usr/bin/env bash
# ============================================================================
# tools/ci/act.sh — correr el CI de GitHub (.github/workflows/ci.yml) en local
#
# Usa act (nektos/act, fijado por hash) sobre Docker, con la imagen de runner
# ubuntu-24.04 de catthehacker. Cada corrida arranca de una COPIA LIMPIA del
# repo (lo versionado + lo nuevo sin ignorar, con los cambios sin commitear)
# y un extern/ vacio, como en GitHub: asi se ve si los make bajan todo solos.
# Para no bajar gigas cada vez se reusan los tarballs de extern/.downloads
# (se verifican igual por hash).
#
# Uso: tools/ci/act.sh <job> [args de act...]
#      tools/ci/act.sh lint
#      tools/ci/act.sh sanitizers
#      tools/ci/act.sh linux
#      tools/ci/act.sh windows
# Variables: BA_ACT_DIR (donde van las copias, una por job; default /tmp/ba-act),
#            BA_ACT_KEEP=1 (reusar el extern/ de la corrida anterior).
# El job `release` no se corre en local (publica en GitHub).
# ============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
JOB="${1:?uso: $0 <job> [args de act]}"
shift
ACT_VER=0.2.89
ACT_SHA=0191d6f1f3b716b5c55820032605d05fc3c1cdbf581ebeff655019e5dd1524c0
IMAGE=ghcr.io/catthehacker/ubuntu:act-24.04
# Un directorio por job: como en GitHub, cada job tiene su propio extern/.
WORK="${BA_ACT_DIR:-/tmp/ba-act}/$JOB"
ACT="$ROOT/extern/.tools/act/act"

if [ "$("$ACT" --version 2>/dev/null)" != "act version $ACT_VER" ]; then
    f="$ROOT/extern/.downloads/act-$ACT_VER.tar.gz"
    mkdir -p "$(dirname "$f")" "$(dirname "$ACT")"
    curl -fL --retry 3 -o "$f.part" \
        "https://github.com/nektos/act/releases/download/v$ACT_VER/act_Linux_x86_64.tar.gz"
    [ "$(sha256sum "$f.part" | cut -d' ' -f1)" = "$ACT_SHA" ] || { echo "ERROR: hash de act" >&2; exit 1; }
    mv "$f.part" "$f"
    tar xzf "$f" -C "$(dirname "$ACT")" act
fi

# Copia limpia del arbol de trabajo.
SRC="$WORK/repo"
mkdir -p "$SRC"
(cd "$ROOT" && git ls-files -co --exclude-standard -z) |
    rsync -a --delete --from0 --files-from=- "$ROOT/" "$SRC/"
# Los contenedores de act corren como root: lo que dejaron se borra con Docker.
rootrm() { docker run --rm -v "$SRC:/w" -w /w alpine:3.20 sh -c "rm -rf $*"; }
rootrm build build-san-* build-win release release-win dist .venv
[ "${BA_ACT_KEEP:-0}" = 1 ] || rootrm extern
# act necesita un commit (github.sha, ref): uno local, descartable.
(cd "$SRC" && rm -rf .git && git init -q && git add -A &&
    git -c user.name=act -c user.email=act@localhost commit -qm act &&
    git branch -qM master)
mkdir -p "$SRC/extern/.downloads"
if [ -d "$ROOT/extern/.downloads" ]; then
    cp -aln "$ROOT/extern/.downloads/." "$SRC/extern/.downloads/" 2>/dev/null ||
        cp -an "$ROOT/extern/.downloads/." "$SRC/extern/.downloads/"
fi

mkdir -p "$WORK/artifacts" "$WORK/cache"
cd "$SRC"
exec "$ACT" push -W .github/workflows/ci.yml -j "$JOB" \
    -P ubuntu-24.04="$IMAGE" \
    --bind \
    --artifact-server-path "$WORK/artifacts" \
    --cache-server-path "$WORK/cache" \
    --env JOBS="$(nproc)" \
    --container-options "--security-opt seccomp=unconfined" \
    "$@"
