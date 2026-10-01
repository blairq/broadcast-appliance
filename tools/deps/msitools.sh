#!/usr/bin/env bash
# ============================================================================
# tools/deps/msitools.sh — abrir un .msi desde Linux, sin instalar nada
#
# Proposito:
#   El LibreOffice de Windows viene en un `.msi` y el bundle para Windows se
#   arma DESDE ACA. Un .msi no es un zip: es un documento compuesto OLE con
#   tablas (Directory, Component, File) y un .cab adentro. El arbol
#   `program/`, `share/` no esta escrito en ningun lado: se RECONSTRUYE
#   cruzando esas tablas. Eso es lo que hace `msiextract`.
#
# POR QUE NO `sudo apt install msitools`. Porque este proyecto no instala nada
# en la maquina: todo lo de afuera vive en extern/ con su hash, y cualquiera
# tiene que poder clonar y compilar sin tocar su sistema. Un `apt install` en
# las instrucciones de compilacion es una dependencia invisible que rompe en la
# proxima maquina —o en CI— y nadie se acuerda por que.
#
# ES UNA HERRAMIENTA DE COMPILACION, NO VIAJA EN EL BUNDLE. Es del lado de
# quien arma el paquete, como `ar`, `tar` o `curl`. Al aparato no llega nada de
# esto.
#
# 7z NO SIRVE, y conviene que quede escrito para que nadie lo intente de nuevo:
# abre el .msi pero escupe los flujos OLE crudos (_0001.cfs y asi). Medido
# sobre el .msi de LibreOffice: 19.496 archivos y ni un solo directorio.
#
# DE glibc Y glib NO NOS HACEMOS CARGO. libc6 y libglib2.0 estan en cualquier
# Linux con entorno grafico y vendorearlas seria arrastrar medio sistema para
# una herramienta que corre una vez. Se vendorean las que NO estan siempre:
# libmsi, libgcab y libgsf (esta ultima viene con GNOME, pero no en un servidor
# como el runner del CI, y sin ella msiextract no arranca).
#
# Uso: tools/deps/msitools.sh          (idempotente)
#      extern/msitools/msiextract ...  (envoltorio con su LD_LIBRARY_PATH)
# ============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DL="$ROOT/extern/.downloads"
OUT="$ROOT/extern/msitools"
BASE="http://archive.ubuntu.com/ubuntu/pool"

log()  { printf '\033[36m[msitools]\033[0m %s\n' "$*"; }
morir() { printf '\033[31m[msitools] ERROR:\033[0m %s\n' "$*" >&2; exit 1; }

# nombre|url|sha256 — el hash se verifica ANTES de abrir nada.
PAQUETES=(
  "msitools_0.103+repack-1build3_amd64.deb|$BASE/universe/m/msitools/msitools_0.103%2brepack-1build3_amd64.deb|a524b4ee9abc2d7d1d3cb0fd4a7629510285eb5c5271acab73c5c69e21eb1f58"
  "libmsi-1.0-0_0.103+repack-1build3_amd64.deb|$BASE/universe/m/msitools/libmsi-1.0-0_0.103%2brepack-1build3_amd64.deb|e6187a2eb31f595e61a3c864d83400060f06ff13517774dcc37e65d6730ff754"
  "libgcab-1.0-0_1.6-1build2_amd64.deb|$BASE/universe/g/gcab/libgcab-1.0-0_1.6-1build2_amd64.deb|b2ad7b79882a6c15192b88b039eac9ee29c94cd24197baa1caa8acb1169008fa"
  "libgsf-1-114_1.14.51-2ubuntu0.1_amd64.deb|$BASE/main/libg/libgsf/libgsf-1-114_1.14.51-2ubuntu0.1_amd64.deb|fa61257611e56ecbf75f42de477016fbb3792bfce9c837232254bdb0a2820b26"
)

bajar() { # nombre url sha
    local f="$DL/$1"
    if [ -f "$f" ] && [ "$(sha256sum "$f" | cut -d' ' -f1)" = "$3" ]; then
        log "$1 ya estaba y el hash coincide"
        return
    fi
    log "bajando $1"
    local silencio=(); [ -t 2 ] || silencio=(--no-progress-meter)
    curl -fL --retry 3 "${silencio[@]}" -o "$f.parcial" "$2" || morir "no se pudo bajar $1"
    # Se verifica ANTES de mover: un archivo a medias con el nombre bueno se
    # toma por bueno la proxima vez y falla mucho despues, lejos de la causa.
    echo "$3  $f.parcial" | sha256sum -c --quiet - || morir "el hash de $1 no coincide"
    mv "$f.parcial" "$f"
}

# Convertir de verdad es lo unico que prueba que quedo usable.
verificar() {
    [ -x "$OUT/msiextract" ] || morir "quedo sin msiextract"
    local salida
    salida="$("$OUT/msiextract" --version 2>&1)" || morir "msiextract no corre: $(echo "$salida" | head -3)"
    log "verificado: $("$OUT/msiextract" --version 2>&1 | head -1)"
}

if [ -x "$OUT/msiextract" ] && "$OUT/msiextract" --version >/dev/null 2>&1; then
    log "ya estaba"
    exit 0
fi

mkdir -p "$DL" "$OUT"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

for p in "${PAQUETES[@]}"; do
    IFS='|' read -r nombre url sha <<< "$p"
    bajar "$nombre" "$url" "$sha"
    # Los .deb se abren con `ar` + `tar`, que estan en cualquier Linux: no hace
    # falta dpkg ni permisos de root. Esto NO instala: extrae.
    ar p "$DL/$nombre" data.tar.zst 2>/dev/null | tar x --zstd -C "$tmp" 2>/dev/null ||
    ar p "$DL/$nombre" data.tar.xz  2>/dev/null | tar xJ      -C "$tmp" 2>/dev/null ||
        morir "no pude abrir $nombre"
done

rm -rf "$OUT/usr"
mkdir -p "$OUT"
cp -a "$tmp/usr" "$OUT/"
[ -x "$OUT/usr/bin/msiextract" ] || morir "el .deb no traia usr/bin/msiextract"

# El envoltorio: sin esto el binario busca libmsi en el sistema, que es
# exactamente lo que estamos evitando.
cat > "$OUT/msiextract" <<'ENV'
#!/usr/bin/env bash
# Envoltorio: apunta el binario a las bibliotecas de extern/, no a las del
# sistema. Ver tools/deps/msitools.sh.
aca="$(cd "$(dirname "$0")" && pwd)"
export LD_LIBRARY_PATH="$aca/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$aca/usr/bin/msiextract" "$@"
ENV
chmod +x "$OUT/msiextract"

verificar
log "listo: $(du -sh "$OUT" | cut -f1) en extern/msitools (no viaja en el bundle)"
