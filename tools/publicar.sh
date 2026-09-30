#!/usr/bin/env bash
# ============================================================================
# tools/publicar.sh — subir a un release de GitHub lo que se armo EN ESTA
# maquina (el .tar.gz de Linux, el .zip de Windows, el .dmg de macOS)
#
# El CI de GitHub corre solo a mano (y gasta minutos del cupo). Esto es la otra
# via: compilar en una maquina propia y subir de ahi. Lo llaman
# make publicar-linux / publicar-win (en Linux) y make publicar-macos (en una
# Mac), despues de armar el archivo.
#
# EL RELEASE SALE DEL COMMIT: el tag es v<version>-<commit corto>. Asi, lo que
# se arma en dos maquinas desde el MISMO commit cae en el mismo release: el
# primero lo crea y los demas le suman sus archivos. TAG=... elige otro.
#
# Se niega (FORZAR=1 saltea los controles) si:
#   - hay cambios sin commitear: lo subido no seria ningun commit;
#   - el commit no esta en GitHub: el release apuntaria a algo que no existe;
#   - el release ya existe y es de OTRO commit: mezclaria versiones.
#
# Es "Latest" si se publica desde master; si no, sale como pre-release.
#
# Uso: tools/publicar.sh <archivo> [<archivo>...]      (con su .sha256 al lado)
#      tools/publicar.sh --solo-chequear               (los controles, antes de
#                                                       compilar: make lo usa asi)
# ============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
log() { echo "[publicar] $*" >&2; }
morir() { echo "[publicar] ERROR: $*" >&2; exit 1; }

solo_chequear=0
[ "${1:-}" = --solo-chequear ] && { solo_chequear=1; shift; }
[ $# -gt 0 ] || [ "$solo_chequear" = 1 ] || morir "uso: $0 <archivo> [<archivo>...]"
command -v gh >/dev/null 2>&1 || morir "falta gh (GitHub CLI) y haber hecho 'gh auth login'"
for f in "$@"; do [ -f "$f" ] || morir "no existe $f (¿se armo?)"; done

forzar="${FORZAR:-0}"
sha="$(git rev-parse HEAD)"
corto="$(git rev-parse --short=7 HEAD)"
rama="$(git branch --show-current)"
ver="$(awk '/^project\(/,/\)/' CMakeLists.txt | awk '/VERSION/{print $2; exit}')"
tag="${TAG:-v${ver}-${corto}}"

if [ "$forzar" != 1 ]; then
    [ -z "$(git status --porcelain --untracked-files=no)" ] ||
        morir "hay cambios sin commitear: lo que se subiria no es ningun commit"
    git fetch -q origin 2>/dev/null || true
    [ -n "$(git branch -r --contains "$sha" 2>/dev/null)" ] ||
        morir "el commit $corto no esta en GitHub: subilo primero (git push)"
fi

if gh release view "$tag" >/dev/null 2>&1; then
    del="$(gh release view "$tag" --json targetCommitish -q .targetCommitish)"
    if [ "$del" != "$sha" ] && [ "$forzar" != 1 ]; then
        morir "el release $tag es del commit $del y aca estas en $sha: mezclaria versiones"
    fi
    log "release $tag ya existe: le sumo los archivos"
elif [ "$solo_chequear" = 1 ]; then
    log "se va a crear el release $tag"
else
    log "creando el release $tag (commit $corto, rama ${rama:-?})"
    extra=(--prerelease --latest=false)
    [ "$rama" = master ] && extra=(--latest)
    gh release create "$tag" --target "$sha" --title "Broadcast Appliance $tag" \
        --notes-file packaging/notas-release.md "${extra[@]}" >/dev/null
fi

[ "$solo_chequear" = 1 ] && { log "controles OK (commit $corto)"; exit 0; }

archivos=()
for f in "$@"; do
    archivos+=("$f")
    [ -f "$f.sha256" ] && archivos+=("$f.sha256")
done
gh release upload "$tag" "${archivos[@]}" --clobber
log "publicado en $tag: $(printf '%s ' "${archivos[@]##*/}")"
gh release view "$tag" --json url -q .url
