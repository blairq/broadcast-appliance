#!/usr/bin/env bash
# ============================================================================
# tools/deps/libreoffice.sh — LibreOffice headless, SOLO para convertir
#
# Proposito:
#   Un orador manda su presentacion en `.pptx` y eso tiene que volverse un
#   carrusel de imagenes. Un `.pptx` es un zip con XML: sacar las diapositivas
#   COMO SE VEN necesita un motor de maquetado, y el unico que lo hace bien es
#   LibreOffice. Se usa UNA sola cosa de el:
#
#       soffice --headless --convert-to pdf  archivo.pptx
#
#   De ahi a imagenes va `mutool` (ver deps-mupdf), que es chico.
#
# NO SE PUEDE QUEDAR SOLO CON IMPRESS, y conviene saberlo antes de esperar
# milagros: LibreOffice moderno fusiona el nucleo en `libmergedlo.so`, que sola
# pesa 85 MB. Sacando Writer (20 MB), Calc (19 MB) y el arte de la interfaz
# (38 MB, que headless no se usa) se baja de ~355 MB a ~280 MB y ahi se termina.
#
# POR ESO ES OPCIONAL. No lo baja `make deps`: hay que pedirlo. Y el bundle lo
# incluye solo si esta (ver src/app/CMakeLists.txt). El que recibe PDF y no
# PowerPoint no carga 280 MB para nada.
#
# Y DE PASO, LAS FUENTES. El tarball oficial trae un juego tipografico decente
# —Liberation, Carlito, Caladea, DejaVu, Noto— que el aparato no tenia: lo unico
# que viajaba era JetBrains Mono, una monoespaciada de programar. Se extraen
# aparte, a `extern/libreoffice/fonts`, porque SIRVEN AUNQUE NO SE USE
# LibreOffice: son para los graficos y los rotulos. (El paquete de la
# distribucion NO las trae: depende de las del sistema. El oficial si.)
#
# LAS DOS PLATAFORMAS. El bundle se arma para Linux y para Windows desde esta
# misma maquina (ver deps-win), asi que se bajan las dos y se extraen sin
# instalar nada: el .tar.gz de .deb y el .msi.
#
# Uso: tools/deps/libreoffice.sh [linux|win|todo]   (idempotente)
# ============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DL="$ROOT/extern/.downloads"
OUT="$ROOT/extern/libreoffice"
QUE="${1:-todo}"

VER="26.2.6"
BASE="https://download.documentfoundation.org/libreoffice/stable/$VER"
LINUX_TGZ="LibreOffice_${VER}_Linux_x86-64_deb.tar.gz"
LINUX_SHA="fd0e8f8f2408dd2e5b90286e60f3f97cf566ba441cd48cfc5bcc68067303e0bc"
WIN_MSI="LibreOffice_${VER}_Win_x86-64.msi"
WIN_SHA="f9877032fd908beb9c0ddf06df4af5c2e85f419c42e14876c4cce5aae5fb2660"

log() { printf '\033[36m[libreoffice]\033[0m %s\n' "$*"; }
morir() { printf '\033[31m[libreoffice] ERROR:\033[0m %s\n' "$*" >&2; exit 1; }

mkdir -p "$DL" "$OUT"

bajar() { # nombre url sha
    local f="$DL/$1"
    if [ -f "$f" ] && [ "$(sha256sum "$f" | cut -d' ' -f1)" = "$3" ]; then
        log "$1 ya estaba y el hash coincide"
        return
    fi
    log "bajando $1 (son cientos de MB, tarda)"
    curl -fL --retry 3 -o "$f.parcial" "$2" || morir "no se pudo bajar $1"
    # Se verifica ANTES de mover: un archivo a medias con el nombre bueno se
    # toma por bueno la proxima vez y falla mucho despues, lejos de la causa.
    echo "$3  $f.parcial" | sha256sum -c --quiet - || morir "el hash de $1 no coincide"
    mv "$f.parcial" "$f"
}

