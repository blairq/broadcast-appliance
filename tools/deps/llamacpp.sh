#!/usr/bin/env bash
# ============================================================================
# tools/deps/llamacpp.sh — llama.cpp, para correr un modelo chico ACA ADENTRO
#
# Proposito:
#   Los eventos inmediatos —«esta acoplando», «se cayo un invitado»— no pueden
#   esperar una ida y vuelta a un servidor: el operador reacciona en medio
#   segundo y el modelo tiene que llegar antes. Un modelo chico corriendo en la
#   misma maquina es la unica forma.
#
# POR QUE LA COMPILACION DE CPU y no una con GPU. Porque la GPU YA ESTA
# OCUPADA: libobs se come 42 ms por cuadro armando el programa (medido). No
# hay placa libre mientras el aparato trabaja, y pelearsela al motor de video
# para ganar unos milisegundos de texto es el peor negocio posible.
#
# Es una herramienta de MEDICION por ahora: no la usa el aparato todavia. Si
# los numeros dan, pasa a ser parte del bundle.
#
# Uso: tools/deps/llamacpp.sh        (idempotente)
# ============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DL="$ROOT/extern/.downloads"
OUT="$ROOT/extern/llamacpp"
VER="b11255"
ARCH="llama-$VER-bin-ubuntu-x64.tar.gz"
URL="https://github.com/ggml-org/llama.cpp/releases/download/$VER/$ARCH"
SHA="30733bba8d90fff5a54e26b1c75cb055d422c45595056349699a84eb362c4e92"

log()  { printf '\033[36m[llamacpp]\033[0m %s\n' "$*"; }
morir() { printf '\033[31m[llamacpp] ERROR:\033[0m %s\n' "$*" >&2; exit 1; }

if [ -x "$OUT/llama-server" ]; then
    log "ya estaba ($VER)"
    exit 0
fi

mkdir -p "$DL" "$OUT"
if [ ! -f "$DL/$ARCH" ] || [ "$(sha256sum "$DL/$ARCH" | cut -d' ' -f1)" != "$SHA" ]; then
    log "bajando $ARCH"
    local_silencio=(); [ -t 2 ] || local_silencio=(--no-progress-meter)
    curl -fL --retry 3 "${local_silencio[@]}" -o "$DL/$ARCH.parcial" "$URL" || morir "no se pudo bajar"
    echo "$SHA  $DL/$ARCH.parcial" | sha256sum -c --quiet - || morir "el hash no coincide"
    mv "$DL/$ARCH.parcial" "$DL/$ARCH"
fi

tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
tar xf "$DL/$ARCH" -C "$tmp"
# Se copia la carpeta ENTERA, no una seleccion: los binarios cargan unas
# bibliotecas que no se llaman `llama-*` (libmtmd, libggml-*, y sobre todo
# `libllama-common.so`, que se me escapo al filtrar por nombre y dejo un
# llama-server que no arrancaba).
raiz="$(find "$tmp" -maxdepth 1 -mindepth 1 -type d | head -1)"
[ -n "$raiz" ] || morir "el tarball no traia la carpeta esperada"
cp -a "$raiz/." "$OUT/"
chmod +x "$OUT"/llama-* 2>/dev/null || true
[ -x "$OUT/llama-server" ] || morir "no quedo llama-server"

# Correrlo de verdad: que este el archivo no dice que ande.
# Las bibliotecas estan al lado del binario, no en el sistema: hay que decirselo.
LD_LIBRARY_PATH="$OUT" "$OUT/llama-server" --version >/dev/null 2>&1 || morir "llama-server no corre"
log "listo: $(du -sh "$OUT" | cut -f1) en extern/llamacpp ($VER)"
