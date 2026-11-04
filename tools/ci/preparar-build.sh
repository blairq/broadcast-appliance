#!/bin/sh
# tools/ci/preparar-build.sh — lo que el Makefile hace antes de compilar, en un solo lugar.
#
#   preparar-build.sh enlaces
#       En un worktree de git (los de los agentes): extern/ y build/modelo-mac son
#       enlaces a los del árbol principal, si acá faltan y allá están. Sin eso, un
#       worktree nuevo no configura (extern/ vacío) y `make compilar` recompila MLX.
#
#   preparar-build.sh configurar [--si-falta] DIR [opciones de cmake...]
#       Configura DIR con cmake SOLO si hace falta:
#         - no está configurado (sin CMakeCache.txt o sin build.ninja/Makefile), o
#         - alguna -DNOMBRE=VALOR pedida no es la que tiene el caché, o
#         - SIN_CACHE=1 (lo que se publica se configura siempre).
#       Con --si-falta solo mira lo primero: no cambia las opciones de un build que
#       ya existe (lo usan `make app` y `make test`, que no deben pasar build/ de
#       publicar a desarrollo ni al revés). Si no, cmake --build vuelve a configurar
#       solo cuando cambia un CMakeLists: llamar a cmake -S -B cada vez son segundos
#       perdidos.
#       Un -G (generador) se pasa solo la primera vez: no se puede cambiar después.
set -eu

raiz="$(cd "$(dirname "$0")/../.." && pwd)"

enlaces() {
    comun="$(git -C "$raiz" rev-parse --path-format=absolute --git-common-dir 2>/dev/null || true)"
    propio="$(git -C "$raiz" rev-parse --path-format=absolute --git-dir 2>/dev/null || true)"
    [ -n "$comun" ] && [ "$comun" != "$propio" ] || return 0   # no es un worktree
    principal="$(dirname "$comun")"
    if [ ! -e "$raiz/extern" ] && [ -d "$principal/extern" ]; then
        ln -s "$principal/extern" "$raiz/extern"
        echo "[preparar] worktree: extern -> $principal/extern"
    fi
    if [ "$(uname -s)" = Darwin ] && [ ! -e "$raiz/build/modelo-mac" ] && [ -d "$principal/build/modelo-mac" ]; then
        mkdir -p "$raiz/build"
        ln -s "$principal/build/modelo-mac" "$raiz/build/modelo-mac"
        echo "[preparar] worktree: build/modelo-mac -> $principal/build/modelo-mac"
    fi
    if command -v ccache >/dev/null 2>&1 && [ -z "$(ccache -k base_dir 2>/dev/null)" ]; then
        echo "[preparar] AVISO: ccache sin base_dir: este worktree no comparte la caché con el principal" >&2
        echo "[preparar]   ccache -o base_dir=$principal" >&2
    fi
}

# El valor de NOMBRE en el CMakeCache.txt (sin el tipo), o nada.
en_cache() {
    awk -v k="$1" 'index($0, k ":") == 1 || index($0, k "=") == 1 { sub(/^[^=]*=/, ""); print; exit }' "$2"
}

configurar() {
    si_falta=0
    if [ "${1:-}" = --si-falta ]; then si_falta=1; shift; fi
    dir="$1"; shift
    cache="$dir/CMakeCache.txt"
    motivo=""
    if [ ! -f "$cache" ] || { [ ! -f "$dir/build.ninja" ] && [ ! -f "$dir/Makefile" ]; }; then
        motivo="$dir no está configurado"
    elif [ "$si_falta" = 0 ] && [ "${SIN_CACHE:-0}" != 0 ]; then
        motivo="SIN_CACHE=1"
    elif [ "$si_falta" = 0 ]; then
        for o in "$@"; do
            case "$o" in
                -D*=*)
                    kv="${o#-D}"; k="${kv%%=*}"; v="${kv#*=}"; k="${k%%:*}"
                    actual="$(en_cache "$k" "$cache")"
                    if [ "$actual" != "$v" ]; then motivo="${k}: «${actual}» -> «${v}»"; break; fi ;;
            esac
        done
    fi
    if [ -z "$motivo" ]; then
        echo "[configurar] $dir: al día (cmake --build vuelve a configurar solo si cambia un CMakeLists)"
        return 0
    fi
    # El generador, solo la primera vez (con caché, cmake lo rechaza si es otro).
    if [ -f "$cache" ]; then
        n=$#; saltar=0
        for o in "$@"; do
            if [ "$saltar" = 1 ]; then saltar=0
            elif [ "$o" = -G ]; then saltar=1
            else set -- "$@" "$o"; fi
        done
        shift "$n"
    fi
    echo "[configurar] $dir: $motivo"
    cmake -S "$raiz" -B "$dir" "$@"
}

case "${1:-}" in
    enlaces) enlaces ;;
    configurar) shift; enlaces; configurar "$@" ;;
    *) echo "uso: $0 enlaces | configurar [--si-falta] DIR [opciones de cmake...]" >&2; exit 2 ;;
esac