# Lo que NO va, y por que: headless no dibuja interfaz, y no abrimos ni textos
# ni planillas. Son ~75 MB que no hacen nada.
podar() {
    local dir="$1"
    rm -f  "$dir/program/libswlo.so"  "$dir/program/libsclo.so"  2>/dev/null || true
    rm -f  "$dir/program/swlo.dll"    "$dir/program/sclo.dll"    2>/dev/null || true
    rm -rf "$dir/share/config" "$dir/share/gallery" "$dir/share/template" 2>/dev/null || true
    rm -rf "$dir/readmes" "$dir/share/readme" 2>/dev/null || true
}

fuentes_aparte() { # dir
    local origen="$1/share/fonts/truetype"
    [ -d "$origen" ] || return 0
    mkdir -p "$OUT/fonts"
    # Solo las de texto: las simbolicas y las de formulas no le sirven a nadie
    # que este haciendo un zocalo.
    find "$origen" -maxdepth 1 -iname '*.ttf' \
         ! -iname 'opens___*' ! -iname '*symbol*' -exec cp -n {} "$OUT/fonts/" \; 2>/dev/null || true
    log "fuentes: $(ls -1 "$OUT/fonts" 2>/dev/null | wc -l) archivos en extern/libreoffice/fonts"
}

hacer_linux() {
    [ -x "$OUT/linux/program/soffice" ] && { log "linux ya estaba"; fuentes_aparte "$OUT/linux"; return; }
    bajar "$LINUX_TGZ" "$BASE/deb/x86_64/$LINUX_TGZ" "$LINUX_SHA"
    local tmp; tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' RETURN
    log "extrayendo (sin instalar nada en el sistema)"
    tar xf "$DL/$LINUX_TGZ" -C "$tmp"
    # Los .deb se abren con `ar` + `tar`, que estan en cualquier Linux: no hace
    # falta dpkg ni permisos de root. Esto NO instala: extrae.
    mkdir -p "$tmp/x"
    local d
    while IFS= read -r d; do
        ( cd "$tmp/x" && ar p "$d" data.tar.xz 2>/dev/null | tar xJ 2>/dev/null ) || true
    done < <(find "$tmp" -name '*.deb' ! -name '*-sdk*' ! -name '*debug*')
    local raiz
    raiz="$(find "$tmp/x" -maxdepth 6 -type d -name program -path '*libreoffice*' | head -1)"
    [ -n "$raiz" ] || morir "no encontre program/ adentro de los .deb"
    raiz="$(dirname "$raiz")"
    rm -rf "$OUT/linux"
    mkdir -p "$OUT/linux"
    cp -a "$raiz/." "$OUT/linux/"
    podar "$OUT/linux"
    fuentes_aparte "$OUT/linux"
    [ -x "$OUT/linux/program/soffice" ] || morir "quedo sin soffice ejecutable"
    log "linux listo: $(du -sh "$OUT/linux" | cut -f1)"
}

hacer_win() {
    [ -f "$OUT/win/program/soffice.exe" ] && { log "windows ya estaba"; return; }
    bajar "$WIN_MSI" "$BASE/win/x86_64/$WIN_MSI" "$WIN_SHA"
    command -v msiextract >/dev/null 2>&1 || command -v 7z >/dev/null 2>&1 || \
        morir "para abrir el .msi desde Linux hace falta msitools (msiextract) o 7z"
    local tmp; tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' RETURN
    log "extrayendo el .msi"
    if command -v msiextract >/dev/null 2>&1; then
        ( cd "$tmp" && msiextract "$DL/$WIN_MSI" >/dev/null )
    else
        ( cd "$tmp" && 7z x -y "$DL/$WIN_MSI" >/dev/null )
    fi
    local raiz
    raiz="$(find "$tmp" -maxdepth 6 -type d -name program | head -1)"
    [ -n "$raiz" ] || morir "no encontre program/ adentro del .msi"
    raiz="$(dirname "$raiz")"
    rm -rf "$OUT/win"
    mkdir -p "$OUT/win"
    cp -a "$raiz/." "$OUT/win/"
    podar "$OUT/win"
    log "windows listo: $(du -sh "$OUT/win" | cut -f1)"
}

case "$QUE" in
    linux) hacer_linux ;;
    win)   hacer_win ;;
    todo)  hacer_linux; hacer_win ;;
    *)     morir "uso: $0 [linux|win|todo]" ;;
esac

log "listo. El bundle lo incluye solo si extern/libreoffice existe."
