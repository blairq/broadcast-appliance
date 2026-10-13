#!/usr/bin/env bash
# tools/ci/sincronizar-docker.sh — compilar en Docker lo que todavía no está commiteado
#
#   tools/ci/sincronizar-docker.sh win gstbaipc test_bavideoanillo
#   tools/ci/sincronizar-docker.sh linux ba-app
#
# Pasa el árbol de trabajo al checkout del volumen (ba-trabajo:/trabajo/ba) por
# el mount de solo lectura del repo, con rsync POR CONTENIDO: solo se reescriben
# los archivos que cambiaron (con la hora de ahora), así Ninja recompila solo
# eso. Copiar el árbol entero (tar) les cambiaba la fecha a todos y el próximo
# build recompilaba mil y pico de pasos. Después compila los targets pedidos en
# build/win o build. Los .exe quedan en el volumen (sacarlos por ruta exacta).
#
# No toca extern/, build/, release/ ni .venv del volumen (no están en git), y el
# próximo `make dist-*-docker` vuelve el checkout al commit (git checkout -f).
set -euo pipefail
QUE="${1:?win|linux}"
shift
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
case "$QUE" in
win) DIR=build/win ;;
linux) DIR=build ;;
*) echo "win o linux" >&2; exit 2 ;;
esac
docker run --rm --platform linux/amd64 \
    -v ba-trabajo:/trabajo -v "$ROOT:/repo:ro" \
    -v ba-ccache:/ccache -e CCACHE_DIR=/ccache -e CCACHE_MAXSIZE=30G \
    -e DIR="$DIR" ba-compilar bash -c '
set -euo pipefail
git config --global --add safe.directory "*"
cd /repo
# Los de git y los nuevos sin ignorar; los borrados no (no hay qué copiar).
git ls-files -co --exclude-standard -z |
    while IFS= read -r -d "" f; do [ -e "$f" ] && printf "%s\0" "$f"; done |
    rsync -lc --from0 --files-from=- --out-format="%n" /repo/ /trabajo/ba/ | sed "s/^/  copiado: /"
cd /trabajo/ba
if [ $# -gt 0 ]; then cmake --build "$DIR" --target "$@"; fi
' sincronizar "$@"
